#include <tuple>

#include "vulkan_loader.h"
#include <mutex>
#include <assert.h>

#include "../util/log/log.h"

#include "../util/util_string.h"
#include "../util/util_win32_compat.h"

namespace dxvk::vk {

  static PFN_vkVoidFunction GetInstanceProcAddr(VkInstance instance, const char* name) {
    static PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;

    static std::once_flag loaded;
    std::call_once(loaded, []() {
      HMODULE juiceVlk = LoadLibraryExA("JuiceVlk.dll", NULL, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
      assert(juiceVlk);
      GetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(juiceVlk, "vkGetInstanceProcAddr");
      assert(GetInstanceProcAddr);
    });

    if(GetInstanceProcAddr != nullptr)
      return GetInstanceProcAddr(instance, name);

    return nullptr;
  }

  PFN_vkVoidFunction LibraryLoader::sym(const char* name) const {
    return dxvk::vk::GetInstanceProcAddr(nullptr, name);
  }
  
  
  InstanceLoader::InstanceLoader(bool owned, VkInstance instance)
  : m_instance(instance), m_owned(owned) { }
  
  
  PFN_vkVoidFunction InstanceLoader::sym(const char* name) const {
    return dxvk::vk::GetInstanceProcAddr(m_instance, name);
  }
  
  
  DeviceLoader::DeviceLoader(bool owned, VkInstance instance, VkDevice device)
  : m_getDeviceProcAddr(reinterpret_cast<PFN_vkGetDeviceProcAddr>(
      dxvk::vk::GetInstanceProcAddr(instance, "vkGetDeviceProcAddr"))),
    m_device(device), m_owned(owned) { }
  
  
  PFN_vkVoidFunction DeviceLoader::sym(const char* name) const {
    return m_getDeviceProcAddr(m_device, name);
  }
  
  
  LibraryFn::LibraryFn() { }
  LibraryFn::~LibraryFn() { }
  
  
  InstanceFn::InstanceFn(bool owned, VkInstance instance)
  : InstanceLoader(owned, instance) { }
  InstanceFn::~InstanceFn() {
    if (m_owned)
      this->vkDestroyInstance(m_instance, nullptr);
  }
  
  
  DeviceFn::DeviceFn(bool owned, VkInstance instance, VkDevice device)
  : DeviceLoader(owned, instance, device) { }
  DeviceFn::~DeviceFn() {
    if (m_owned)
      this->vkDestroyDevice(m_device, nullptr);
  }
  
}