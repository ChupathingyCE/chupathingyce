/* Native macOS renders one ordinary view. VisionOS stereo is deliberately
   unavailable; output parameters are always initialized conservatively. */
#include "halo_stereo.h"
#include <string.h>
static const struct halo_stereo_frame mono_frame = { 0 };
const struct halo_stereo_frame *halo_stereo_frame(void) { return &mono_frame; }
int halo_stereo_current_layer(void) { return HALO_STEREO_LAYER_MONO; }
int halo_stereo_hud_split(void) { return 0; }
int halo_stereo_ui_span(void) { return 0; }
int halo_stereo_zoom(void) { return 0; }
int halo_stereo_zoom_overlay_on(void) { return 0; }
int halo_stereo_film(void) { return 0; }
int halo_stereo_film_letterbox(void) { return 0; }
int halo_stereo_cut_covered(void) { return 0; }
int halo_stereo_cut_requested(void) { return 0; }
int halo_first_person_body_depth_clamp(void) { return 0; }
float halo_stereo_hud_resolution(void) { return 1.0f; }
float halo_stereo_zoom_fit(float aspect) { (void)aspect; return 1.0f; }
void halo_stereo_zoom_density(float scale[2]) { scale[0] = scale[1] = 1.0f; }
void halo_stereo_zoom_view(float t[2]) { t[0] = t[1] = 1.0f; }
void halo_stereo_hud_tangents(float t[2]) { t[0] = t[1] = 0.0f; }
void halo_stereo_reticle(float v[3]) { v[0] = v[1] = 0.0f; v[2] = -1.0f; }
void halo_stereo_reticle_drew(void) { }
void halo_stereo_set_ui_shown(int shown) { (void)shown; }
void halo_stereo_depth_range(float *near, float *far) { *near = 0.1f; *far = 1000.0f; }
void halo_stereo_fade(float v[4]) { memset(v, 0, 4 * sizeof(float)); }
int halo_stereo_expansion(float *progress, float *bars) { *progress = *bars = 0.0f; return 0; }
int halo_stereo_cutscene_frame(float f[3], float u[3], float t[2], float *dim) {
    memset(f, 0, 3 * sizeof(float)); memset(u, 0, 3 * sizeof(float));
    t[0] = t[1] = *dim = 0.0f; return 0;
}
int halo_stereo_side_by_side_cutscene(int eye, float t[4]) {
    (void)eye; memset(t, 0, 4 * sizeof(float)); return 0;
}
int halo_stereo_side_by_side_window(int eye, struct halo_stereo_window_model *model) {
    (void)eye; memset(model, 0, sizeof(*model)); return 0;
}
float halo_stereo_vignette(void) { return 0.0f; }
int halo_hud_group_current(void) { return HALO_HUD_GROUP_NONE; }
void halo_hud_group_extent_in(int group, float x0, float y0, float x1, float y1) {
    (void)group; (void)x0; (void)y0; (void)x1; (void)y1;
}
int halo_hud_group_rectangle(int group, float r[4]) {
    (void)group; memset(r, 0, 4 * sizeof(float)); return 0;
}
