/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2026 Jonathan Ferguson

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// rpt_offline.c -- frames for a film, not for play
//
// While pt_offline is set (the client does that while it renders a demo,
// see client/cl_render.c) every frame is made with that many paths a pixel
// at full resolution, from nothing: no light is carried over from the frame
// before, so nothing can trail or lag. Each is saved as a PNG into
// pt_offline_dir.
//
// With pt_render_blur the paths are spread over the time the frame covers,
// each path seeing from where the eye was at a moment of its own, which
// blurs the picture as a camera's open shutter does when it is turned or
// carried. What moves in the picture is where it is as the shutter closes:
// its own motion is not blurred yet.
//
// With pt_render_live none of that is done: each frame is the one the game
// would have shown, made with the settings it is played with and with what
// it carries over from the frames before, and only saved. Stepped at the
// rate a renderer reaches when played, that is a film of how it plays.

#include "rpt_local.h"
#include "../pt/png/pt_png.h"

#define	PATHS_PER_PASS	4
#define	BLUR_PATHS_PER_PASS	2	// blurred: a pixel sees from as many moments as there are
								// passes, 32 at 64 paths, in about the time the frame took
								// before; 1 is finer grained and takes 1.4 to 1.7 times as long

static cvar_t	*pt_offline;			// paths a pixel; 0 = playing as usual
static cvar_t	*pt_offline_dir;
static cvar_t	*pt_render_blur;		// share of the frame's time the shutter is open, 0-1
static cvar_t	*pt_render_hud;			// the status bar and messages are in the pictures
static cvar_t	*pt_render_bounces;
static cvar_t	*pt_render_light_samples;
static cvar_t	*pt_render_fog;
static cvar_t	*pt_render_live;		// save the game's own picture, as it is played

static qboolean	off_active;
static int		off_frame;				// number of the next picture
static char		off_dir[MAX_OSPATH];
static qboolean	off_failed;

static uint32_t	*off_pixels;			// the view just made, without the overlay
static qboolean	off_have_pixels;
static float	*off_sum;				// blurred: linear light summed over the passes
static float	*off_positions;			// exporting blurred frames: the scene at one moment
static int		off_num_positions;

// where the eye was a frame ago, for blur
static qboolean	off_have_last;
static int		off_last_frame;
static vec3_t	off_last_origin, off_last_axis[3];
static float	off_last_time;
static float	off_last_fov[2];

static float	off_to_linear[256];
static byte		off_to_display[4097];

void R_InitOffline (void)
{
	int		i;

	pt_offline = ri.Cvar_Get ("pt_offline", "0", 0);
	pt_offline_dir = ri.Cvar_Get ("pt_offline_dir", "", 0);
	pt_render_blur = ri.Cvar_Get ("pt_render_blur", "0.5", CVAR_ARCHIVE);	// film's 180 degree shutter
	pt_render_hud = ri.Cvar_Get ("pt_render_hud", "1", CVAR_ARCHIVE);
	pt_render_bounces = ri.Cvar_Get ("pt_render_bounces", "6", CVAR_ARCHIVE);
	pt_render_light_samples = ri.Cvar_Get ("pt_render_light_samples", "16", CVAR_ARCHIVE);
	pt_render_fog = ri.Cvar_Get ("pt_render_fog", "1", CVAR_ARCHIVE);
	pt_render_live = ri.Cvar_Get ("pt_render_live", "0", 0);

	// a renderer that starts while a film is being made (the user changed
	// it) carries on with the next picture, it does not begin again
	off_active = false;

	for (i=0 ; i<256 ; i++)
		off_to_linear[i] = pow (i / 255.0, 2.2);
	for (i=0 ; i<=4096 ; i++)
		off_to_display[i] = (byte)(pow (i / 4096.0, 1 / 2.2) * 255.0 + 0.5);
}

void R_ShutdownOffline (void)
{
	free (off_pixels);
	free (off_sum);
	free (off_positions);
	off_pixels = NULL;
	off_sum = NULL;
	off_positions = NULL;
	off_num_positions = 0;
	off_have_pixels = false;
}

/*
===============
R_Offline

Is a film being made? Also notices it starting and stopping.
===============
*/
qboolean R_Offline (void)
{
	qboolean	on;

	on = pt_offline->value >= 1 && pt_offline_dir->string[0];
	if (on && (!off_active || strcmp (off_dir, pt_offline_dir->string)))
	{
		if (strlen (pt_offline_dir->string) >= sizeof(off_dir) - 24)
			return false;
		strcpy (off_dir, pt_offline_dir->string);
		off_frame = 0;
		off_failed = false;
		off_have_last = false;
	}
	off_active = on;
	return on && !off_failed;
}

/*
===============
R_OfflineSettings

A film gets the best of everything, whatever the game is played with
===============
*/
void R_OfflineSettings (pt_view_t *view)
{
	if (pt_render_live->value)
		return;		// as it is played

	view->scale = 1;
	view->samples = PATHS_PER_PASS;
	view->antialias = 1;
	view->filter = 2;		// as before: offline frames are made of many passes added up
	view->debug = 0;
	view->bounces = pt_render_bounces->value;
	view->light_samples = pt_render_light_samples->value;
	view->reflections = 2;
	view->reflection_bounces = 0;
	view->reflection_rate = 1;
	view->refraction = 1;
	view->texture_filter = 1;
	view->fog = pt_render_fog->value != 0;
}

