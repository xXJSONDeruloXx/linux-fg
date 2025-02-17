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

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    #ifdef NDEBUG
        const bool m_enableValidationLayers = false;
    #else
        const bool m_enableValidationLayers = true;
    #endif
};
