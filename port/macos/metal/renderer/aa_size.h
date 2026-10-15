/* SSAA 2x uses twice the pixels each way, as port/linux/src/d3d8_gl.c's
   screen_mode_choose does. Cap the multiplier at the GPU's target limit
   without changing the picture's shape. Never shrink an existing target.
   Pure arithmetic shared by production and sanitizer tests. */
#ifndef HALO_METAL_AA_SIZE_H
#define HALO_METAL_AA_SIZE_H
#include <math.h>

static inline float metal_ssaa_factor(float width, float height, int maximum)
{
    float factor = 2.0f;

    if (!isfinite(width) || !isfinite(height) || width <= 0.0f || height <= 0.0f || maximum <= 0)
        return 1.0f;
    if (width * factor > (float)maximum)
        factor = (float)maximum / width;
    if (height * factor > (float)maximum)
        factor = (float)maximum / height;
    return factor > 1.0f ? factor : 1.0f;
}
#endif
