#include "window_capture.hpp"
#include <iomanip>
#include <sstream>

bool WindowCapture::Initialize(uint32_t windowId) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << windowId;
    LOG_INFO("Initializing window capture for window ID: ", ss.str());
    m_window = windowId;

    if (!SetupX11Connection()) {
        LOG_ERROR("Failed to setup X11 connection");
        return false;
    }

    auto setup = xcb_get_setup(m_connection);
    auto screen = xcb_setup_roots_iterator(setup).data;
    m_rootWindow = screen->root;
    
    std::stringstream rootss;
    rootss << "0x" << std::hex << std::setw(8) << std::setfill('0') << m_rootWindow;
    LOG_INFO("Root window ID: ", rootss.str());

    auto cookie = xcb_get_window_attributes(m_connection, m_window);
    auto attrs = xcb_get_window_attributes_reply(m_connection, cookie, nullptr);
    if (!attrs) {
        LOG_ERROR("Window ", ss.str(), " does not exist or is not accessible");
        return false;
    }

    auto prop_cookie = xcb_get_property(m_connection, 0, m_window,
        XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 1024);
    auto prop = xcb_get_property_reply(m_connection, prop_cookie, nullptr);
    
    if (prop && prop->type == XCB_ATOM_STRING) {
        int len = xcb_get_property_value_length(prop);
        char* name = (char*)xcb_get_property_value(prop);
        LOG_INFO("Window name: ", std::string(name, len));
    }
    
    if (prop) free(prop);
    free(attrs);

    uint32_t width, height;
    if (!GetWindowSize(width, height)) {
        LOG_ERROR("Failed to get window size");
        return false;
    }
    LOG_INFO("Initial window size: ", width, "x", height);

    uint32_t size = width * height * 4;
    LOG_INFO("Allocating shared memory of size: ", size, " bytes");
    if (!SetupSharedMemory(size)) {
        LOG_ERROR("Failed to setup shared memory");
        return false;
    }

    LOG_INFO("WindowCapture initialized successfully for window ", ss.str());
    return true;
}

void WindowCapture::Cleanup() {
    CleanupSharedMemory();
    
    if (m_connection) {
        xcb_disconnect(m_connection);
        m_connection = nullptr;
    }
}

bool WindowCapture::SetupX11Connection() {
    m_connection = xcb_connect(nullptr, &m_screen);
    if (xcb_connection_has_error(m_connection)) {
        LOG_ERROR("Failed to connect to X server");
        return false;
    }

    auto shm_query = xcb_shm_query_version(m_connection);
    auto shm_reply = xcb_shm_query_version_reply(m_connection, shm_query, nullptr);
    if (!shm_reply) {
        LOG_ERROR("Server does not support SHM");
        return false;
    }
    free(shm_reply);

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
    auto cookie = xcb_get_geometry(m_connection, m_window);
    auto geometry = xcb_get_geometry_reply(m_connection, cookie, nullptr);
    
    if (!geometry) {
        LOG_ERROR("Failed to get window geometry");
        return false;
    }

    width = geometry->width;
    height = geometry->height;
    
    m_windowX = geometry->x;
    m_windowY = geometry->y;
    
    LOG_INFO("Window position: ", m_windowX, ",", m_windowY);
    LOG_INFO("Window dimensions: ", width, "x", height);
    
    free(geometry);
    return true;
}

bool WindowCapture::CaptureFrame(Frame& frame) {
    uint32_t width, height;
    if (!GetWindowSize(width, height)) {
        return false;
    }

    // Attempt to capture from root window at window's position
    auto cookie = xcb_get_image(
        m_connection,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        m_rootWindow,
        m_windowX, m_windowY,   // x, y position of the window
        width, height,          // width, height
        ~0                      // plane mask
    );

    xcb_generic_error_t* error = nullptr;
    auto reply = xcb_get_image_reply(m_connection, cookie, &error);

    if (error) {
        LOG_ERROR("XCB error during image capture. Code: ", error->error_code);
        LOG_ERROR("Major opcode: ", error->major_code);
        LOG_ERROR("Minor opcode: ", error->minor_code);
        LOG_ERROR("Resource ID: ", error->resource_id);
        free(error);
        return false;
    }

    if (!reply) {
        LOG_ERROR("Failed to get reply for image capture (no error provided)");
        return false;
    }

    uint8_t* data = xcb_get_image_data(reply);
    uint32_t size = xcb_get_image_data_length(reply);
    LOG_INFO("Captured image size: ", size, " bytes");

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize bufferSize = width * height * 4;

    if (!FrameManager::Get().CreateStagingBuffer(stagingBuffer, stagingMemory, bufferSize)) {
        LOG_ERROR("Failed to create staging buffer");
        free(reply);
        return false;
    }

    void* mappedData;
    vkMapMemory(VulkanContext::Get().GetDevice(), stagingMemory, 0, bufferSize, 0, &mappedData);
    memcpy(mappedData, data, size);
    vkUnmapMemory(VulkanContext::Get().GetDevice(), stagingMemory);

    free(reply);

    VkCommandBuffer commandBuffer = FrameManager::Get().BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = width;
    region.imageExtent.height = height;
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

    // Cleanup staging buffer
    FrameManager::Get().DestroyStagingBuffer(stagingBuffer, stagingMemory);

    return true;
}