static void Off_Normalize (vec3_t v)
{
	VectorNormalize (v);
}

/*
===============
R_OfflineMomentsBegin

Room for the scene as it was at a moment between the last frame and this
===============
*/
void R_OfflineMomentsBegin (const pt_view_t *view)
{
	const pt_scene_t	*scene = view->scene;

	if (scene && scene->prev_positions && scene->num_triangles > off_num_positions)
	{
		free (off_positions);
		off_num_positions = scene->num_triangles + 1024;
		off_positions = malloc (off_num_positions * 9 * sizeof(float));
		if (!off_positions)
			off_num_positions = 0;
	}
}

/*
===============
R_OfflineMoment

The view at a moment between the last frame (t = 0) and this one (t = 1):
the eye and everything that moves are put where they were then. Each moment
is a picture of its own, made from nothing.
===============
*/
void R_OfflineMoment (const pt_view_t *view, float t, pt_view_t *moment, pt_scene_t *moment_scene)
{
	const pt_scene_t	*scene = view->scene;
	float		w;
	int			i;

	*moment = *view;
	moment->restart = 1;
	moment->time = off_last_time + (view->time - off_last_time) * t;
	for (i=0 ; i<3 ; i++)
	{
		moment->origin[i] = off_last_origin[i] + (view->origin[i] - off_last_origin[i]) * t;
		moment->forward[i] = off_last_axis[0][i] + (view->forward[i] - off_last_axis[0][i]) * t;
		moment->right[i] = off_last_axis[1][i] + (view->right[i] - off_last_axis[1][i]) * t;
	}
	// square it up again
	Off_Normalize (moment->forward);
	w = DotProduct (moment->right, moment->forward);
	VectorMA (moment->right, -w, moment->forward, moment->right);
	Off_Normalize (moment->right);
	CrossProduct (moment->right, moment->forward, moment->up);

	if (scene && scene->prev_positions && off_positions)
	{
		const float	*now = scene->positions, *was = scene->prev_positions;

		for (i=0 ; i<scene->num_triangles*9 ; i++)
			off_positions[i] = was[i] + (now[i] - was[i]) * t;
		*moment_scene = *scene;
		moment_scene->positions = off_positions;
		moment_scene->prev_positions = NULL;
		moment->scene = moment_scene;
	}
}

