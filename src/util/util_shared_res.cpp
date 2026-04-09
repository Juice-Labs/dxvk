#include "util_shared_res.h"
#include "util_string.h"
#include "log/log.h"

#ifdef _WIN32
#include <mutex>
#include <cstring>
#include "ExternalHandleShared.h"
#endif

namespace dxvk {

#ifdef _WIN32
  HANDLE openKmtHandle(HANDLE kmt_handle) {
    Logger::trace(str::format("openKmtHandle: passthrough handle ", reinterpret_cast<uint64_t>(kmt_handle)));
    return kmt_handle;
  }

  typedef bool (*PFN_Juice_GetExtHandleSharedMemory)(void** ppSharedData, void** ppMutex);
  typedef bool (*PFN_Juice_ResolveHandle)(void* handle, void* outResult);

  struct JuiceExtHandleState {
    ExternalHandleSharedData* sharedData = nullptr;
    HANDLE mutex = nullptr;
    PFN_Juice_ResolveHandle pfnResolve = nullptr;
  };

  static JuiceExtHandleState g_juiceState;
  static std::once_flag      g_juiceInitFlag;

  static void initJuiceSharedMemory() {
    HMODULE hIcd = ::GetModuleHandleA("RemoteGPUVlk.dll");
    if (!hIcd) {
      Logger::warn("util_shared_res: RemoteGPUVlk.dll not loaded in this process");
      return;
    }

    auto pfn = reinterpret_cast<PFN_Juice_GetExtHandleSharedMemory>(
      ::GetProcAddress(hIcd, "Juice_GetExtHandleSharedMemory"));
    if (!pfn) {
      Logger::warn("util_shared_res: Juice_GetExtHandleSharedMemory not found in ICD");
      return;
    }

    void* pShared = nullptr;
    void* pMutex  = nullptr;
    if (!pfn(&pShared, &pMutex) || !pShared || !pMutex) {
      Logger::warn("util_shared_res: ICD returned no shared memory (client not connected yet?)");
      return;
    }

    auto* sd = reinterpret_cast<ExternalHandleSharedData*>(pShared);
    if (sd->magic != kExternalHandleSharedMagic) {
      Logger::warn("util_shared_res: shared memory magic mismatch");
      return;
    }

    g_juiceState.sharedData = sd;
    g_juiceState.mutex = static_cast<HANDLE>(pMutex);

    g_juiceState.pfnResolve = reinterpret_cast<PFN_Juice_ResolveHandle>(
      ::GetProcAddress(hIcd, "Juice_ResolveHandle"));

    Logger::trace("util_shared_res: Juice shared memory acquired from ICD");
  }

  static JuiceExtHandleState& juiceState() {
    std::call_once(g_juiceInitFlag, initJuiceSharedMemory);
    return g_juiceState;
  }


  bool setSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize) {
    auto& st = juiceState();
    if (!st.sharedData || !st.mutex) {
      Logger::warn("setSharedMetadata: Juice shared memory not available");
      return false;
    }

    if (bufSize > kMaxTextureMetadataSize) {
      Logger::warn("setSharedMetadata: metadata too large for shared entry");
      return false;
    }

    uint64_t key = reinterpret_cast<uint64_t>(handle);

    ::WaitForSingleObject(st.mutex, INFINITE);

    uint32_t count = st.sharedData->count.load(std::memory_order_relaxed);
    bool found = false;
    for (uint32_t i = 0; i < count && i < kMaxExternalHandles; ++i) {
      auto& e = st.sharedData->entries[i];
      if (e.refCount.load(std::memory_order_acquire) > 0 && e.localHandle == key) {
        std::memcpy(e.textureMetadata, buf, bufSize);
        e.textureMetadataSize = bufSize;
        e.hasTextureMetadata.store(1, std::memory_order_release);
        found = true;
        break;
      }
    }

    ::ReleaseMutex(st.mutex);

    if (found)
      Logger::trace(str::format("setSharedMetadata: OK handle=", key, " (slot ", count, " entries)"));
    else
      Logger::warn(str::format("setSharedMetadata: handle ", key, " not found in shared table (", count, " entries)"));

    return found;
  }


  bool getSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize, uint32_t *metadataSize) {
    auto& st = juiceState();

    if (st.pfnResolve) {
      ExternalHandleResolveResult result{};
      if (st.pfnResolve(handle, &result) && result.hasTextureMetadata &&
          result.textureMetadataSize > 0 && result.textureMetadataSize <= bufSize) {
        std::memcpy(buf, result.textureMetadata, result.textureMetadataSize);
        if (metadataSize)
          *metadataSize = result.textureMetadataSize;
        Logger::trace(str::format("getSharedMetadata: OK via ResolveHandle handle=",
          reinterpret_cast<uint64_t>(handle)));
        return true;
      }
      Logger::warn(str::format("getSharedMetadata: ResolveHandle returned no metadata for handle=",
        reinterpret_cast<uint64_t>(handle)));
      return false;
    }

    if (!st.sharedData || !st.mutex)
      return false;

    ::WaitForSingleObject(st.mutex, INFINITE);

    uint32_t count = st.sharedData->count.load(std::memory_order_relaxed);
    uint64_t key = reinterpret_cast<uint64_t>(handle);

    uint64_t serverHandle = 0;
    const ExternalHandleEntry* direct = nullptr;

    for (uint32_t i = 0; i < count && i < kMaxExternalHandles; ++i) {
      auto& e = st.sharedData->entries[i];
      if (e.refCount.load(std::memory_order_acquire) > 0 && e.localHandle == key) {
        serverHandle = e.handle;
        if (e.hasTextureMetadata.load(std::memory_order_acquire))
          direct = &e;
        break;
      }
    }

    const ExternalHandleEntry* source = direct;
    if (!source && serverHandle) {
      for (uint32_t i = 0; i < count && i < kMaxExternalHandles; ++i) {
        auto& e = st.sharedData->entries[i];
        if (e.refCount.load(std::memory_order_acquire) > 0 &&
            e.handle == serverHandle &&
            e.hasTextureMetadata.load(std::memory_order_acquire)) {
          source = &e;
          break;
        }
      }
    }

    bool ok = false;
    if (source) {
      uint32_t sz = source->textureMetadataSize;
      if (sz <= bufSize) {
        std::memcpy(buf, source->textureMetadata, sz);
        if (metadataSize)
          *metadataSize = sz;
        ok = true;
      }
    }

    ::ReleaseMutex(st.mutex);

    if (ok)
      Logger::trace(str::format("getSharedMetadata: OK handle=", key,
        " (serverHandle=", serverHandle, ", via ", (source == direct ? "direct" : "server-handle-fallback"), ")"));
    else
      Logger::warn(str::format("getSharedMetadata: FAILED handle=", key,
        " (serverHandle=", serverHandle, ", ", count, " entries searched)"));

    return ok;
  }

#else
  HANDLE openKmtHandle(HANDLE kmt_handle) {
    Logger::warn("openKmtHandle: Shared resources not available on this platform.");
    return INVALID_HANDLE_VALUE;
  }

  bool setSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize) {
    Logger::warn("setSharedMetadata: Shared resources not available on this platform.");
    return false;
  }

  bool getSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize, uint32_t *metadataSize) {
    Logger::warn("getSharedMetadata: Shared resources not available on this platform.");
    return false;
  }

#endif

}
