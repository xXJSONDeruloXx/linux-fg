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
    if (!m_initialized) {
        LOG_ERROR("Scaler not initialized");
        return false;
    }

    // Handle SDL events properly
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                LOG_INFO("Received SDL_QUIT event");
                return false;
                
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    LOG_INFO("Received window close event");
                    return false;
                }
                break;
        }
    }

    static int frameCount = 0;
    if (frameCount++ % 60 == 0) {  // Log every 60 frames
        LOG_INFO("Current FPS: ", m_currentFps);
        LOG_INFO("Input Resolution: ", m_config.inputWidth, "x", m_config.inputHeight);
        LOG_INFO("Target Resolution: ", m_config.outputWidth, "x", m_config.outputHeight);
        LOG_INFO("Interpolation: ", m_config.enableInterpolation ? "Enabled" : "Disabled");
    }

    auto currentTime = std::chrono::steady_clock::now();
    m_frameTimings.push(currentTime);

    while (m_frameTimings.size() > 60) {
        m_frameTimings.pop();
    }

    if (m_frameTimings.size() > 1) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_frameTimings.back() - m_frameTimings.front()).count();
        m_currentFps = 1000.0f * (m_frameTimings.size() - 1) / duration;
    }

    // Update parent window FPS
    m_parentFrameTimings.push(currentTime);

    while (m_parentFrameTimings.size() > 60) {
        m_parentFrameTimings.pop();
    }

    if (m_parentFrameTimings.size() > 1) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_parentFrameTimings.back() - m_parentFrameTimings.front()).count();
        m_parentFps = 1000.0f * (m_parentFrameTimings.size() - 1) / duration;
    }

    if (m_currentFrame.image == VK_NULL_HANDLE) {
        LOG_INFO("Creating current frame buffer");
        if (!FrameManager::Get().CreateFrame(m_currentFrame, m_config.inputWidth, m_config.inputHeight)) {
            LOG_ERROR("Failed to create current frame");
            return false;
        }
    }

    if (m_config.enableInterpolation && m_previousFrame.image == VK_NULL_HANDLE) {
        LOG_INFO("Creating previous frame buffer");
        if (!FrameManager::Get().CreateFrame(m_previousFrame, m_config.inputWidth, m_config.inputHeight)) {
            LOG_ERROR("Failed to create previous frame");
            return false;
        }
    }

    if (m_outputFrame.image == VK_NULL_HANDLE) {
        LOG_INFO("Creating output frame buffer");
        if (!FrameManager::Get().CreateFrame(m_outputFrame, m_config.outputWidth, m_config.outputHeight)) {
            LOG_ERROR("Failed to create output frame");
            return false;
        }
    }

    LOG_INFO("Attempting to capture frame...");
    if (!WindowCapture::Get().CaptureFrame(m_currentFrame)) {
        LOG_ERROR("Failed to capture frame");
        return false;
    }
    LOG_INFO("Frame captured successfully");

    LOG_INFO("Scaling frame...");
    if (!ScaleFrame(m_currentFrame, m_outputFrame)) {
        LOG_ERROR("Failed to scale frame");
        return false;
    }
    LOG_INFO("Frame scaled successfully");

    // Create staging buffer for reading back the image data
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize bufferSize = m_config.outputWidth * m_config.outputHeight * 4;

    if (!FrameManager::Get().CreateStagingBuffer(stagingBuffer, stagingMemory, bufferSize)) {
        LOG_ERROR("Failed to create staging buffer");
        return false;
    }

    // Copy image data to staging buffer
    VkCommandBuffer commandBuffer = FrameManager::Get().BeginSingleTimeCommands();
    
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_outputFrame.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = m_config.outputWidth;
    region.imageExtent.height = m_config.outputHeight;
    region.imageExtent.depth = 1;

    vkCmdCopyImageToBuffer(commandBuffer,
        m_outputFrame.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        1,
        &region);

    FrameManager::Get().EndSingleTimeCommands(commandBuffer);

    // Ensure compute operations are complete
    vkQueueWaitIdle(VulkanContext::Get().GetComputeQueue());

    // Map the staging buffer and create SDL surface
    void* data;
    vkMapMemory(VulkanContext::Get().GetDevice(), stagingMemory, 0, bufferSize, 0, &data);

    SDL_Surface* surface = SDL_CreateRGBSurfaceFrom(
        data,
        m_config.outputWidth,
        m_config.outputHeight,
        32,
        m_config.outputWidth * 4,
        0x00FF0000,  // B mask
        0x0000FF00,  // G mask
        0x000000FF,  // R mask
        0xFF000000   // A mask
    );

    if (!surface) {
        LOG_ERROR("Failed to create SDL surface: ", SDL_GetError());
        vkUnmapMemory(VulkanContext::Get().GetDevice(), stagingMemory);
        FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);
        return false;
    }

    // Get the window surface
    SDL_Surface* windowSurface = SDL_GetWindowSurface(m_window);
    if (!windowSurface) {
        LOG_ERROR("Failed to get window surface: ", SDL_GetError());
        SDL_FreeSurface(surface);
        vkUnmapMemory(VulkanContext::Get().GetDevice(), stagingMemory);
        FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);
        return false;
    }

    if (SDL_MUSTLOCK(windowSurface)) {
        if (SDL_LockSurface(windowSurface) < 0) {
            LOG_ERROR("Failed to lock window surface: ", SDL_GetError());
            return false;
        }
    }

    // Blit the surface to the window surface
    if (SDL_BlitSurface(surface, NULL, windowSurface, NULL) != 0) {
        LOG_ERROR("Failed to blit surface: ", SDL_GetError());
        SDL_FreeSurface(surface);
        vkUnmapMemory(VulkanContext::Get().GetDevice(), stagingMemory);
        FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);
        return false;
    }

    // Prepare stats text
    std::stringstream stats;
    stats << std::fixed << std::setprecision(1)
          << "Parent FPS: " << m_parentFps << "\n"
          << "Output FPS: " << m_currentFps << "\n"
          << "Input: " << m_config.inputWidth << "x" << m_config.inputHeight << "\n"
          << "Output: " << m_config.outputWidth << "x" << m_config.outputHeight;

    // Render stats text
    if (m_statsSurface) {
        SDL_FreeSurface(m_statsSurface);
    }
    m_statsSurface = TTF_RenderText_Blended_Wrapped(m_font, stats.str().c_str(), 
                                                   m_textColor, m_config.outputWidth);

    if (m_statsSurface) {
        SDL_Rect dstRect = {10, 10, m_statsSurface->w, m_statsSurface->h};
        SDL_BlitSurface(m_statsSurface, NULL, windowSurface, &dstRect);
    }

    if (SDL_MUSTLOCK(windowSurface)) {
        SDL_UnlockSurface(windowSurface);
    }

    // Update the window with the new content
    if (SDL_UpdateWindowSurface(m_window) != 0) {
        LOG_ERROR("Failed to update window surface: ", SDL_GetError());
    }

    SDL_FreeSurface(surface);

    vkUnmapMemory(VulkanContext::Get().GetDevice(), stagingMemory);
    FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);

    if (m_config.enableInterpolation) {
        if (!FrameManager::Get().CopyFrameData(m_currentFrame, m_previousFrame)) {
            LOG_ERROR("Failed to store frame for interpolation");
            return false;
        }
    }

    return true;
}

void Scaler::Cleanup() {
    // Get Vulkan context once
    auto& vulkan = VulkanContext::Get();
    if (vulkan.GetDevice()) {
        vkDeviceWaitIdle(vulkan.GetDevice());
    }

    // Cleanup TTF/SDL resources
    if (m_statsSurface) {
        SDL_FreeSurface(m_statsSurface);
        m_statsSurface = nullptr;
    }

    if (m_font) {
        TTF_CloseFont(m_font);
        m_font = nullptr;
    }

    TTF_Quit();

    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
    
    // Cleanup Vulkan resources
    auto device = vulkan.GetDevice(); // Use the same vulkan reference

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
