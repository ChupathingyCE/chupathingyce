/*
H2_BLOOM.H: Halo 2's bloom (h2_bloom.c).
*/

#ifndef __H2_BLOOM_H
#define __H2_BLOOM_H

/* (the built sky tag's padding: 'H2BL', then the Halo 2 sky's BLOOM OVERRIDE
amount, threshold, brightness and gamma power) */
#define H2_BLOOM_SKY_OFFSET 0x20

/* (a window's 3D done, its interface not begun: render.c) */
void h2_bloom_render(void);

/* (the scripts' rasterizer_bloom_override*) */
void h2_bloom_override_enable(boolean on);
void h2_bloom_override_reset(void);
void h2_bloom_override_threshold(float value);
void h2_bloom_override_brightness(float value);

#endif /* __H2_BLOOM_H */
