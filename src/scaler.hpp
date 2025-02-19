#pragma once
#include <memory>
#include <queue>
#include <fstream>
#include "frame_manager.hpp"
#include "window_capture.hpp"

struct ScalerConfig {
    uint32_t inputWidth = 0;
    uint32_t inputHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t targetFps = 60;
    bool enableInterpolation = true;
    float interpolationFactor = 0.5f;
};

struct ScalePushConstants {
    int32_t inputSize[2];
    int32_t outputSize[2];
};

class Scaler {
public:
    static Scaler& Get() {
        static Scaler instance;
        return instance;
    }

    bool Initialize(const ScalerConfig& config);
    void Cleanup();

    bool ProcessFrame();
    bool IsInitialized() const { return m_initialized; }

private:
    Scaler() = default;
    ~Scaler() { Cleanup(); }

    bool LoadShaders();
    bool CreateComputePipeline();
    bool CreateDescriptorPool();
    bool CreateFrameResources();
    bool CreateCommandPool();
    bool ScaleFrame(const Frame& input, Frame& output);

    ScalerConfig m_config;
    bool m_initialized = false;

    // Frame management
    Frame m_currentFrame;
    Frame m_previousFrame;
    Frame m_outputFrame;
    
    // Vulkan resources
    VkShaderModule m_scaleShader = VK_NULL_HANDLE;
    VkPipeline m_scalePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    std::queue<std::chrono::steady_clock::time_point> m_frameTimings;
    float m_currentFps = 0.0f;

    SDL_Window* m_window = nullptr;
};
