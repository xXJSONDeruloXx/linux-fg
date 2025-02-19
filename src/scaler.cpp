#include "scaler.hpp"
#include <SDL2/SDL_ttf.h>

bool Scaler::Initialize(const ScalerConfig& config) {
    m_config = config;
    
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        LOG_ERROR("SDL initialization failed: ", SDL_GetError());
        return false;
    }

    // After SDL_Init
    if (TTF_Init() < 0) {
        LOG_ERROR("TTF initialization failed: ", TTF_GetError());
        return false;
    }

    // Load a built-in font (modify path as needed)
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf"
    };

    bool fontLoaded = false;
    for (const auto& path : fontPaths) {
        m_font = TTF_OpenFont(path, 14);
        if (m_font) {
            fontLoaded = true;
            break;
        }
    }

    if (!fontLoaded) {
        LOG_ERROR("Failed to load any font");
        return false;
    }

    // Create output window
    m_window = SDL_CreateWindow(
        "Scaled Output",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        m_config.outputWidth, m_config.outputHeight,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI  // Add HIGHDPI support
    );

    if (!m_window) {
        LOG_ERROR("Failed to create SDL window: ", SDL_GetError());
        return false;
    }

    if (!CreateCommandPool()) {
        LOG_ERROR("Failed to create command pool");
        return false;
    }

    // After creating command pool
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    auto& vulkan = VulkanContext::Get();
    if (vkCreateSemaphore(vulkan.GetDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(vulkan.GetDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(vulkan.GetDevice(), &fenceInfo, nullptr, &m_inFlightFence) != VK_SUCCESS) {
        return false;
    }

    if (!LoadShaders()) {
        LOG_ERROR("Failed to load shaders");
        return false;
    }

    if (!CreateComputePipeline()) {
        LOG_ERROR("Failed to create compute pipeline");
        return false;
    }

    if (!CreateDescriptorPool()) {
        LOG_ERROR("Failed to create descriptor pool");
        return false;
    }

    if (!CreateFrameResources()) {
        LOG_ERROR("Failed to create frame resources");
        return false;
    }

    if (!vulkan.CreateSurface(m_window)) {
        LOG_ERROR("Failed to create surface");
        return false;
    }
    
    if (!vulkan.CreateSwapchain(config.outputWidth, config.outputHeight)) {
        LOG_ERROR("Failed to create swapchain");
        return false;
    }

    m_initialized = true;
    LOG_INFO("Scaler initialized successfully");
    return true;
}

bool Scaler::CreateCommandPool() {
    VulkanContext& vulkan = VulkanContext::Get();
    
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

bool Scaler::LoadShaders() {
    VulkanContext& vulkan = VulkanContext::Get();
    
    std::ifstream file("shaders/scale.comp.spv", std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open scale.comp.spv");
        return false;
    }

    size_t fileSize = (size_t) file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = buffer.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(buffer.data());

    if (vkCreateShaderModule(vulkan.GetDevice(), &createInfo, nullptr, &m_scaleShader) != VK_SUCCESS) {
        LOG_ERROR("Failed to create scale shader module");
        return false;
    }

    return true;
}

bool Scaler::CreateComputePipeline() {
    VulkanContext& vulkan = VulkanContext::Get();
    auto device = vulkan.GetDevice();

    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor set layout");
        return false;
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ScalePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        LOG_ERROR("Failed to create pipeline layout");
        return false;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = m_scaleShader;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = m_pipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_scalePipeline) != VK_SUCCESS) {
        LOG_ERROR("Failed to create compute pipeline");
        return false;
    }

    return true;
}

bool Scaler::CreateDescriptorPool() {
    VulkanContext& vulkan = VulkanContext::Get();
    auto device = vulkan.GetDevice();

    std::vector<VkDescriptorPoolSize> poolSizes = {
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
        },
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
        }
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        LOG_ERROR("Failed to create descriptor pool");
        return false;
    }

    return true;
}

