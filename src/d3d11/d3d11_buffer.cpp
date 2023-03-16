#include "d3d11_buffer.h"
#include "d3d11_context.h"
#include "d3d11_device.h"

#include "../dxvk/dxvk_data.h"

namespace dxvk {
  
  D3D11Buffer::D3D11Buffer(
          D3D11Device*                pDevice,
    const D3D11_BUFFER_DESC*          pDesc)
  : D3D11DeviceChild<ID3D11Buffer>(pDevice),
    m_desc        (*pDesc),
    m_resource    (this),
    m_d3d10       (this) {
    DxvkBufferCreateInfo info;
    info.flags  = 0;
    info.size   = pDesc->ByteWidth;
    info.usage  = VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
    info.access = VK_ACCESS_TRANSFER_READ_BIT
                | VK_ACCESS_TRANSFER_WRITE_BIT;
    
    if (pDesc->BindFlags & D3D11_BIND_VERTEX_BUFFER) {
      info.usage  |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
      info.stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
      info.access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    
    if (pDesc->BindFlags & D3D11_BIND_INDEX_BUFFER) {
      info.usage  |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
      info.stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
      info.access |= VK_ACCESS_INDEX_READ_BIT;
    }
    
    if (pDesc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) {
      info.usage  |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      info.stages |= m_parent->GetEnabledShaderStages();
      info.access |= VK_ACCESS_UNIFORM_READ_BIT;
    }
    
    if (pDesc->BindFlags & D3D11_BIND_SHADER_RESOURCE) {
      info.usage  |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
                  |  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      info.stages |= m_parent->GetEnabledShaderStages();
      info.access |= VK_ACCESS_SHADER_READ_BIT;
    }
    
    if (pDesc->BindFlags & D3D11_BIND_STREAM_OUTPUT) {
      info.usage  |= VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT;
      info.stages |= VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
      info.access |= VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT;
    }
    
    if (pDesc->BindFlags & D3D11_BIND_UNORDERED_ACCESS) {
      info.usage  |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
                  |  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      info.stages |= m_parent->GetEnabledShaderStages();
      info.access |= VK_ACCESS_SHADER_READ_BIT
                  |  VK_ACCESS_SHADER_WRITE_BIT;
    }
    
    if (pDesc->MiscFlags & D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS) {
      info.usage  |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
      info.stages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
      info.access |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }

    // Create the buffer and set the entire buffer slice as mapped,
    // so that we only have to update it when invalidating th buffer
    auto d3d11BufferCreateInfo = GetJuiceInfo();

    if (pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILED) {
      info.flags  |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT
                  |  VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT
                  |  VK_BUFFER_CREATE_SPARSE_ALIASED_BIT;
    }

    // Set host read bit as necessary. We may internally read staging
    // buffer contents even if the buffer is not marked for reading.
    if (pDesc->CPUAccessFlags && pDesc->Usage != D3D11_USAGE_DYNAMIC) {
      info.stages |= VK_PIPELINE_STAGE_HOST_BIT;
      info.access |= VK_ACCESS_HOST_READ_BIT;

      if (pDesc->CPUAccessFlags & D3D11_CPU_ACCESS_WRITE)
        info.access |= VK_ACCESS_HOST_WRITE_BIT;
    }

    if (!(pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILE_POOL)) {
      // Create the buffer and set the entire buffer slice as mapped,
      // so that we only have to update it when invalidating the buffer
      m_buffer = m_parent->GetDXVKDevice()->createBuffer(info, GetMemoryFlags());
      m_mapped = m_buffer->getSliceHandle();

      m_mapMode = DetermineMapMode();

      // For Stream Output buffers we need a counter
      if (pDesc->BindFlags & D3D11_BIND_STREAM_OUTPUT)
        m_soCounter = CreateSoCounterBuffer();
    } else {
      m_sparseAllocator = m_parent->GetDXVKDevice()->createSparsePageAllocator();
      m_sparseAllocator->setCapacity(info.size / SparseMemoryPageSize);

      m_mapped = DxvkBufferSliceHandle();
      m_mapMode = D3D11_COMMON_BUFFER_MAP_MODE_NONE;
    }
  }
  
  
  D3D11Buffer::~D3D11Buffer() {

  }
  
  
  HRESULT STDMETHODCALLTYPE D3D11Buffer::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr)
      return E_POINTER;

    *ppvObject = nullptr;
    
    if (riid == __uuidof(IUnknown)
     || riid == __uuidof(ID3D11DeviceChild)
     || riid == __uuidof(ID3D11Resource)
     || riid == __uuidof(ID3D11Buffer)) {
      *ppvObject = ref(this);
      return S_OK;
    }
    
    if (riid == __uuidof(ID3D10DeviceChild)
     || riid == __uuidof(ID3D10Resource)
     || riid == __uuidof(ID3D10Buffer)) {
      *ppvObject = ref(&m_d3d10);
      return S_OK;
    }

    if (riid == __uuidof(IDXGIObject)
     || riid == __uuidof(IDXGIDeviceSubObject)
     || riid == __uuidof(IDXGIResource)
     || riid == __uuidof(IDXGIResource1)) {
       *ppvObject = ref(&m_resource);
       return S_OK;
    }
    
