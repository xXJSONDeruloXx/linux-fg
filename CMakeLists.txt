cmake_minimum_required(VERSION 3.15)
project(lossless-scaling LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Vulkan REQUIRED)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED xcb xcb-shm)
find_package(SDL2 REQUIRED)

# Shader compilation
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
)

# Link libraries
target_link_libraries(lossless-scaling PRIVATE
    ${Vulkan_LIBRARIES}
    ${X11_LIBRARIES}
    ${XCB_LIBRARIES}
    ${SDL2_LIBRARIES}
    xcb-shm
    X11-xcb
)

# Installation
install(TARGETS lossless-scaling
    RUNTIME DESTINATION bin
)

# Install shaders
install(DIRECTORY ${CMAKE_BINARY_DIR}/shaders/
    DESTINATION share/lossless-scaling/shaders
)
