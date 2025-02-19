#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include <string>
#include <fstream>
#include "vulkan_context.hpp"

struct Frame {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

struct MotionPushConstants {
    int32_t imageSize[2];
    int32_t blockSize;
    float searchRadius;
};

struct InterpolatePushConstants {
    float interpolationFactor;
    int32_t imageSize[2];
};

class FrameManager {
public:
    static FrameManager& Get() {
        static FrameManager instance;
        return instance;
    }

    bool Initialize(uint32_t width, uint32_t height);
    void Cleanup();

    // Frame management
    bool CreateFrame(Frame& frame, uint32_t width, uint32_t height);
    void DestroyFrame(Frame& frame);
    bool CopyFrameData(const Frame& source, Frame& destination);
    
    // Frame interpolation
    bool InterpolateFrames(const Frame& previous, const Frame& current, 
                          Frame& output, float factor);

    // Buffer management
    bool CreateStagingBuffer(VkBuffer& buffer, VkDeviceMemory& memory, VkDeviceSize size);
    void DestroyStagingBuffer(VkBuffer buffer, VkDeviceMemory memory);

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    FrameManager() = default;
    ~FrameManager() { Cleanup(); }

    // Command pool
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    bool CreateCommandPool();

    // Motion estimation resources
    VkShaderModule m_motionShader = VK_NULL_HANDLE;
    VkPipeline m_motionPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_motionPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_motionDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_motionDescriptorSet = VK_NULL_HANDLE;

    // Frame interpolation resources
    VkShaderModule m_interpolateShader = VK_NULL_HANDLE;
    VkPipeline m_interpolatePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_interpolatePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_interpolateDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_interpolateDescriptorSet = VK_NULL_HANDLE;

    // Shared resources
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // Pipeline creation
    bool CreateMotionPipeline();
    bool CreateInterpolatePipeline();
    bool CreateDescriptorSets();

    // Shader loading
    bool LoadShaderFile(const std::string& filename, std::vector<char>& buffer);

    FrameManager(const FrameManager&) = delete;
    FrameManager& operator=(const FrameManager&) = delete;
    FrameManager(FrameManager&&) = delete;
    FrameManager& operator=(FrameManager&&) = delete;
};