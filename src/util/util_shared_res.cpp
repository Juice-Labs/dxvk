#include "util_shared_res.h"
#include "log/log.h"

#ifdef _WIN32
#include <winioctl.h>
#endif

namespace dxvk {

#define METADATA_PREFIX L"Local\\DxvkMetadata_"
#define MAX_METADATA_SIZE 4096  // Adjust as needed

  HANDLE openKmtHandle(HANDLE kmt_handle) {
    // Create unique name for this KMT handle's mapping
    WCHAR mappingName[64];
    swprintf(mappingName, 64, METADATA_PREFIX L"%p", kmt_handle);

    // Try to open existing mapping first
    HANDLE hMapFile = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mappingName);
    if (hMapFile == NULL) {
        // If it doesn't exist, create a new one
        hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, 
                                    NULL,
                                    PAGE_READWRITE,
                                    0,
                                    MAX_METADATA_SIZE + sizeof(uint32_t),
                                    mappingName);
        if (hMapFile != NULL) {
            // Duplicate the handle and intentionally leak it
            HANDLE leakedHandle;
            DuplicateHandle(GetCurrentProcess(), hMapFile,
                          GetCurrentProcess(), &leakedHandle,
                          0, FALSE, DUPLICATE_SAME_ACCESS);
        }
    }
    
    return hMapFile != NULL ? hMapFile : INVALID_HANDLE_VALUE;
  }

  static std::unordered_map<HANDLE, std::vector<uint8_t>> s_sharedMetadata;

  bool setSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize) {
    if (buf == nullptr || bufSize == 0 || bufSize > MAX_METADATA_SIZE)
        return false;

    // Map view of the file - handle is already the mapping handle
    void* pView = MapViewOfFile(handle, FILE_MAP_WRITE, 0, 0, bufSize + sizeof(uint32_t));
    if (pView == NULL)
        return false;

    // Write size and data
    *static_cast<uint32_t*>(pView) = bufSize;
    memcpy(static_cast<uint8_t*>(pView) + sizeof(uint32_t), buf, bufSize);

    UnmapViewOfFile(pView);
    return true;
  }

  bool getSharedMetadata(HANDLE handle, void *buf, uint32_t bufSize, uint32_t *metadataSize) {
    // Map view of the file - handle is already the mapping handle
    void* pView = MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0);
    if (pView == NULL)
        return false;

    uint32_t storedSize = *static_cast<uint32_t*>(pView);
    if (metadataSize)
        *metadataSize = storedSize;

    if (buf && bufSize > 0) {
        size_t copySize = std::min(bufSize, storedSize);
        memcpy(buf, static_cast<uint8_t*>(pView) + sizeof(uint32_t), copySize);
    }

    UnmapViewOfFile(pView);
    return true;
  }
}
