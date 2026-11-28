# Vulkan headers

The Khronos Vulkan API headers, vendored unchanged (`VK_HEADER_VERSION`
361, as bgfx carries them in `3rdparty/khronos/vulkan-local` at bgfx commit
4608478e119667eab7635226f721ede32b0935c3): `vulkan_core.h`, `vk_platform.h`
and the `vk_video` headers `vulkan_core.h` includes. Apache-2.0 or MIT (see
`LICENSE`).

What uses them: `display.ray_tracing`'s Vulkan side path
(`port/linux/src/posix_modern_lighting_rt.c`, 64-bit Linux), which loads
`libvulkan.so.1` at run time (`dlopen`) and so needs only the types and
function pointer declarations: no Vulkan SDK and no Vulkan library at build
time. Built with `-Iport/third_party/vulkan-headers/include`
(`tools/linux_build.py`, `tools/lp64_build.py`).
