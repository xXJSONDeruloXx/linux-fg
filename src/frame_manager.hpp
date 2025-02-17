#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include <vector>
#include "vulkan_context.hpp"

struct Frame {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

class FrameManager {
public:
    static FrameManager& Get() {
        static FrameManager instance;
        return instance;
    }

    bool Initialize(uint32_t width, uint32_t height);
    void Cleanup();

    bool CreateFrame(Frame& frame, uint32_t width, uint32_t height);
    void DestroyFrame(Frame& frame);
    
    bool CopyFrameData(const Frame& source, Frame& destination);
    bool InterpolateFrames(const Frame& previous, const Frame& current, 
                          Frame& output, float factor);

    bool CreateStagingBuffer(VkBuffer& buffer, VkDeviceMemory& memory, VkDeviceSize size);
    void DestroyStagingBuffer(VkBuffer buffer, VkDeviceMemory memory);

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    FrameManager() = default;
    ~FrameManager() { Cleanup(); }

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    bool CreateCommandPool();
};
