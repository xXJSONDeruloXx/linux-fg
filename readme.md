
# Real-time Window Upscaling with Frame Interpolation

A Linux application for real-time window capture, upscaling and frame interpolation using Vulkan compute shaders.

## Features

- X11 window capture using XCB/SHM
- Vulkan-based image processing pipeline
- Lanczos scaling shader for high-quality upscaling
- Motion-based frame interpolation (WIP)
- Configurable input/output resolutions and FPS target

## Building

### Dependencies

For Debian/Ubuntu:
```bash
sudo apt install build-essential cmake libvulkan-dev vulkan-tools \
    libx11-dev libxcb-dev pkg-config vulkan-headers \
    libxshmfence-dev libxcb-shm0-dev libxcb-image0-dev \
    spirv-tools glslang-tools vulkan-validationlayers-dev \
    libwayland-dev wayland-protocols
```

For Arch Linux:
```bash
sudo pacman -S base-devel cmake vulkan-devel vulkan-tools shaderc xcb-util \
    xcb-util-wm xcb-util-keysyms libx11 libxcb pkg-config vulkan-headers \
    vulkan-icd-loader libxshmfence xcb-util-image spirv-tools glslang \
    vulkan-extra-layers vulkan-extra-tools vulkan-validation-layers
```

### Compilation

```bash
cmake .
make -j$(nproc)
```

## Usage

First, get the window ID you want to capture:
```bash
xwininfo | grep "Window id"
```

Then run the application:
```bash
./lossless-scaling [options] window-id
```

### Options

```
--help                   Show help message
--input-width WIDTH      Input width (default: auto-detect)
--input-height HEIGHT    Input height (default: auto-detect) 
--output-width WIDTH     Output width
--output-height HEIGHT   Output height
--target-fps FPS        Target FPS (default: 60)
--no-interpolation      Disable frame interpolation
--interpolation-factor F Interpolation blend factor (0.0-1.0)
```

## Implementation Details

The application consists of several key components:

- **Window Capture**: Uses X11/XCB with shared memory for efficient window content capture
- **Frame Management**: Handles Vulkan image resources and synchronization
- **Compute Shaders**:
  - scale.comp: Lanczos upscaling filter
  - motion.comp: Motion vector estimation between frames
  - interpolate.comp: Frame interpolation using motion vectors

## Development Roadmap

- [ ] Implement and test basic window capture and scaling
- [ ] Add motion vector extraction using compute shaders
- [ ] Implement frame interpolation using GLSL shaders
- [ ] Integrate motion-based frame warping
- [ ] Evaluate quality and performance metrics
- [ ] Consider integration with gamescope compositor
- [ ] Investigate advanced interpolation methods (e.g. RIFE)

## Contributing

This is an early development project. Feel free to open issues or submit pull requests.

## License

[Insert chosen license]

## References

- [MobFGSR](https://github.com/Mob-FGSR/MobFGSR) - Frame Generation and Super Resolution techniques