/*
OPENSAUCE_POSTPROCESS.H

OpenSauce's post-processing for the maps that carry it
(port/linux/game/opensauce_postprocess.c): the effects of a map's
effect_postprocess_collection tag (efpc, with its effect_postprocess_generic
and shader_postprocess_generic tags: efpg, shpg), its shader_postprocess_
globals' bloom (sppg), and the pp_* script functions that drive them, drawn
over the 3D view at OpenSauce's render stages. The shaders are the tags' own
compiled Direct3D 9 effects, turned into GLSL (port/linux/src/d3d9_effect.c)
and drawn by the OpenGL renderer (port/linux/src/opensauce_postprocess_gl.inl).

Only a map with an OpenSauce header (opensauce.h) and these tags has any of
it: every other map draws as it did, and the render stages return at once.
display.opensauce_post_processing turns it off.
*/

#ifndef OPENSAUCE_POSTPROCESS_H
#define OPENSAUCE_POSTPROCESS_H

/* ---------- constants */

/* OpenSauce's render stages (postprocess_render_stage), in its numbering */
enum
{
	_opensauce_pp_stage_pre_blur = 0,
	_opensauce_pp_stage_pre_alpha_blended,
	_opensauce_pp_stage_pre_hud,
	_opensauce_pp_stage_pre_ui,
	_opensauce_pp_stage_post_ui,
	_opensauce_pp_stage_blur,
	NUMBER_OF_OPENSAUCE_PP_STAGES
};

/* a parameter's value type (shader_variable_base_type) */
enum
{
	_opensauce_pp_value_boolean = 0,
	_opensauce_pp_value_integer,
	_opensauce_pp_value_float,
	_opensauce_pp_value_argb_color,
	_opensauce_pp_value_matrix,
	_opensauce_pp_value_texture,
	NUMBER_OF_OPENSAUCE_PP_VALUE_TYPES
};

/* a shader pass's flags (s_pass_definition) */
enum
{
	_opensauce_pp_pass_clear_target_bit = 0,
	_opensauce_pp_pass_copy_scene_to_target_bit,
	_opensauce_pp_pass_clear_buffer_texture_bit,
};

#if defined(HALO_CUSTOM_EDITION) && defined(HALO_MAP_LIST) && !defined(HALO_XBOX_CONSOLE) && \
	!defined(HALO_SERVER) && !defined(HALO_CONSOLE) && !defined(HALO_SWITCH_CONSOLE)
#define OPENSAUCE_POSTPROCESS
#endif

/* ---------- the renderer's part (port/linux/src/opensauce_postprocess_gl.inl) */

/* what a stage's draws know of the frame */
struct opensauce_pp_frame
{
	float near_clip, far_clip;
	/* the world to clip space matrix the 3D view was drawn by (clip =
	matrix[i] . (x, y, z, 1) for each of x, y, z, w), this frame's and the
	last frame's, for the depth, normals and velocity OpenSauce's shaders
	read (its "G-buffer", made here from the depth buffer) */
	float clip_from_world[4][4];
	float previous_clip_from_world[4][4];
	int previous_valid;
	/* the projection's scale of x and y in view space, for the normals */
	float projection_scale[2];
	/* OpenSauce's HUD scale (4:3 in the screen's aspect) */
	float hud_scale[2];
	/* the 3D view's bounds in the game's units of the screen */
	short x0, y0, x1, y1;
	/* counts the frames (the G-buffer is made once each) */
	long frame_serial;
};

/* a pass of a shader's technique, as its tag has it */
struct opensauce_pp_pass
{
	unsigned short flags;
	short render_chain;
};

/* whether the renderer draws post-processing (the OpenGL renderer, outside
OpenGL ES and the consoles) */
int halo_opensauce_pp_available(void);
/* a shader's compiled effect loaded, with its technique by name: a handle
(0 or more), or -1 if it does not read or translate (said in the log by
name) */
long halo_opensauce_pp_effect_load(const void *binary, unsigned long size, const char *name, const char *technique);
/* whether an effect has a parameter of the name */
int halo_opensauce_pp_parameter_exists(long effect, const char *name);
/* every effect let go (the map unloads) */
void halo_opensauce_pp_unload(void);
/* a stage begun over the 3D view: FALSE if nothing can be drawn now */
int halo_opensauce_pp_stage_begin(const struct opensauce_pp_frame *frame, int stage);
/* an effect begun (the chains' first-render flags set), and ended: its
result made the scene if it drew */
void halo_opensauce_pp_effect_begin(void);
void halo_opensauce_pp_effect_end(int drew, float fade);
/* a parameter of an effect set by name, as D3DX sets one: values as
32-bit words (floats, or ints for the integers and booleans), count of
them */
void halo_opensauce_pp_parameter_set(long effect, const char *name, int type, const void *values, int count);
/* a texture parameter set by name to a bitmap's texture (the game's
D3DBaseTexture), NULL for none */
void halo_opensauce_pp_parameter_texture(long effect, const char *name, const void *texture);
/* a shader drawn with its passes over a quad (tessellation and bounds as
the effect instance's); FALSE if it could not be */
int halo_opensauce_pp_shader_draw(long effect, const struct opensauce_pp_pass *passes, int pass_count,
	const short tessellation[2], const float x_bounds[2], const float y_bounds[2]);
/* the bloom of shader_postprocess_globals (OpenSauce's own shader, made
natively): size, exposure, mix amount, minimum and maximum colors */
int halo_opensauce_pp_bloom(float size, float exposure, float mix_amount, const float minimum[3],
	const float maximum[3]);
/* the stage ended: the result put back in the back buffer if any drew */
void halo_opensauce_pp_stage_end(void);

/* ---------- the game's part (port/linux/game/opensauce_postprocess.c) */

#ifdef OPENSAUCE_POSTPROCESS

struct render_frustum;

/* a map's tags loaded: its collection and globals read (OpenSauce maps
only: opensauce_map.c) */
void opensauce_postprocess_tags_loaded(void *tag_instances, long tag_count);
/* the map unloaded */
void opensauce_postprocess_map_unloaded(void);
/* a render stage of the 3D view (source/render/render.c): pre_alpha_
blended draws pre_blur, blur and pre_alpha_blended, as OpenSauce does */
void opensauce_postprocess_render_stage(int stage, const struct render_frustum *frustum, short x0, short y0,
	short x1, short y1);

/* the pp_* script functions (opensauce_scripts.c) */
void opensauce_postprocess_set_instance_active(short instance, int active);
void opensauce_postprocess_set_instance_fade(short instance, float start, float end, float time);
float opensauce_postprocess_instance_current_fade(short instance);
void opensauce_postprocess_set_variable_integer(short effect, short variable, long value, float time);
void opensauce_postprocess_set_variable_real(short effect, short variable, const float values[4], float time);
void opensauce_postprocess_bloom_set_maximum_color(const float color[3], float time);
void opensauce_postprocess_bloom_set_exposure(float exposure, float time);
/* whether a map's post-processing is drawn (for the script functions'
logging) */
int opensauce_postprocess_active(void);

#else

static __inline void opensauce_postprocess_tags_loaded(void *tag_instances, long tag_count)
{
	(void)tag_instances;
	(void)tag_count;
}
static __inline void opensauce_postprocess_map_unloaded(void) { }
#define opensauce_postprocess_render_stage(stage, frustum, x0, y0, x1, y1) ((void)0)

#endif

#endif
