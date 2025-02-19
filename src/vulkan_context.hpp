#pragma once
#include <xcb/xcb.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_xcb.h>
#include <vector>
#include <memory>
#include "logger.hpp"

class VulkanContext {
public:
    static VulkanContext& Get() {
        static VulkanContext instance;
        return instance;
    }

    bool Initialize();
    void Cleanup();

    VkDevice GetDevice() const { return m_device; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkQueue GetComputeQueue() const { return m_computeQueue; }
    uint32_t GetComputeQueueFamily() const { return m_computeQueueFamily; }

    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, 
                     VkMemoryPropertyFlags properties,
                     VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    bool CreateImage(uint32_t width, uint32_t height, VkFormat format,
                    VkImageUsageFlags usage, VkMemoryPropertyFlags properties,
                    VkImage& image, VkDeviceMemory& imageMemory);

    void DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory);
    void DestroyImage(VkImage image, VkDeviceMemory memory);

    // Add new getters
    VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
    VkQueue GetPresentQueue() const { return m_presentQueue; }
    VkImage GetSwapchainImage(uint32_t index) const { return m_swapchainImages[index]; }

    // Add new method
    bool RecreateSwapchain(uint32_t width, uint32_t height) {
        vkDeviceWaitIdle(m_device);
        CleanupSwapchain();
        return CreateSwapchain(width, height);
    }

private:
    VulkanContext() = default;
    ~VulkanContext() { Cleanup(); }

    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    bool CheckValidationLayerSupport();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    uint32_t m_computeQueueFamily = 0;

    // Add swapchain members
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_swapchainImageFormat;
    VkExtent2D m_swapchainExtent;
    
    // Add queue for presentation
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    uint32_t m_presentQueueFamily = 0;

    // Add required methods
    bool CreateSurface(SDL_Window* window);
    bool CreateSwapchain(uint32_t width, uint32_t height);
    void CleanupSwapchain();
    
    // Add helper methods
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat();
    VkPresentModeKHR ChooseSwapPresentMode();
    VkExtent2D ChooseSwapExtent(uint32_t width, uint32_t height);

    // Add synchronization primitives
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    size_t m_currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    #ifdef NDEBUG
        const bool m_enableValidationLayers = false;
    #else
        const bool m_enableValidationLayers = true;
    #endif

    std::vector<VkSurfaceFormatKHR> m_surfaceFormats;
    std::vector<VkPresentModeKHR> m_presentModes;
};

bool VulkanContext::CreateSurface(SDL_Window* window) {
    SDL_Vulkan_CreateSurface(window, m_instance, &m_surface);
    return m_surface != VK_NULL_HANDLE;
}

VkSurfaceFormatKHR VulkanContext::ChooseSwapSurfaceFormat() {
    // Choose SRGB if available
    for (const auto& format : m_surfaceFormats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB && 
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return m_surfaceFormats[0];
}

VkPresentModeKHR VulkanContext::ChooseSwapPresentMode() {
    // Prefer mailbox (triple buffering) if available
    for (const auto& mode : m_presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanContext::ChooseSwapExtent(uint32_t width, uint32_t height) {
    VkExtent2D extent = {width, height};
    
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);

    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    extent.width = std::clamp(extent.width, 
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height);

    return extent;
}

void VulkanContext::CleanupSwapchain() {
    for (auto imageView : m_swapchainImageViews) {
        vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapchainImageViews.clear();
    
    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanContext::CreateSwapchain(uint32_t width, uint32_t height) {
    // Query surface capabilities first
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities);
    
    // Query surface formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    m_surfaceFormats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, m_surfaceFormats.data());
    
    // Query present modes
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    m_presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, m_presentModes.data());
    
    // Create the swapchain
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = capabilities.minImageCount + 1;
    createInfo.imageFormat = ChooseSwapSurfaceFormat().format;
    createInfo.imageColorSpace = ChooseSwapSurfaceFormat().colorSpace;
    createInfo.imageExtent = ChooseSwapExtent(width, height);
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = ChooseSwapPresentMode();
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        return false;
    }

    // Get swapchain images and create image views
    uint32_t imageCount;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    m_swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = createInfo.imageFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainImageViews[i]) != VK_SUCCESS) {
            return false;
        }
    }

    // Need to create these in Initialize()
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        m_imageAvailableSemaphores.push_back(VK_NULL_HANDLE);
        m_renderFinishedSemaphores.push_back(VK_NULL_HANDLE);
        m_inFlightFences.push_back(VK_NULL_HANDLE);
    }

    return true;
}
