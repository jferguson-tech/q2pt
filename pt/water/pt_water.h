/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Jonathan Ferguson */
/*
A height field wave simulation for one flat body of liquid, and the two
pictures a renderer needs from it: which way the surface tilts, and where
the waves gather light on whatever lies below (caustics).

It knows nothing about any renderer. The host steps it, pokes it where
things touch the surface, and hands the pictures on as textures.
*/
#ifndef PT_WATER_H
#define PT_WATER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pt_water_s pt_water_t;

/* slopes are stored as slope * PT_WATER_SLOPE_SCALE + 0.5, clamped to 0-1 */
#define PT_WATER_SLOPE_SCALE	1.0f
/* heights are stored as height / (2 * PT_WATER_HEIGHT_MAX) + 0.5, clamped to
   0-1, in sixteen bits: B the upper eight, A the lower */
#define PT_WATER_HEIGHT_MAX		8.0f
/* caustic brightness is stored as brightness / PT_WATER_CAUSTIC_MAX */
#define PT_WATER_CAUSTIC_MAX	4.0f

/* covers the rectangle in cells of about cell_size; at most max_cells a side */
pt_water_t *pt_water_create(float min_x, float min_y, float max_x, float max_y, float cell_size, int max_cells);
void pt_water_destroy(pt_water_t *w);

int pt_water_width(const pt_water_t *w);
int pt_water_height(const pt_water_t *w);
float pt_water_cell(const pt_water_t *w);	/* the size the cells came out */

/*
Says where in the rectangle there is liquid. Until the first call all of it
is; after that only what the triangles given so far cover, and waves bounce
off the rest as off a bank.
*/
void pt_water_cover(pt_water_t *w, const float a[2], const float b[2], const float c[2]);

/* push the surface down around a point: amount is in units of height */
void pt_water_disturb(pt_water_t *w, float x, float y, float radius, float amount);

/*
Advance by dt seconds. speed is how fast waves travel, in units per second;
damping is the share of a wave's motion lost per second (0-1).
*/
void pt_water_step(pt_water_t *w, float dt, float speed, float damping);

/*
The pictures, width * height pixels of R,G,B,A bytes, row 0 at min_y.
waves: R and G are the slopes in x and y, B and A the height.
caustics: R, G and B are how much the waves brighten the light going
through at that point, 1 meaning unchanged; `strength` scales the effect.
A is 255 where there is liquid, there or in a cell next to it, and else 0.
Both stay valid until the next call that changes the simulation.
*/
const uint32_t *pt_water_waves(pt_water_t *w, float wave_scale);
/*
How far above level the surface stands at a point, as the wave picture made
with this wave_scale has it. Returns 0 where there is no liquid; covered,
if given, says whether there is.
*/
float pt_water_height_at(const pt_water_t *w, float x, float y, float wave_scale, int *covered);
/* how far the surface stood from level, at most, in the picture last made */
float pt_water_reach(const pt_water_t *w);
const uint32_t *pt_water_caustics(pt_water_t *w, float strength);

#ifdef __cplusplus
}
#endif

#endif