bool Scaler::CreateFrameResources() {
    VulkanContext& vulkan = VulkanContext::Get();
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    if (vkCreateSampler(vulkan.GetDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        LOG_ERROR("Failed to create sampler");
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (vkAllocateDescriptorSets(vulkan.GetDevice(), &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate descriptor set");
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(vulkan.GetDevice(), &cmdAllocInfo, &m_commandBuffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate command buffer");
        return false;
    }

    return true;
}

bool Scaler::ScaleFrame(const Frame& input, Frame& output) {
    // Add debug logging
    LOG_INFO("ScaleFrame - Input: ", input.width, "x", input.height,
             " Output: ", output.width, "x", output.height);

    VulkanContext& vulkan = VulkanContext::Get();
    
    VkDescriptorImageInfo inputInfo{};
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    inputInfo.imageView = input.view;
    inputInfo.sampler = m_sampler;

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outputInfo.imageView = output.view;

    std::vector<VkWriteDescriptorSet> descriptorWrites = {
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_descriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &inputInfo,
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_descriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &outputInfo,
        }
    };

    vkUpdateDescriptorSets(vulkan.GetDevice(), 
        static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(),
        0, nullptr);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = input.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(m_commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    barrier.image = output.image;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(m_commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_scalePipeline);
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    ScalePushConstants pushConstants{
        .inputSize = {static_cast<int32_t>(input.width), static_cast<int32_t>(input.height)},
        .outputSize = {static_cast<int32_t>(output.width), static_cast<int32_t>(output.height)}
    };

    vkCmdPushConstants(m_commandBuffer, m_pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

    uint32_t groupsX = (output.width + 15) / 16;
    uint32_t groupsY = (output.height + 15) / 16;

    // Log dispatch parameters
    LOG_INFO("Dispatch groups: ", groupsX, "x", groupsY);
    
    vkCmdDispatch(m_commandBuffer, groupsX, groupsY, 1);

    barrier.image = output.image;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(m_commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to record command buffer");
        return false;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    if (vkQueueSubmit(vulkan.GetComputeQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        LOG_ERROR("Failed to submit command buffer");
        return false;
    }

    vkQueueWaitIdle(vulkan.GetComputeQueue());
    return true;
}

bool Scaler::ProcessFrame() {
    auto& vulkan = VulkanContext::Get();
    
    vkWaitForFences(vulkan.GetDevice(), 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(vulkan.GetDevice(), 1, &m_inFlightFence);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(vulkan.GetDevice(), vulkan.GetSwapchain(), 
        UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Handle window resize
        return RecreateSwapchain();
    }

    // Submit work
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphore};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    
    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(vulkan.GetComputeQueue(), 1, &submitInfo, m_inFlightFence) != VK_SUCCESS) {
        return false;
    }

    // Add transition to PRESENT_SRC_KHR
    VkImageMemoryBarrier barrier{};
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // ... setup barrier ...
    
    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vulkan.GetSwapchain();
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(vulkan.GetPresentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        return RecreateSwapchain();
    }
    return result == VK_SUCCESS;
}

void Scaler::Cleanup() {
    auto& vulkan = VulkanContext::Get();
    auto device = vulkan.GetDevice();

    if (m_imageAvailableSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, m_imageAvailableSemaphore, nullptr);
        m_imageAvailableSemaphore = VK_NULL_HANDLE;
    }

    if (m_renderFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, m_renderFinishedSemaphore, nullptr);
        m_renderFinishedSemaphore = VK_NULL_HANDLE;
    }

    if (m_inFlightFence != VK_NULL_HANDLE) {
        vkDestroyFence(device, m_inFlightFence, nullptr);
        m_inFlightFence = VK_NULL_HANDLE;
    }

    if (m_commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device, m_commandPool, 1, &m_commandBuffer);
        m_commandBuffer = VK_NULL_HANDLE;
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    if (m_scalePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_scalePipeline, nullptr);
        m_scalePipeline = VK_NULL_HANDLE;
    }

    if (m_scaleShader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, m_scaleShader, nullptr);
        m_scaleShader = VK_NULL_HANDLE;
    }

    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    FrameManager::Get().DestroyFrame(m_currentFrame);
    FrameManager::Get().DestroyFrame(m_previousFrame);
    FrameManager::Get().DestroyFrame(m_outputFrame);

    m_initialized = false;
}
