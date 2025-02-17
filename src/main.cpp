#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include "scaler.hpp"
#include "logger.hpp"

void PrintUsage() {
    std::cout << "Usage: lossless-scaling [options] window-id\n"
              << "Options:\n"
              << "  --help                   Show this help message\n"
              << "  --input-width WIDTH      Input width (default: auto-detect)\n"
              << "  --input-height HEIGHT    Input height (default: auto-detect)\n"
              << "  --output-width WIDTH     Output width\n"
              << "  --output-height HEIGHT   Output height\n"
              << "  --target-fps FPS         Target FPS (default: 60)\n"
              << "  --no-interpolation       Disable frame interpolation\n"
              << "  --interpolation-factor F Interpolation blend factor (0.0-1.0, default: 0.5)\n";
}

int main(int argc, char* argv[]) {
    uint32_t windowId = 0;
    ScalerConfig config;
    config.enableInterpolation = true;
    config.interpolationFactor = 0.5f;
    config.targetFps = 60;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            PrintUsage();
            return 0;
        } else if (strcmp(argv[i], "--input-width") == 0 && i + 1 < argc) {
            config.inputWidth = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--input-height") == 0 && i + 1 < argc) {
            config.inputHeight = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output-width") == 0 && i + 1 < argc) {
            config.outputWidth = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--output-height") == 0 && i + 1 < argc) {
            config.outputHeight = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--target-fps") == 0 && i + 1 < argc) {
            config.targetFps = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-interpolation") == 0) {
            config.enableInterpolation = false;
        } else if (strcmp(argv[i], "--interpolation-factor") == 0 && i + 1 < argc) {
            config.interpolationFactor = std::atof(argv[++i]);
        } else if (windowId == 0) {
            char* endPtr;
            windowId = std::strtoul(argv[i], &endPtr, 0);
            if (*endPtr != '\0') {
                LOG_ERROR("Invalid window ID");
                return 1;
            }
        }
    }

    if (windowId == 0) {
        LOG_ERROR("No window ID specified");
        PrintUsage();
        return 1;
    }

    if (!WindowCapture::Get().Initialize(windowId)) {
        LOG_ERROR("Failed to initialize window capture");
        return 1;
    }

    if (config.inputWidth == 0 || config.inputHeight == 0) {
        if (!WindowCapture::Get().GetWindowSize(config.inputWidth, config.inputHeight)) {
            LOG_ERROR("Failed to get window size");
            WindowCapture::Get().Cleanup();
            return 1;
        }
        LOG_INFO("Auto-detected input size: ", config.inputWidth, "x", config.inputHeight);
    }

    if (config.outputWidth == 0 || config.outputHeight == 0) {
        config.outputWidth = config.inputWidth;
        config.outputHeight = config.inputHeight;
    }

    if (!VulkanContext::Get().Initialize()) {
        LOG_ERROR("Failed to initialize Vulkan");
        WindowCapture::Get().Cleanup();
        return 1;
    }

    if (!FrameManager::Get().Initialize(config.outputWidth, config.outputHeight)) {
        LOG_ERROR("Failed to initialize frame manager");
        VulkanContext::Get().Cleanup();
        WindowCapture::Get().Cleanup();
        return 1;
    }

    if (!Scaler::Get().Initialize(config)) {
        LOG_ERROR("Failed to initialize scaler");
        FrameManager::Get().Cleanup();
        VulkanContext::Get().Cleanup();
        WindowCapture::Get().Cleanup();
        return 1;
    }

    LOG_INFO("Starting main loop");
    const auto frameTime = std::chrono::microseconds(1000000 / config.targetFps);
    auto nextFrame = std::chrono::steady_clock::now();

    try {
        while (true) {
            auto now = std::chrono::steady_clock::now();
            if (now < nextFrame) {
                std::this_thread::sleep_for(nextFrame - now);
            }
            nextFrame = now + frameTime;

            if (!Scaler::Get().ProcessFrame()) {
                LOG_ERROR("Failed to process frame");
                break;
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Exception caught: ", e.what());
    }

    Scaler::Get().Cleanup();
    FrameManager::Get().Cleanup();
    VulkanContext::Get().Cleanup();
    WindowCapture::Get().Cleanup();

    return 0;
}
