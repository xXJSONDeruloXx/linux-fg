#pragma once
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <X11/Xlib.h>
#include <sys/shm.h>
#include "logger.hpp"
#include "frame_manager.hpp"

class WindowCapture {
public:
    static WindowCapture& Get() {
        static WindowCapture instance;
        return instance;
    }

    bool Initialize(uint32_t windowId);
    void Cleanup();

    bool CaptureFrame(Frame& frame);
    bool GetWindowSize(uint32_t& width, uint32_t& height);

private:
    WindowCapture() = default;
    ~WindowCapture() { Cleanup(); }

    bool SetupX11Connection();
    bool SetupSharedMemory(uint32_t size);
    void CleanupSharedMemory();

    xcb_connection_t* m_connection = nullptr;
    xcb_window_t m_window = 0;
    xcb_window_t m_rootWindow = 0;
    int m_screen = 0;
    int16_t m_windowX = 0;
    int16_t m_windowY = 0;

    // Shared memory segment info
    xcb_shm_seg_t m_segment = 0;
    void* m_shmData = nullptr;
    int m_shmId = -1;
};
