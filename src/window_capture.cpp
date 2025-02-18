#include "window_capture.hpp"
#include <cstring>
#include <dlfcn.h>
#include <iomanip>
#include <sstream>
 
bool WindowCapture::Initialize(uint32_t windowId) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << windowId;
    LOG_INFO("Initializing window capture for window ID: ", ss.str());
    m_window = windowId;
 
    if (!DetectDisplayServer()) {
        LOG_ERROR("Failed to detect display server");
        return false;
    }
 
    switch (m_displayServer) {
        case DisplayServer::X11:
        case DisplayServer::XWAYLAND:
            if (!SetupX11Connection()) {
                LOG_ERROR("Failed to setup X11 connection");
                return false;
            }
            if (!InitializeCompositing()) {
                LOG_WARN("Compositing not available, falling back to basic capture");
            }
            if (!GetTopLevelParent()) {
                LOG_ERROR("Failed to get top-level parent window");
                return false;
            }
            break;
 
        case DisplayServer::WAYLAND:
            if (!SetupWaylandConnection()) {
                LOG_ERROR("Failed to setup Wayland connection");
                return false;
            }
            break;
    }
 
    if (!UpdateWindowGeometry()) {
        LOG_ERROR("Failed to get window geometry");
        return false;
    }
 
    uint32_t size = m_width * m_height * 4; // RGBA
    LOG_INFO("Allocating shared memory of size: ", size, " bytes");
    if (!SetupSharedMemory(size)) {
        LOG_ERROR("Failed to setup shared memory");
        return false;
    }
 
    LOG_INFO("WindowCapture initialized successfully");
    return true;
}
 
bool WindowCapture::DetectDisplayServer() {
    const char* wayland_display = getenv("WAYLAND_DISPLAY");
    const char* x11_display = getenv("DISPLAY");
    
    if (wayland_display) {
        if (x11_display) {
            Display* display = XOpenDisplay(nullptr);
            if (display) {
                const char* vendor = ServerVendor(display);
                if (vendor && strstr(vendor, "XWayland")) {
                    m_displayServer = DisplayServer::XWAYLAND;
                    LOG_INFO("Detected XWayland display server");
                } else {
                    m_displayServer = DisplayServer::X11;
                    LOG_INFO("Detected X11 display server with Wayland present");
                }
                XCloseDisplay(display);
            }
        } else {
            m_displayServer = DisplayServer::WAYLAND;
            LOG_INFO("Detected native Wayland display server");
        }
    } else if (x11_display) {
        m_displayServer = DisplayServer::X11;
        LOG_INFO("Detected X11 display server");
    } else {
        LOG_ERROR("No display server detected");
        return false;
    }
 
    return true;
}
 
bool WindowCapture::SetupX11Connection() {
    m_connection = xcb_connect(nullptr, &m_screen);
    if (xcb_connection_has_error(m_connection)) {
        LOG_ERROR("Failed to connect to X server");
        return false;
    }
 
    auto setup = xcb_get_setup(m_connection);
    auto screen = xcb_setup_roots_iterator(setup).data;
    m_rootWindow = screen->root;
 
    // Check for SHM extension
    auto shm_query = xcb_shm_query_version(m_connection);
    auto shm_reply = xcb_shm_query_version_reply(m_connection, shm_query, nullptr);
    if (!shm_reply) {
        LOG_WARN("Server does not support SHM, performance may be reduced");
        free(shm_reply);
    }
 
    return true;
}
 
bool WindowCapture::InitializeCompositing() {
    auto composite_query = xcb_composite_query_version(
        m_connection,
        XCB_COMPOSITE_MAJOR_VERSION,
        XCB_COMPOSITE_MINOR_VERSION
    );
    
    auto composite_reply = xcb_composite_query_version_reply(
        m_connection,
        composite_query,
        nullptr
    );
 
    if (!composite_reply) {
        LOG_WARN("Composite extension not available");
        return false;
    }
 
    free(composite_reply);
    m_hasComposite = true;
 
    if (!RedirectWindow()) {
        LOG_WARN("Failed to redirect window for compositing");
        return false;
    }
 
    return true;
}
 
bool WindowCapture::RedirectWindow() {
    if (!m_hasComposite) return false;
 
    xcb_composite_redirect_window(
        m_connection,
        m_window,
        XCB_COMPOSITE_REDIRECT_AUTOMATIC
    );
 
    auto error = xcb_request_check(m_connection, 
        xcb_composite_redirect_window_checked(
            m_connection,
            m_window,
            XCB_COMPOSITE_REDIRECT_AUTOMATIC
        )
    );
 
    if (error) {
        LOG_WARN("Failed to redirect window: error code ", error->error_code);
        free(error);
        return false;
    }
 
    m_isRedirected = true;
    return true;
}
 
