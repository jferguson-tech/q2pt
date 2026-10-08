/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Jonathan Ferguson */
/*
Makes, from nothing but the colours of a texture, what a path tracer wants
to know about the surface and old game art never recorded: which way its
small details face, and how rough and how metallic it is from place to place.

The art it is meant for was painted by hand in a few hundred colours, with
its own light in it: raised edges were given a highlight on the side towards
the top left of the picture and a shadow on the other. That painted light is
the best evidence of shape there is, so it is read back as shape. Where
nothing was painted, dark is taken to be deep. Once the shape is known the
painted light is no longer wanted in the colours, where it would light every
raised edge a second time and from the wrong side as often as not, so the
picture can be had back with it taken out.

It knows nothing about any game or renderer, and reads and writes no files.
*/
#ifndef PT_MATERIAL_H
#define PT_MATERIAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
Raised whenever the map made from a given picture changes, so that whoever
keeps maps from one run to the next can tell which were made by an older
reading.
*/
#define PT_MATERIAL_VERSION		2

typedef struct pt_material_from_s
{
	int		repeats;		/* the picture tiles both ways, as a wall's does, so
							   its edges are worked across; 0 for one that
							   stands alone, as a model's skin does */
	int		painted_light;	/* highlights and shadows are read as shape lit from
							   the top left. 0 where the picture's up is not the
							   surface's, as on a skin wrapped round a model:
							   then only "dark is deep" is used */
	float	bump;			/* 1 = relief as read; more is deeper, 0 is flat */
	float	roughness;		/* the material's own, 0 mirror - 1 matte; the
							   picture varies it about this */
	float	metallic;		/* the material's own, 0 - 1: how metallic its bare
							   parts are. The picture says which those are:
							   what is vivid is paint, rust or wood, what is
							   dim is dirt or a gap */
	float	delight;		/* how much of the painted light is taken out of
							   the colours: 0 none, 1 all that was read. Only
							   with painted_light */
} pt_material_from_t;

typedef struct pt_material_maps_s
{
	uint32_t	*detail;	/* pt_material_detail_scale() times as wide and as
							   high as the picture. In each pixel R and G are
							   the x and y of a unit normal, x to the right and
							   y down the picture, each stored as (n + 1) / 2;
							   its z, out of the picture, is what is left of
							   its length. B is how metallic the surface is
							   there and A how rough */
	int			detail_width, detail_height;
	uint32_t	*colour;	/* the picture with its painted light taken out, or
							   NULL where there was none to take: then the
							   picture itself is as good */
	int			colour_width, colour_height;
} pt_material_maps_t;

/*
How many times finer than the picture its detail map is, each way. A picture
that repeats is stretched over whole walls and seen from close by, so its
map is made finer than it is; one that stands alone gets a map of its size.
*/
int pt_material_detail_scale(int width, int height, int repeats);

/*
pixels: width * height of R,G,B,A bytes in memory, top row first, colours
as they are shown (not linear light).

Fills in maps and returns 1; each map is made with malloc for the caller to
free. Returns 0, with both maps NULL, if the picture is too large (over 2048
either way) or there was no memory for it.
*/
int pt_material_read(const uint32_t *pixels, int width, int height, const pt_material_from_t *from, pt_material_maps_t *maps);

/*
The same picture's height as it was read, for looking at: one byte a pixel
at the detail map's size, 128 level, 64 a texel lower. Made with malloc, or
NULL.
*/
unsigned char *pt_material_height(const uint32_t *pixels, int width, int height, const pt_material_from_t *from);

#ifdef __cplusplus
}
#endif

#endif
