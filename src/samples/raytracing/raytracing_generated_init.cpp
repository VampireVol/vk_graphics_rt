#include <vector>
#include <array>
#include <memory>
#include <limits>

#include <cassert>

#include "vulkan_basics.h"
#include "raytracing_generated.h"
#include "include/RayTracer_ubo.h"

static uint32_t ComputeReductionAuxBufferElements(uint32_t whole_size, uint32_t wg_size)
{
  uint32_t sizeTotal = 0;
  while (whole_size > 1)
  {
    whole_size  = (whole_size + wg_size - 1) / wg_size;
    sizeTotal  += std::max<uint32_t>(whole_size, 1);
  }
  return sizeTotal;
}

RayTracer_Generated::~RayTracer_Generated()
{
  m_pMaker = nullptr;

  vkDestroyDescriptorSetLayout(device, CastSingleRayMegaDSLayout, nullptr);
  CastSingleRayMegaDSLayout = VK_NULL_HANDLE;

  vkDestroyPipeline(device, CastSingleRayMegaPipeline, nullptr);
  vkDestroyPipelineLayout(device, CastSingleRayMegaLayout, nullptr);
  CastSingleRayMegaLayout   = VK_NULL_HANDLE;
  CastSingleRayMegaPipeline = VK_NULL_HANDLE;
  vkDestroyDescriptorSetLayout(device, copyKernelFloatDSLayout, nullptr);
  vkDestroyDescriptorPool(device, m_dsPool, NULL); m_dsPool = VK_NULL_HANDLE;

  vkDestroyBuffer(device, CastSingleRay_local.rayDirAndFarBuffer, nullptr);
  vkDestroyBuffer(device, CastSingleRay_local.rayPosAndNearBuffer, nullptr);

 
  vkDestroyBuffer(device, m_classDataBuffer, nullptr);


  FreeMemoryForMemberBuffersAndImages();
  FreeMemoryForInternalBuffers();
}

void RayTracer_Generated::InitHelpers()
{
  vkGetPhysicalDeviceProperties(physicalDevice, &m_devProps);
  m_pMaker = std::make_unique<vk_utils::ComputePipelineMaker>();
}

VkDescriptorSetLayout RayTracer_Generated::CreateCastSingleRayMegaDSLayout()
{
  std::array<VkDescriptorSetLayoutBinding, 2+1+6> dsBindings;

  // binding for out_color
  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  // binding for m_pAccelStruct
  dsBindings[1].binding            = 1;
  dsBindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  dsBindings[1].descriptorCount    = 1;
  dsBindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[1].pImmutableSamplers = nullptr;

  // binding for POD members stored in m_classDataBuffer
  dsBindings[2].binding            = 2;
  dsBindings[2].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[2].descriptorCount    = 1;
  dsBindings[2].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[2].pImmutableSamplers = nullptr;

  for (int i = 0; i < 6; i++) {
    dsBindings[i + 3].binding = i + 3;
    dsBindings[i + 3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    dsBindings[i + 3].descriptorCount = 1;
    dsBindings[i + 3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    dsBindings[i + 3].pImmutableSamplers = nullptr;
  }
  
  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = uint32_t(dsBindings.size());
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();
  
  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}

VkDescriptorSetLayout RayTracer_Generated::CreatecopyKernelFloatDSLayout()
{
  std::array<VkDescriptorSetLayoutBinding, 2> dsBindings;

  dsBindings[0].binding            = 0;
  dsBindings[0].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[0].descriptorCount    = 1;
  dsBindings[0].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[0].pImmutableSamplers = nullptr;

  dsBindings[1].binding            = 1;
  dsBindings[1].descriptorType     = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dsBindings[1].descriptorCount    = 1;
  dsBindings[1].stageFlags         = VK_SHADER_STAGE_COMPUTE_BIT;
  dsBindings[1].pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
  descriptorSetLayoutCreateInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCreateInfo.bindingCount = dsBindings.size();
  descriptorSetLayoutCreateInfo.pBindings    = dsBindings.data();

  VkDescriptorSetLayout layout = nullptr;
  VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, NULL, &layout));
  return layout;
}


void RayTracer_Generated::InitKernel_CastSingleRayMega(const char* a_filePath)
{
  std::string shaderPath = AlterShaderPath("shaders_generated/CastSingleRayMega.comp.spv"); 
  
  m_pMaker->LoadShader(device, shaderPath.c_str(), nullptr, "main");
  CastSingleRayMegaDSLayout = CreateCastSingleRayMegaDSLayout();
  CastSingleRayMegaLayout   = m_pMaker->MakeLayout(device, { CastSingleRayMegaDSLayout }, 128); // at least 128 bytes for push constants
  CastSingleRayMegaPipeline = m_pMaker->MakePipeline(device);  
}


void RayTracer_Generated::InitKernels(const char* a_filePath)
{
  InitKernel_CastSingleRayMega(a_filePath);
}

