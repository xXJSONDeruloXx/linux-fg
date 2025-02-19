cmake_minimum_required(VERSION 3.15)
project(lossless-scaling LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# Find required packages
find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)

# Use pkg-config for Wayland instead of find_package
pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-server wayland-egl wayland-cursor wayland-protocols)
include_directories(${WAYLAND_INCLUDE_DIRS})
link_directories(${WAYLAND_LIBRARY_DIRS})
add_definitions(${WAYLAND_CFLAGS_OTHER})

# Use pkg-config for XCB dependencies
pkg_check_modules(XCB REQUIRED xcb xcb-shm xcb-composite xcb-xfixes)

# Use pkg-config for SDL2 instead of find_package
pkg_check_modules(SDL2 REQUIRED sdl2)
include_directories(${SDL2_INCLUDE_DIRS})
link_directories(${SDL2_LIBRARY_DIRS})

# Add SDL2_ttf dependency
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
include_directories(${SDL2_TTF_INCLUDE_DIRS})
link_directories(${SDL2_TTF_LIBRARY_DIRS})

# Shader compilation function
find_program(GLSLC glslc REQUIRED)
function(compile_shader TARGET SHADER)
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders/"
        COMMAND ${GLSLC} -O --target-env=vulkan1.2 ${SHADER} -o ${SPIRV}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}"
    )
    
    target_sources(${TARGET} PRIVATE ${SPIRV})
endfunction()

# Source files
set(SOURCES
    src/main.cpp
    src/scaler.cpp
    src/frame_manager.cpp
    src/window_capture.cpp
    src/vulkan_context.cpp
)

# Create executable
add_executable(lossless-scaling ${SOURCES})

# Compile shaders
compile_shader(lossless-scaling ${CMAKE_SOURCE_DIR}/shaders/motion.comp)
compile_shader(lossless-scaling ${CMAKE_SOURCE_DIR}/shaders/interpolate.comp)
compile_shader(lossless-scaling ${CMAKE_SOURCE_DIR}/shaders/scale.comp)

# Include directories
target_include_directories(lossless-scaling PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${Vulkan_INCLUDE_DIRS}
    ${X11_INCLUDE_DIRS}
    ${XCB_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${WAYLAND_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(lossless-scaling PRIVATE
    ${Vulkan_LIBRARIES}
    ${X11_LIBRARIES}
    ${XCB_LIBRARIES}
    ${SDL2_LIBRARIES}  # Changed from SDL2::SDL2
    ${WAYLAND_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    xcb-shm
    X11-xcb
    Xcomposite
)

# Installation
install(TARGETS lossless-scaling
    RUNTIME DESTINATION bin
)

# Install shaders
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/
    DESTINATION share/lossless-scaling/shaders
)