    if (logQueryInterfaceError(__uuidof(ID3D11Buffer), riid)) {
      Logger::warn("D3D11Buffer::QueryInterface: Unknown interface query");
      Logger::warn(str::format(riid));
    }

    return E_NOINTERFACE;
  }
  
  
  UINT STDMETHODCALLTYPE D3D11Buffer::GetEvictionPriority() {
    return DXGI_RESOURCE_PRIORITY_NORMAL;
  }
  
  
  void STDMETHODCALLTYPE D3D11Buffer::SetEvictionPriority(UINT EvictionPriority) {
    static bool s_errorShown = false;

    if (!std::exchange(s_errorShown, true))
      Logger::warn("D3D11Buffer::SetEvictionPriority: Stub");
  }
  
  
  void STDMETHODCALLTYPE D3D11Buffer::GetType(D3D11_RESOURCE_DIMENSION* pResourceDimension) {
    *pResourceDimension = D3D11_RESOURCE_DIMENSION_BUFFER;
  }
  
  
  void STDMETHODCALLTYPE D3D11Buffer::GetDesc(D3D11_BUFFER_DESC* pDesc) {
    *pDesc = m_desc;
  }
  
  
  bool D3D11Buffer::CheckViewCompatibility(
          UINT                BindFlags,
          DXGI_FORMAT         Format) const {
    // Check whether the given bind flags are supported
    if ((m_desc.BindFlags & BindFlags) != BindFlags)
      return false;

    // Structured buffer views use no format
    if (Format == DXGI_FORMAT_UNKNOWN)
      return (m_desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0;

    // Check whether the given combination of buffer view
    // type and view format is supported by the device
    DXGI_VK_FORMAT_INFO viewFormat = m_parent->LookupFormat(Format, DXGI_VK_FORMAT_MODE_ANY);
    VkFormatFeatureFlags2 features = GetBufferFormatFeatures(BindFlags);

    return CheckFormatFeatureSupport(viewFormat.Format, features);
  }


  HRESULT D3D11Buffer::NormalizeBufferProperties(D3D11_BUFFER_DESC* pDesc) {
    // Zero-sized buffers are illegal
    if (!pDesc->ByteWidth && !(pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILE_POOL))
      return E_INVALIDARG;

    // Constant buffer size must be a multiple of 16
    if ((pDesc->BindFlags & D3D11_BIND_CONSTANT_BUFFER)
     && (pDesc->ByteWidth & 0xF))
      return E_INVALIDARG;

    // Basic validation for structured buffers
    if ((pDesc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)
     && ((pDesc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
      || (pDesc->StructureByteStride == 0)
      || (pDesc->StructureByteStride & 0x3)))
      return E_INVALIDARG;
    
    // Basic validation for raw buffers
    if ((pDesc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)
     && (!(pDesc->BindFlags & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS))))
      return E_INVALIDARG;

    // Mip generation obviously doesn't work for buffers
    if (pDesc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
      return E_INVALIDARG;

    // Basic validation for tiled buffers
    if (pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILED) {
      if ((pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILE_POOL)
       || (pDesc->Usage != D3D11_USAGE_DEFAULT)
       || (pDesc->CPUAccessFlags))
        return E_INVALIDARG;
    }

    // Basic validation for tile pools
    if (pDesc->MiscFlags & D3D11_RESOURCE_MISC_TILE_POOL) {
      if ((pDesc->MiscFlags & ~D3D11_RESOURCE_MISC_TILE_POOL)
       || (pDesc->ByteWidth % SparseMemoryPageSize)
       || (pDesc->Usage != D3D11_USAGE_DEFAULT)
       || (pDesc->BindFlags)
       || (pDesc->CPUAccessFlags))
        return E_INVALIDARG;
    }

    if (!(pDesc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED))
      pDesc->StructureByteStride = 0;
    
    return S_OK;
  }


  BOOL D3D11Buffer::CheckFormatFeatureSupport(
          VkFormat              Format,
          VkFormatFeatureFlags2 Features) const {
    DxvkFormatFeatures support = m_parent->GetDXVKDevice()->getFormatFeatures(Format);
    return (support.buffer & Features) == Features;
  }


  VkMemoryPropertyFlags D3D11Buffer::GetMemoryFlags() const {
    VkMemoryPropertyFlags memoryFlags = 0;

    if (m_desc.MiscFlags & (D3D11_RESOURCE_MISC_TILE_POOL | D3D11_RESOURCE_MISC_TILED))
      return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    switch (m_desc.Usage) {
      case D3D11_USAGE_IMMUTABLE:
        memoryFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;

      case D3D11_USAGE_DEFAULT:
        memoryFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if ((m_desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) || m_desc.CPUAccessFlags) {
          memoryFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                      |  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        if (m_desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) {
          memoryFlags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
          memoryFlags &= ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
        break;
      
      case D3D11_USAGE_DYNAMIC:
        memoryFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    |  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        if (m_desc.BindFlags)
          memoryFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        break;
      
      case D3D11_USAGE_STAGING:
        memoryFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                    |  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                    |  VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        break;
    }
    
    bool useCached = (m_parent->GetOptions()->cachedDynamicResources == ~0u)
                  || (m_parent->GetOptions()->cachedDynamicResources & m_desc.BindFlags);

    if ((memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && useCached) {
      memoryFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                  |  VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }

    return memoryFlags;
  }


  VkD3D11BufferCreateInfoJUICE D3D11Buffer::GetJuiceInfo() const {
    VkD3D11BufferCreateInfoJUICE d3d11BufferCreateInfo;
    d3d11BufferCreateInfo.sType = VK_STRUCTURE_TYPE_D3D11_BUFFER_CREATE_INFO_JUICE;
    d3d11BufferCreateInfo.pNext = nullptr;
    d3d11BufferCreateInfo.bindFlags = VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_VERTEX_BUFFER) != 0) ? VK_D3D11_BIND_VERTEX_BUFFER_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_INDEX_BUFFER) != 0) ? VK_D3D11_BIND_INDEX_BUFFER_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_CONSTANT_BUFFER) != 0) ? VK_D3D11_BIND_CONSTANT_BUFFER_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_SHADER_RESOURCE) != 0) ? VK_D3D11_BIND_SHADER_RESOURCE_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_STREAM_OUTPUT) != 0) ? VK_D3D11_BIND_STREAM_OUTPUT_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_RENDER_TARGET) != 0) ? VK_D3D11_BIND_RENDER_TARGET_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) != 0) ? VK_D3D11_BIND_DEPTH_STENCIL_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.bindFlags |= ((m_desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0) ? VK_D3D11_BIND_UNORDERED_ACCESS_BIT_JUICE : VK_D3D11_BIND_NONE_JUICE;
    d3d11BufferCreateInfo.usage = (m_desc.Usage == D3D11_USAGE_DEFAULT)   ? VK_D3D11_USAGE_DEFAULT_JUICE   :
                                  (m_desc.Usage == D3D11_USAGE_IMMUTABLE) ? VK_D3D11_USAGE_IMMUTABLE_JUICE :
                                  (m_desc.Usage == D3D11_USAGE_DYNAMIC)   ? VK_D3D11_USAGE_DYNAMIC_JUICE   :
                                  (m_desc.Usage == D3D11_USAGE_STAGING)   ? VK_D3D11_USAGE_STAGING_JUICE   : VK_D3D11_USAGE_DEFAULT_JUICE;
    d3d11BufferCreateInfo.cpuAccessFlags = VK_D3D11_CPU_ACCESS_NONE_JUICE;
    d3d11BufferCreateInfo.cpuAccessFlags |= ((m_desc.CPUAccessFlags & D3D11_CPU_ACCESS_WRITE) != 0) ? VK_D3D11_CPU_ACCESS_WRITE_BIT_JUICE : VK_D3D11_CPU_ACCESS_NONE_JUICE;
    d3d11BufferCreateInfo.cpuAccessFlags |= ((m_desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) != 0) ? VK_D3D11_CPU_ACCESS_READ_BIT_JUICE : VK_D3D11_CPU_ACCESS_NONE_JUICE;
    return d3d11BufferCreateInfo;
  }


  Rc<DxvkBuffer> D3D11Buffer::CreateSoCounterBuffer() {
    Rc<DxvkDevice> device = m_parent->GetDXVKDevice();

    VkDxvkBufferCreateInfoJUICE dxvkBufferCreateInfo;
    dxvkBufferCreateInfo.sType = VK_STRUCTURE_TYPE_DXVK_BUFFER_CREATE_INFO_JUICE;
    dxvkBufferCreateInfo.pNext = nullptr;
    dxvkBufferCreateInfo.type = VK_DXVK_TYPE_D3D11_SO_COUNTER_BUFFER_JUICE;

    DxvkBufferCreateInfo info;
    info.size   = sizeof(D3D11SOCounter);
    info.usage  = VK_BUFFER_USAGE_TRANSFER_DST_BIT
                | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
    info.stages = VK_PIPELINE_STAGE_TRANSFER_BIT
                | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
                | VK_PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT;
    info.access = VK_ACCESS_TRANSFER_READ_BIT
                | VK_ACCESS_TRANSFER_WRITE_BIT
                | VK_ACCESS_INDIRECT_COMMAND_READ_BIT
                | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT
                | VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT;
    return device->createBuffer(info, &dxvkBufferCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  }


  D3D11_COMMON_BUFFER_MAP_MODE D3D11Buffer::DetermineMapMode() {
    return (m_buffer->memFlags() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
      ? D3D11_COMMON_BUFFER_MAP_MODE_DIRECT
      : D3D11_COMMON_BUFFER_MAP_MODE_NONE;
  }
  

  D3D11Buffer* GetCommonBuffer(ID3D11Resource* pResource) {
    D3D11_RESOURCE_DIMENSION dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    pResource->GetType(&dimension);

    return dimension == D3D11_RESOURCE_DIMENSION_BUFFER
      ? static_cast<D3D11Buffer*>(pResource)
      : nullptr;
  }

}