void RayTracer_Generated::InitBuffers(size_t a_maxThreadsCount, bool a_tempBuffersOverlay)
{
  m_maxThreadCount = a_maxThreadsCount;
  std::vector<VkBuffer> allBuffers;
  allBuffers.reserve(64);

  struct BufferReqPair
  {
    BufferReqPair() {  }
    BufferReqPair(VkBuffer a_buff, VkDevice a_dev) : buf(a_buff) { vkGetBufferMemoryRequirements(a_dev, a_buff, &req); }
    VkBuffer             buf = VK_NULL_HANDLE;
    VkMemoryRequirements req = {};
  };

  struct LocalBuffers
  {
    std::vector<BufferReqPair> bufs;
    size_t                     size = 0;
    std::vector<VkBuffer>      bufsClean;
  };

  std::vector<LocalBuffers> groups;
  groups.reserve(16);

  LocalBuffers localBuffersCastSingleRay;
  localBuffersCastSingleRay.bufs.reserve(32);
  CastSingleRay_local.rayDirAndFarBuffer = vk_utils::createBuffer(device, sizeof(LiteMath::float4)*a_maxThreadsCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  localBuffersCastSingleRay.bufs.push_back(BufferReqPair(CastSingleRay_local.rayDirAndFarBuffer, device));
  CastSingleRay_local.rayPosAndNearBuffer = vk_utils::createBuffer(device, sizeof(LiteMath::float4)*a_maxThreadsCount, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  localBuffersCastSingleRay.bufs.push_back(BufferReqPair(CastSingleRay_local.rayPosAndNearBuffer, device));
  for(const auto& pair : localBuffersCastSingleRay.bufs)
  {
    allBuffers.push_back(pair.buf);
    localBuffersCastSingleRay.size += pair.req.size;
  }
  groups.push_back(localBuffersCastSingleRay);


  size_t largestIndex = 0;
  size_t largestSize  = 0;
  for(size_t i=0;i<groups.size();i++)
  {
    if(groups[i].size > largestSize)
    {
      largestIndex = i;
      largestSize  = groups[i].size;
    }
    groups[i].bufsClean.resize(groups[i].bufs.size());
    for(size_t j=0;j<groups[i].bufsClean.size();j++)
      groups[i].bufsClean[j] = groups[i].bufs[j].buf;
  }

  auto& allBuffersRef = a_tempBuffersOverlay ? groups[largestIndex].bufsClean : allBuffers;

  m_classDataBuffer = vk_utils::createBuffer(device, sizeof(m_uboData),  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | GetAdditionalFlagsForUBO());
  allBuffersRef.push_back(m_classDataBuffer);
  
  AllocMemoryForInternalBuffers(allBuffersRef);

  if(a_tempBuffersOverlay)
  {
    for(size_t i=0;i<groups.size();i++)
      if(i != largestIndex)
        AssignBuffersToMemory(groups[i].bufsClean, m_allMem);
  }
}

void RayTracer_Generated::InitMemberBuffers()
{
  std::vector<VkBuffer> memberVectors;
  std::vector<VkImage>  memberTextures;



  AllocMemoryForMemberBuffersAndImages(memberVectors, memberTextures);
}




void RayTracer_Generated::AllocMemoryForInternalBuffers(const std::vector<VkBuffer>& a_buffers)
{
  if(a_buffers.size() > 0)
    m_allMem = vk_utils::allocateAndBindWithPadding(device, physicalDevice, a_buffers);
  else
    m_allMem = VK_NULL_HANDLE;
}

void RayTracer_Generated::AssignBuffersToMemory(const std::vector<VkBuffer>& a_buffers, VkDeviceMemory a_mem)
{
  if(a_buffers.size() == 0 || a_mem == VK_NULL_HANDLE)
    return;

  std::vector<VkMemoryRequirements> memInfos(a_buffers.size());
  for(size_t i=0;i<memInfos.size();i++)
  {
    if(a_buffers[i] != VK_NULL_HANDLE)
      vkGetBufferMemoryRequirements(device, a_buffers[i], &memInfos[i]);
    else
    {
      memInfos[i] = memInfos[0];
      memInfos[i].size = 0;
    }
  }
  
  for(size_t i=1;i<memInfos.size();i++)
  {
    if(memInfos[i].memoryTypeBits != memInfos[0].memoryTypeBits)
    {
      std::cout << "[RayTracer_Generated::AssignBuffersToMemory]: error, input buffers has different 'memReq.memoryTypeBits'" << std::endl;
      return;
    }
  }

  auto offsets = vk_utils::calculateMemOffsets(memInfos);
  for (size_t i = 0; i < memInfos.size(); i++)
  {
    if(a_buffers[i] != VK_NULL_HANDLE)
      vkBindBufferMemory(device, a_buffers[i], a_mem, offsets[i]);
  }
}

void RayTracer_Generated::AllocMemoryForMemberBuffersAndImages(const std::vector<VkBuffer>& a_buffers, const std::vector<VkImage>& a_images)
{
  if(a_buffers.size() > 0)
    m_vdata.m_vecMem = vk_utils::allocateAndBindWithPadding(device, physicalDevice, a_buffers);
  else
    m_vdata.m_vecMem = VK_NULL_HANDLE;
  
  m_vdata.m_texMem = VK_NULL_HANDLE;
}

void RayTracer_Generated::FreeMemoryForInternalBuffers()
{
  if(m_allMem != VK_NULL_HANDLE)
    vkFreeMemory(device, m_allMem, nullptr);
  m_allMem = VK_NULL_HANDLE;
}

void RayTracer_Generated::FreeMemoryForMemberBuffersAndImages()
{
  if(m_vdata.m_vecMem != VK_NULL_HANDLE)
    vkFreeMemory(device, m_vdata.m_vecMem, nullptr);
  m_vdata.m_vecMem = VK_NULL_HANDLE;
  if(m_vdata.m_texMem != VK_NULL_HANDLE)
    vkFreeMemory(device, m_vdata.m_texMem, nullptr);
  m_vdata.m_texMem = VK_NULL_HANDLE;
}

