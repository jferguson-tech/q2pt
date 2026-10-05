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

/* the part of the window the 3D view covers, in pixels from the top left */
typedef struct pt_view_s
{
	int		x, y, width, height;
	float	time;
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
	void	(*render_view)(pt_backend_t *self, const pt_view_t *view);
	void	(*present)(pt_backend_t *self, const uint32_t *overlay);
};

/* both return NULL on failure with a reason in err */
pt_backend_t *pt_cpu_create(const pt_create_t *ci, char *err, int errlen);
pt_backend_t *pt_rtx_create(const pt_create_t *ci, char *err, int errlen);

#ifdef __cplusplus
}
#endif

#endif
