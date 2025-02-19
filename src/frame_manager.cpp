#include "frame_manager.hpp"

bool FrameManager::Initialize(uint32_t width, uint32_t height) {
    if (!CreateCommandPool()) {
        LOG_ERROR("Failed to create command pool");
        return false;
    }

    LOG_INFO("FrameManager initialized successfully");
    return true;
}

bool FrameManager::LoadShaderFile(const std::string& filename, std::vector<char>& buffer) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open shader file: ", filename);
        return false;
    }

    size_t fileSize = (size_t)file.tellg();
    buffer.resize(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return true;
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
    if (!m_motionPipeline || !m_interpolatePipeline) {
        if (!CreateMotionPipeline() || !CreateInterpolatePipeline() || !CreateDescriptorSets()) {
            LOG_ERROR("Failed to create interpolation pipelines");
            return false;
        }
    }

    // Create motion vectors frame
    Frame motionVectors;
    if (!CreateFrame(motionVectors, current.width, current.height)) {
        LOG_ERROR("Failed to create motion vectors frame");
        return false;
    }

    // Motion estimation
    VkDescriptorImageInfo prevInfo{};
    prevInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    prevInfo.imageView = previous.view;
    prevInfo.sampler = m_sampler;

    VkDescriptorImageInfo currInfo{};
    currInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    currInfo.imageView = current.view;
    currInfo.sampler = m_sampler;

    VkDescriptorImageInfo motionInfo{};
    motionInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    motionInfo.imageView = motionVectors.view;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.imageView = output.view;

    // Create descriptor writes for motion estimation
    std::vector<VkWriteDescriptorSet> motionDescriptorWrites = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_motionDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &prevInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_motionDescriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &currInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_motionDescriptorSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &motionInfo
        }
    };

    // Create descriptor writes for interpolation
    std::vector<VkWriteDescriptorSet> interpolateDescriptorWrites = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_interpolateDescriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &prevInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_interpolateDescriptorSet,
            .dstBinding = 1,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &currInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_interpolateDescriptorSet,
            .dstBinding = 2,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &motionInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_interpolateDescriptorSet,
            .dstBinding = 3,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &outputInfo
        }
    };

    auto& vulkan = VulkanContext::Get();
    
    // Update motion descriptor sets
    vkUpdateDescriptorSets(vulkan.GetDevice(), 
        static_cast<uint32_t>(motionDescriptorWrites.size()), 
        motionDescriptorWrites.data(), 0, nullptr);

    // Execute motion estimation
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    struct MotionPushConstants {
        int32_t imageSize[2];
        int32_t blockSize;
        float searchRadius;
    } motionConstants{
        .imageSize = {static_cast<int32_t>(current.width), 
                     static_cast<int32_t>(current.height)},
        .blockSize = 8,
        .searchRadius = 16.0f
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_motionPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_motionPipelineLayout, 0, 1, &m_motionDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, m_motionPipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(motionConstants), &motionConstants);

    uint32_t groupsX = (current.width + 15) / 16;
    uint32_t groupsY = (current.height + 15) / 16;
    vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

    // Update interpolation descriptor sets
    vkUpdateDescriptorSets(vulkan.GetDevice(),
        static_cast<uint32_t>(interpolateDescriptorWrites.size()),
        interpolateDescriptorWrites.data(), 0, nullptr);

    struct InterpolatePushConstants {
        float interpolationFactor;
        int32_t imageSize[2];
    } interpolateConstants{
        .interpolationFactor = factor,
        .imageSize = {static_cast<int32_t>(current.width),
                     static_cast<int32_t>(current.height)}
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_interpolatePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_interpolatePipelineLayout, 0, 1, &m_interpolateDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, m_interpolatePipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(interpolateConstants), &interpolateConstants);

    vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

    EndSingleTimeCommands(commandBuffer);
    DestroyFrame(motionVectors);

    return true;
}