bool WindowCapture::GetTopLevelParent() {
    xcb_window_t parent = m_window;
    xcb_window_t root = 0;
    xcb_window_t* children = nullptr;
    uint32_t num_children = 0;
 
    while (true) {
        auto query_cookie = xcb_query_tree(m_connection, parent);
        auto query_reply = xcb_query_tree_reply(m_connection, query_cookie, nullptr);
        
        if (!query_reply) {
            LOG_ERROR("Failed to query window tree");
            return false;
        }
 
        xcb_window_t new_parent = query_reply->parent;
        root = query_reply->root;
        
        free(query_reply);
 
        if (new_parent == root) {
            m_topLevelWindow = parent;
            return true;
        }
        
        parent = new_parent;
    }
}
 
bool WindowCapture::TranslateCoordinates() {
    auto translate_cookie = xcb_translate_coordinates(
        m_connection,
        m_window,
        m_rootWindow,
        0, 0
    );
    
    auto translate_reply = xcb_translate_coordinates_reply(
        m_connection,
        translate_cookie,
        nullptr
    );
 
    if (!translate_reply) {
        LOG_ERROR("Failed to translate coordinates");
        return false;
    }
 
    m_absoluteX = translate_reply->dst_x;
    m_absoluteY = translate_reply->dst_y;
    
    free(translate_reply);
    
    LOG_INFO("Absolute window position: ", m_absoluteX, ",", m_absoluteY);
    return true;
}
 
bool WindowCapture::UpdateWindowGeometry() {
    auto cookie = xcb_get_geometry(m_connection, m_window);
    auto geometry = xcb_get_geometry_reply(m_connection, cookie, nullptr);
    
    if (!geometry) {
        LOG_ERROR("Failed to get window geometry");
        return false;
    }
 
    m_width = geometry->width;
    m_height = geometry->height;
    m_windowX = geometry->x;
    m_windowY = geometry->y;
    
    free(geometry);
 
    if (!TranslateCoordinates()) {
        LOG_ERROR("Failed to get absolute coordinates");
        return false;
    }
 
    LOG_INFO("Window geometry updated - Size: ", m_width, "x", m_height,
             " Position: ", m_windowX, ",", m_windowY,
             " Absolute: ", m_absoluteX, ",", m_absoluteY);
    return true;
}
 
bool WindowCapture::SetupSharedMemory(uint32_t size) {
    m_shmId = shmget(IPC_PRIVATE, size, IPC_CREAT | 0777);
    if (m_shmId == -1) {
        LOG_ERROR("Failed to create shared memory segment, errno: ", errno);
        return false;
    }
 
    m_shmData = shmat(m_shmId, nullptr, 0);
    if (m_shmData == (void*)-1) {
        LOG_ERROR("Failed to attach shared memory, errno: ", errno);
        shmctl(m_shmId, IPC_RMID, nullptr);
        m_shmId = -1;
        return false;
    }
 
    m_segment = xcb_generate_id(m_connection);
    auto cookie = xcb_shm_attach_checked(m_connection, m_segment, m_shmId, 0);
    auto error = xcb_request_check(m_connection, cookie);
    if (error) {
        LOG_ERROR("Failed to attach XCB SHM segment, error code: ", error->error_code);
        free(error);
        CleanupSharedMemory();
        return false;
    }
 
    shmctl(m_shmId, IPC_RMID, nullptr);
    return true;
}
 
void WindowCapture::CleanupSharedMemory() {
    if (m_connection && m_segment) {
        xcb_shm_detach(m_connection, m_segment);
        m_segment = 0;
    }
 
    if (m_shmData && m_shmData != (void*)-1) {
        shmdt(m_shmData);
        m_shmData = nullptr;
    }
 
    if (m_shmId != -1) {
        shmctl(m_shmId, IPC_RMID, nullptr);
        m_shmId = -1;
    }
}
 
bool WindowCapture::GetWindowSize(uint32_t& width, uint32_t& height) {
    if (!UpdateWindowGeometry()) {
        return false;
    }
    
    width = m_width;
    height = m_height;
    return true;
}
 
bool WindowCapture::CaptureFrame(Frame& frame) {
    switch (m_displayServer) {
        case DisplayServer::X11:
            return m_hasComposite ? CaptureXCompositeFrame(frame) : CaptureX11Frame(frame);
        case DisplayServer::XWAYLAND:
            return CaptureXCompositeFrame(frame);
        case DisplayServer::WAYLAND:
            return CaptureWaylandFrame(frame);
        default:
            LOG_ERROR("Unknown display server type");
            return false;
    }
}
 
