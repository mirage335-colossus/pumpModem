# VkFFT native Vulkan dependency

This is the unmodified header closure and MIT license from
[VkFFT v1.3.4](https://github.com/DTolm/VkFFT/releases/tag/v1.3.4), commit
[`066a17c17068c0f11c9298d848c2976c71fad1c1`](https://github.com/DTolm/VkFFT/commit/066a17c17068c0f11c9298d848c2976c71fad1c1).
`UPSTREAM.sha256` records every retained upstream file. The official GitHub tag
archive used for import had SHA-256
`25706a60d5f21d9db09cf602d61b00d58389094967efb64d51950aa09c47ea15`.

Only `vkFFT/**/*.h` and `LICENSE` are retained. Upstream examples, benchmark
programs, plots, generated measurements, and other backend dependencies are
omitted. There are no local patches. The optional `DATAPUMP_TRIAD_VULKAN` target
checks the inventory during configuration and includes these files in the native
probe's source identity. Ordinary builds do not download dependencies.

Vulkan headers/loader and glslang/SPIR-V development libraries are explicit host
dependencies. GLSL compilation runs in the native process; no Python runtime,
external shader-compiler process or ROCm installation is required. On Debian 13,
the development packages are `libvulkan-dev`, `glslang-dev`, `glslang-tools`, and
`spirv-tools`. A verified unprivileged extraction can instead be supplied through
`CMAKE_PREFIX_PATH`, with `Vulkan_LIBRARY` pointing to the matching installed
loader if its development symlink is not present in that prefix. GPU drivers
remain supplied by the operating system.

VkFFT's half mode stores FP16 but computes in FP32; it is not evidence of packed
FP16 arithmetic or matrix instructions. Backend precision and execution must be
qualified separately. Upgrade the tag/commit and complete inventory together,
then run the native Vulkan numerical and affected receiver checks on an actual
hardware device. A software Vulkan implementation is not GPU qualification.