bool FrameManager::CreateMotionPipeline() {
    auto& vulkan = VulkanContext::Get();
    
    // Load motion shader
    std::vector<char> shaderCode;
    if (!LoadShaderFile("shaders/motion.comp.spv", shaderCode)) {
        return false;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    if (vkCreateShaderModule(vulkan.GetDevice(), &createInfo, nullptr, &m_motionShader) != VK_SUCCESS) {
        return false;
    }

    // Create descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkan.GetDevice(), &layoutInfo, nullptr, 
                                  &m_motionDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    // Create pipeline layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(MotionPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_motionDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkan.GetDevice(), &pipelineLayoutInfo, nullptr,
                              &m_motionPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = m_motionShader;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_motionPipelineLayout;

    if (vkCreateComputePipelines(vulkan.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &m_motionPipeline) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool FrameManager::CreateInterpolatePipeline() {
    auto& vulkan = VulkanContext::Get();
    
    // Load interpolate shader
    std::vector<char> shaderCode;
    if (!LoadShaderFile("shaders/interpolate.comp.spv", shaderCode)) {
        return false;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shaderCode.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data());

    if (vkCreateShaderModule(vulkan.GetDevice(), &createInfo, nullptr, &m_interpolateShader) != VK_SUCCESS) {
        return false;
    }

    // Create descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 2,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        },
        {
            .binding = 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(vulkan.GetDevice(), &layoutInfo, nullptr,
                                  &m_interpolateDescriptorSetLayout) != VK_SUCCESS) {
        return false;
    }

    // Create pipeline layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(InterpolatePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_interpolateDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(vulkan.GetDevice(), &pipelineLayoutInfo, nullptr,
                              &m_interpolatePipelineLayout) != VK_SUCCESS) {
        return false;
    }

    // Create compute pipeline
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = m_interpolateShader;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_interpolatePipelineLayout;

    if (vkCreateComputePipelines(vulkan.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &m_interpolatePipeline) != VK_SUCCESS) {
        return false;
    }

    return true;
}

bool FrameManager::CreateDescriptorSets() {
    auto& vulkan = VulkanContext::Get();

    // make sampler if not exists
    if (m_sampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(vulkan.GetDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
            return false;
        }
    }

    // make descriptor pool
    std::vector<VkDescriptorPoolSize> poolSizes = {
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 6  // 2 for motion + 3 for interpolate
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 2  // 1 for motion + 1 for interpolate
        }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;  // One for motion, one for interpolate

    if (vkCreateDescriptorPool(vulkan.GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        return false;
    }

    std::vector<VkDescriptorSetLayout> layouts = {
        m_motionDescriptorSetLayout,
        m_interpolateDescriptorSetLayout
    };

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    std::vector<VkDescriptorSet> sets(2);
    if (vkAllocateDescriptorSets(vulkan.GetDevice(), &allocInfo, sets.data()) != VK_SUCCESS) {
        return false;
    }

    m_motionDescriptorSet = sets[0];
    m_interpolateDescriptorSet = sets[1];

    return true;
}

void FrameManager::Cleanup() {
    auto& vulkan = VulkanContext::Get();
    auto device = vulkan.GetDevice();

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    if (m_motionPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_motionPipeline, nullptr);
        m_motionPipeline = VK_NULL_HANDLE;
    }

    if (m_interpolatePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_interpolatePipeline, nullptr);
        m_interpolatePipeline = VK_NULL_HANDLE;
    }

    if (m_motionPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_motionPipelineLayout, nullptr);
        m_motionPipelineLayout = VK_NULL_HANDLE;
    }

    if (m_interpolatePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_interpolatePipelineLayout, nullptr);
        m_interpolatePipelineLayout = VK_NULL_HANDLE;
    }

    if (m_motionDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_motionDescriptorSetLayout, nullptr);
        m_motionDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_interpolateDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_interpolateDescriptorSetLayout, nullptr);
        m_interpolateDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_motionShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_motionShader, nullptr);
        m_motionShader = VK_NULL_HANDLE;
    }

    if (m_interpolateShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_interpolateShader, nullptr);
        m_interpolateShader = VK_NULL_HANDLE;
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}