/*
===============
R_OfflineRender

Makes the frame, in as many passes as its paths take
===============
*/
void R_OfflineRender (refdef_t *fd, pt_view_t *view)
{
	pt_view_t	moment;
	pt_camera_t	last;
	float		blur, t, w, *sum;
	int			paths, passes, size, i, k, x, y;
	qboolean	blurred, follows;

	size = rpt.width * rpt.height;
	if (!off_pixels)
		off_pixels = malloc (size * sizeof(uint32_t));
	off_have_pixels = false;
	if (!off_pixels)
		return;

	if (pt_render_live->value)
	{	// the frame the game would have shown; R_OfflineFinish reads it back
		rpt.backend->render_view (rpt.backend, view);
		return;
	}

	paths = (int)pt_offline->value;
	passes = (paths + PATHS_PER_PASS - 1) / PATHS_PER_PASS;
	if (passes < 1)
		passes = 1;

	blur = pt_render_blur->value;
	if (blur > 1)
		blur = 1;
	// nothing to blur between if the frame before was not the one before
	// this in time, or the eye was somewhere else altogether (a new map, a
	// teleporter)
	follows = off_have_last && off_last_frame == off_frame - 1
		&& fabs (fd->vieworg[0] - off_last_origin[0]) + fabs (fd->vieworg[1] - off_last_origin[1])
			+ fabs (fd->vieworg[2] - off_last_origin[2]) < 256;
	blurred = blur > 0 && follows;

	if (R_Exporting ())
	{
		// the buffers a denoiser outside the game works from, in place of a picture
		VectorCopy (off_last_origin, last.origin);
		VectorCopy (off_last_axis[0], last.forward);
		VectorCopy (off_last_axis[1], last.right);
		VectorCopy (off_last_axis[2], last.up);
		last.fov_x = off_last_fov[0];
		last.fov_y = off_last_fov[1];
		if (!R_ExportRender (view, paths, blurred ? blur : 0, follows ? &last : NULL))
			off_failed = true;
	}
	else if (!blurred)
	{
		// from one moment: every pass adds to the last, and the backend
		// keeps what it has gathered as it does for a view at rest
		for (i=0 ; i<passes ; i++)
		{
			view->restart = i == 0;
			rpt.backend->render_view (rpt.backend, view);
		}
		off_have_pixels = rpt.backend->read_pixels (rpt.backend, off_pixels, 0) != 0;
	}
	else
	{
		// Every pass is a picture of its own, made from nothing, each pixel
		// of it seen from where the eye was at a moment of that pixel's own
		// while the shutter was open; the passes are added up as the light
		// they are. The pixels of one pass are at different moments, so
		// nothing of one pass can be looked up in the next.
		passes = (paths + BLUR_PATHS_PER_PASS - 1) / BLUR_PATHS_PER_PASS;
		if (passes < 1)
			passes = 1;
		if (!off_sum)
			off_sum = malloc (size * 3 * sizeof(float));
		if (!off_sum)
			return;
		memset (off_sum, 0, size * 3 * sizeof(float));

		moment = *view;
		moment.restart = 1;
		moment.samples = BLUR_PATHS_PER_PASS;
		moment.blur = 1;
		// 1 is now, when the shutter closes, 0 the frame before: it opened at
		// 1 - blur, with the eye on its straight way from there to here
		t = 1 - blur;
		for (i=0 ; i<3 ; i++)
		{
			moment.open_origin[i] = off_last_origin[i] + (view->origin[i] - off_last_origin[i]) * t;
			moment.open_forward[i] = off_last_axis[0][i] + (view->forward[i] - off_last_axis[0][i]) * t;
			moment.open_right[i] = off_last_axis[1][i] + (view->right[i] - off_last_axis[1][i]) * t;
		}
		// square it up again
		Off_Normalize (moment.open_forward);
		w = DotProduct (moment.open_right, moment.open_forward);
		VectorMA (moment.open_right, -w, moment.open_forward, moment.open_right);
		Off_Normalize (moment.open_right);
		CrossProduct (moment.open_right, moment.open_forward, moment.open_up);

		for (k=0 ; k<passes ; k++)
		{
			rpt.backend->render_view (rpt.backend, &moment);
			if (!rpt.backend->read_pixels (rpt.backend, off_pixels, 0))
				return;
			for (i=0, sum=off_sum ; i<size ; i++, sum+=3)
			{
				sum[0] += off_to_linear[off_pixels[i] & 255];
				sum[1] += off_to_linear[(off_pixels[i] >> 8) & 255];
				sum[2] += off_to_linear[(off_pixels[i] >> 16) & 255];
			}
		}

		w = 4096.0f / passes;
		for (i=0, sum=off_sum ; i<size ; i++, sum+=3)
			off_pixels[i] = (uint32_t)off_to_display[(int)(sum[0] * w)]
				| ((uint32_t)off_to_display[(int)(sum[1] * w)] << 8)
				| ((uint32_t)off_to_display[(int)(sum[2] * w)] << 16) | 0xff000000;
		off_have_pixels = true;

		// the backend only has the last moment to show: put the whole
		// picture over it, under whatever the game draws next
		Draw_Touch (fd->x, fd->y, fd->x + fd->width, fd->y + fd->height);
		for (y=fd->y ; y<fd->y+fd->height && y<rpt.height ; y++)
		{
			if (y < 0)
				continue;
			for (x=fd->x ; x<fd->x+fd->width && x<rpt.width ; x++)
				if (x >= 0)
					rpt.overlay[y * rpt.width + x] = off_pixels[y * rpt.width + x];
		}
	}

	off_have_last = true;
	off_last_frame = off_frame;
	VectorCopy (view->origin, off_last_origin);
	VectorCopy (view->forward, off_last_axis[0]);
	VectorCopy (view->right, off_last_axis[1]);
	VectorCopy (view->up, off_last_axis[2]);
	off_last_fov[0] = view->fov_x;
	off_last_fov[1] = view->fov_y;
	off_last_time = view->time;
}

/*
===============
R_OfflineFinish

After the frame has been shown: save it
===============
*/
void R_OfflineFinish (void)
{
	char	path[MAX_OSPATH];
	int		size;

	if (!R_Offline ())
		return;

	if (R_Exporting ())
	{
		Com_sprintf (path, sizeof(path), "%s/frame%05d.ptx", off_dir, off_frame);
		if (!R_ExportWrite (path, off_frame))
		{
			ri.Con_Printf (PRINT_ALL, "Couldn't write %s\n", path);
			off_failed = true;
			return;
		}
		off_have_pixels = false;
		off_frame++;
		return;
	}

	size = rpt.width * rpt.height;
	if (!off_pixels)
		off_pixels = malloc (size * sizeof(uint32_t));
	if (!off_pixels)
		return;

	// with the status bar, or with no view drawn this frame (between maps),
	// the picture is the window as it stands
	if (pt_render_hud->value || !off_have_pixels)
	{
		if (!rpt.backend->read_pixels (rpt.backend, off_pixels, pt_render_hud->value || !pt_render_live->value))
		{
			ri.Con_Printf (PRINT_ALL, "This renderer cannot save its frames yet.\n");
			off_failed = true;
			return;
		}
	}
	off_have_pixels = false;

	Com_sprintf (path, sizeof(path), "%s/frame%05d.png", off_dir, off_frame);
	if (!pt_png_write (path, off_pixels, rpt.width, rpt.height))
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't write %s\n", path);
		off_failed = true;
		return;
	}
	off_frame++;
}