bool WindowCapture::CaptureX11Frame(Frame& frame) {
    if (!UpdateWindowGeometry()) {
        return false;
    }
 
    auto cookie = xcb_get_image(
        m_connection,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        m_window,
        0, 0,
        m_width, m_height,
        ~0
    );
 
    xcb_generic_error_t* error = nullptr;
    auto reply = xcb_get_image_reply(m_connection, cookie, &error);
 
    if (error) {
        LOG_ERROR("XCB error during image capture. Code: ", error->error_code);
        free(error);
        return false;
    }
 
    if (!reply) {
        LOG_ERROR("Failed to get image reply");
        return false;
    }
 
    uint8_t* data = xcb_get_image_data(reply);
    uint32_t size = xcb_get_image_data_length(reply);
    
    bool result = CopyToStagingBuffer(data, size, frame);
    free(reply);
    
    return result;
}
 
bool WindowCapture::CaptureXCompositeFrame(Frame& frame) {
    if (!UpdateWindowGeometry()) {
        return false;
    }
 
    // Name the window pixmap
    xcb_pixmap_t pixmap = xcb_generate_id(m_connection);
    auto name_cookie = xcb_composite_name_window_pixmap_checked(m_connection, m_window, pixmap);
    
    auto error = xcb_request_check(m_connection, name_cookie);
    if (error) {
        LOG_ERROR("Failed to name window pixmap, error code: ", error->error_code);
        free(error);
        return false;
    }
 
    // Try SHM capture first
    auto shm_cookie = xcb_shm_get_image(
        m_connection,
        pixmap,
        0, 0,
        m_width, m_height,
        ~0,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        m_segment,
        0
    );
 
    error = xcb_request_check(m_connection, shm_cookie);
    if (!error) {
        // SHM capture succeeded
        xcb_free_pixmap(m_connection, pixmap);
        return CopyToStagingBuffer(m_shmData, m_width * m_height * 4, frame);
    }
    free(error);
 
    // Fallback to regular capture
    auto cookie = xcb_get_image(
        m_connection,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        pixmap,
        0, 0,
        m_width, m_height,
        ~0
    );
 
    auto reply = xcb_get_image_reply(m_connection, cookie, &error);
    if (error) {
        LOG_ERROR("Failed to capture pixmap, error code: ", error->error_code);
        free(error);
        xcb_free_pixmap(m_connection, pixmap);
        return false;
    }
 
    if (!reply) {
        LOG_ERROR("Failed to get image reply");
        xcb_free_pixmap(m_connection, pixmap);
        return false;
    }
 
    uint8_t* data = xcb_get_image_data(reply);
    uint32_t size = xcb_get_image_data_length(reply);
    
    bool result = CopyToStagingBuffer(data, size, frame);
    free(reply);
    xcb_free_pixmap(m_connection, pixmap);
    
    return result;
}
 
bool WindowCapture::CaptureWaylandFrame(Frame& frame) {
    if (m_displayServer != DisplayServer::WAYLAND) {
        LOG_ERROR("Native Wayland capture not implemented, use XWayland instead");
        return false;
    }
    
    LOG_ERROR("Native Wayland capture not yet implemented");
    return false;
}
 
bool WindowCapture::CopyToStagingBuffer(const void* data, size_t size, Frame& frame) {
    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize bufferSize = m_width * m_height * 4;
 
    if (size < bufferSize) {
        LOG_ERROR("Captured image size (", size, ") smaller than expected (", bufferSize, ")");
        return false;
    }
 
    if (!FrameManager::Get().CreateStagingBuffer(stagingBuffer, stagingMemory, bufferSize)) {
        LOG_ERROR("Failed to create staging buffer");
        return false;
    }
 
    // Copy data to staging buffer
    void* mappedData;
    if (vkMapMemory(VulkanContext::Get().GetDevice(), stagingMemory, 0, bufferSize, 0, &mappedData) != VK_SUCCESS) {
        LOG_ERROR("Failed to map staging buffer memory");
        FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);
        return false;
    }
 
    memcpy(mappedData, data, bufferSize);
    vkUnmapMemory(VulkanContext::Get().GetDevice(), stagingMemory);
 
    // Copy staging buffer to frame
    VkCommandBuffer commandBuffer = FrameManager::Get().BeginSingleTimeCommands();
    if (!commandBuffer) {
        LOG_ERROR("Failed to begin command buffer");
        FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);
        return false;
    }
 
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = m_width;
    region.imageExtent.height = m_height;
    region.imageExtent.depth = 1;
 
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = frame.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
 
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
 
    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        frame.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
 
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
 
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
 
    FrameManager::Get().EndSingleTimeCommands(commandBuffer);
    FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);
 
    return true;
}
 
void WindowCapture::Cleanup() {
    CleanupSharedMemory();
    
    if (m_connection) {
        xcb_disconnect(m_connection);
        m_connection = nullptr;
    }
 
    if (m_wayland.display) {
        if (m_wayland.registry) {
            wl_registry_destroy(m_wayland.registry);
        }
        if (m_wayland.compositor) {
            wl_compositor_destroy(m_wayland.compositor);
        }
        if (m_wayland.shm) {
            wl_shm_destroy(m_wayland.shm);
        }
        wl_display_disconnect(m_wayland.display);
    }
}
