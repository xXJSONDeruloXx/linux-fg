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

    VkQueue GetPresentQueue() const { return m_presentQueue; }
    VkSwapchainKHR GetSwapchain() const { return m_swapchain; }
    VkSurfaceKHR GetSurface() const { return m_surface; }

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

    VkQueue m_presentQueue = VK_NULL_HANDLE;
    uint32_t m_presentQueueFamily = 0;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    VkFormat m_swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;

    uint32_t m_width = 0;
    uint32_t m_height = 0;

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    #ifdef NDEBUG
        const bool m_enableValidationLayers = false;
    #else
        const bool m_enableValidationLayers = true;
    #endif
};
