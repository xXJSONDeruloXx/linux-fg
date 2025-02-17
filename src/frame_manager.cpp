#include "frame_manager.hpp"

bool FrameManager::Initialize(uint32_t width, uint32_t height) {
    if (!CreateCommandPool()) {
        LOG_ERROR("Failed to create command pool");
        return false;
    }

    LOG_INFO("FrameManager initialized successfully");
    return true;
}

void FrameManager::Cleanup() {
    auto& vulkan = VulkanContext::Get();
    
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(vulkan.GetDevice(), m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

bool FrameManager::CreateFrame(Frame& frame, uint32_t width, uint32_t height) {
    auto& vulkan = VulkanContext::Get();

    frame.width = width;
    frame.height = height;

    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_STORAGE_BIT |
                             VK_IMAGE_USAGE_SAMPLED_BIT;

    if (!vulkan.CreateImage(width, height, frame.format, usage,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           frame.image, frame.memory)) {
        LOG_ERROR("Failed to create frame image");
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = frame.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = frame.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(vulkan.GetDevice(), &viewInfo, nullptr, &frame.view) != VK_SUCCESS) {
        LOG_ERROR("Failed to create frame image view");
        DestroyFrame(frame);
        return false;
    }

    return true;
}

void FrameManager::DestroyFrame(Frame& frame) {
    auto& vulkan = VulkanContext::Get();
    
    if (frame.view != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan.GetDevice(), frame.view, nullptr);
        frame.view = VK_NULL_HANDLE;
    }

    if (frame.image != VK_NULL_HANDLE) {
        vulkan.DestroyImage(frame.image, frame.memory);
        frame.image = VK_NULL_HANDLE;
        frame.memory = VK_NULL_HANDLE;
    }
}

bool FrameManager::CopyFrameData(const Frame& source, Frame& destination) {
    if (source.width != destination.width || source.height != destination.height) {
        LOG_ERROR("Frame dimensions don't match for copy operation");
        return false;
    }

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = source.image;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = destination.image;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    VkImageCopy copyRegion{};
    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.extent.width = source.width;
    copyRegion.extent.height = source.height;
    copyRegion.extent.depth = 1;

    vkCmdCopyImage(commandBuffer,
        source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destination.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &copyRegion);

    EndSingleTimeCommands(commandBuffer);
    return true;
}

bool FrameManager::CreateCommandPool() {
    auto& vulkan = VulkanContext::Get();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = vulkan.GetComputeQueueFamily();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(vulkan.GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create command pool");
        return false;
    }

    return true;
}

VkCommandBuffer FrameManager::BeginSingleTimeCommands() {
    auto& vulkan = VulkanContext::Get();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(vulkan.GetDevice(), &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void FrameManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    auto& vulkan = VulkanContext::Get();

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(vulkan.GetComputeQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(vulkan.GetComputeQueue());

    vkFreeCommandBuffers(vulkan.GetDevice(), m_commandPool, 1, &commandBuffer);
}

bool FrameManager::CreateStagingBuffer(VkBuffer& buffer, VkDeviceMemory& memory, VkDeviceSize size) {
    auto& vulkan = VulkanContext::Get();

    return vulkan.CreateBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        buffer,
        memory
    );
}

void FrameManager::DestroyStagingBuffer(VkBuffer buffer, VkDeviceMemory memory) {
    auto& vulkan = VulkanContext::Get();
    vulkan.DestroyBuffer(buffer, memory);
}

bool FrameManager::InterpolateFrames(const Frame& previous, const Frame& current, 
                                   Frame& output, float factor) {
    // TODO: Implement frame interpolation using compute shader
    // we will be implement this with motion estimation and frame blending i guess
    LOG_WARN("Frame interpolation not yet implemented");
    return CopyFrameData(current, output);
}
