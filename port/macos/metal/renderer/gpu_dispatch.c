/* Native macOS dispatch: compiled only for the explicit Metal opt-in. */
#include "gpu.h"
extern const struct gpu_backend gpu_backend_metal;
#define METAL_FUNCTION(type, name, parameters, arguments) \
    type gpu_##name parameters { return gpu_backend_metal.name arguments; }
#define METAL_PROCEDURE(name, parameters, arguments) \
    void gpu_##name parameters { gpu_backend_metal.name arguments; }
GPU_FUNCTIONS(METAL_FUNCTION, METAL_PROCEDURE)
