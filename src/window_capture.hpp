#pragma once
#include <xcb/xcb.h>
#include <xcb/shm.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <wayland-client.h>
#include <sys/shm.h>
#include "logger.hpp"
#include "frame_manager.hpp"
#include <queue>
#include <chrono>
 
enum class DisplayServer {
    X11,
    XWAYLAND,
    WAYLAND
};
 
struct WaylandContext {
    struct wl_display* display = nullptr;
    struct wl_registry* registry = nullptr;
    struct wl_compositor* compositor = nullptr;
    struct wl_shm* shm = nullptr;
};
 
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
    float GetSourceFps() const { return m_sourceFps; }
 
private:
    WindowCapture() = default;
    ~WindowCapture() { Cleanup(); }
 
    // Display server detection and setup
    bool DetectDisplayServer();
    bool SetupX11Connection();
    bool SetupWaylandConnection();
    bool InitializeCompositing();
    bool RedirectWindow();
 
    // Window management
    bool GetWindowAttributes();
    bool GetTopLevelParent();
    bool TranslateCoordinates();
    bool UpdateWindowGeometry();
 
    // Capture methods
    bool CaptureX11Frame(Frame& frame);
    bool CaptureXCompositeFrame(Frame& frame);
    bool CaptureWaylandFrame(Frame& frame);
 
    // Memory management
    bool SetupSharedMemory(uint32_t size);
    void CleanupSharedMemory();
    bool CopyToStagingBuffer(const void* data, size_t size, Frame& frame);
 
    DisplayServer m_displayServer = DisplayServer::X11;
 
    // X11/XCB resources
    xcb_connection_t* m_connection = nullptr;
    xcb_window_t m_window = 0;
    xcb_window_t m_rootWindow = 0;
    xcb_window_t m_topLevelWindow = 0;
    int m_screen = 0;
 
    // Window geometry
    int16_t m_windowX = 0;
    int16_t m_windowY = 0;
    int16_t m_absoluteX = 0;
    int16_t m_absoluteY = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
 
    // Compositor state
    bool m_hasComposite = false;
    bool m_isRedirected = false;
 
    // Shared memory resources
    xcb_shm_seg_t m_segment = 0;
    void* m_shmData = nullptr;
    int m_shmId = -1;
 
    // Wayland resources
    WaylandContext m_wayland;

    std::queue<std::chrono::steady_clock::time_point> m_captureTimings; // rename from m_frameTimings
    float m_sourceFps = 0.0f;
};