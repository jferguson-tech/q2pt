/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Jonathan Ferguson */
#ifndef PT_H
#define PT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pt_log_fn)(const char *msg);

typedef struct pt_create_s
{
	void		*hinstance;		/* HINSTANCE of the host */
	void		*hwnd;			/* HWND to present into */
	int			width, height;	/* client area in pixels */
	pt_log_fn	log;			/* may be NULL */
} pt_create_t;

/*
World description. Right handed, any consistent length unit. Colours in
textures are display referred (sRGB-like); emission, light intensity and sky
scale are linear radiometric values in one consistent unit.

Triangles are one sided for emission: they emit from the side where the
vertices appear counter clockwise.
*/

#define PT_MAT_SKY		1u		/* shows the sky; nothing else applies */

typedef struct pt_texture_s
{
	int				width, height;
	const uint32_t	*pixels;	/* R,G,B,A bytes, top row first; alpha ignored */
} pt_texture_t;

typedef struct pt_material_s
{
	int			texture;		/* index into textures, -1 for plain white */
	float		emission[3];	/* average emitted radiance. With a texture the
								   radiance at a point is this times
								   texel / average texel */
	float		alpha;			/* 1 = opaque; less lets light through */
	uint32_t	flags;
} pt_material_t;

typedef struct pt_point_light_s
{
	float		origin[3];
	float		intensity[3];	/* radiant intensity; irradiance is this * cos / d^2 */
} pt_point_light_t;

typedef struct pt_world_s
{
	const pt_texture_t	*textures;
	int					num_textures;
	const pt_material_t	*materials;
	int					num_materials;

	const float			*positions;			/* 3 per vertex */
	const float			*uvs;				/* 2 per vertex, 1.0 = one texture repeat */
	int					num_vertices;
	const uint32_t		*indices;			/* 3 per triangle */
	const uint32_t		*tri_materials;		/* 1 per triangle */
	int					num_triangles;

	const pt_point_light_t	*lights;
	int						num_lights;

	/*
	Sky cube, or -1s for a black sky. Face order +X -X +Y -Y +Z -Z. For the
	face on axis a, with b and c the next two axes in cyclic order (X: Y,Z
	Y: Z,X  Z: X,Y), a direction d maps to
		u = (d[b] / |d[a]| + 1) / 2,  v = (d[c] / |d[a]| + 1) / 2
	with v = 0 at the texture's top row.
	*/
	int					sky_textures[6];
	float				sky_scale;			/* radiance of a white sky texel */
} pt_world_t;

/* one 3D view */
typedef struct pt_view_s
{
	/* the part of the window it covers, in pixels from the top left */
	int		x, y, width, height;
	float	time;

	float	origin[3];
	float	forward[3], right[3], up[3];	/* orthonormal */
	float	fov_x, fov_y;					/* degrees */

	/* quality settings; a backend may ignore what it has no use for */
	float	scale;			/* internal resolution as a fraction of the view */
	int		samples;		/* paths per pixel per frame */
	int		bounces;		/* maximum path length after the first hit */
	float	exposure;
} pt_view_t;

/*
A backend owns everything between "here is the scene" and pixels on screen.

One frame is: an optional render_view, then exactly one present. Anything
outside the view, and the whole window when render_view was not called, is
black under the overlay.

The overlay is width*height pixels, bytes R,G,B,A in memory, premultiplied
alpha, top row first. It is composited over the view.
*/
typedef struct pt_backend_s pt_backend_t;
struct pt_backend_s
{
	const char	*name;
	void	(*destroy)(pt_backend_t *self);

	/* copies everything it needs; NULL unloads the world */
	void	(*load_world)(pt_backend_t *self, const pt_world_t *world);

	void	(*render_view)(pt_backend_t *self, const pt_view_t *view);
	void	(*present)(pt_backend_t *self, const uint32_t *overlay);

	/* one line about the last frame, valid until the next call */
	const char *(*stats)(pt_backend_t *self);
};

/* both return NULL on failure with a reason in err */
pt_backend_t *pt_cpu_create(const pt_create_t *ci, char *err, int errlen);
pt_backend_t *pt_rtx_create(const pt_create_t *ci, char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif
