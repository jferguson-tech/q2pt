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
// rpt_draw.c -- 2D drawing into the overlay. Both path tracers composite the
// overlay over their image, so everything here is premultiplied RGBA.

#include "rpt_local.h"

static image_t	*draw_chars;
static uint32_t	r_rawpalette[256];

/*
=============================================================================

Which parts of the overlay hold anything

Most of the overlay is empty most of the time: a status bar, a crosshair and
a few lines of text on a picture of millions of pixels. Clearing all of it
every frame and handing all of it to the backend cost more than drawing on
it, so note is kept, in squares of OV_TILE pixels, of where anything was
drawn. Only those are cleared for the next frame, and the backend is told
which may have changed: those drawn on now and those drawn on last time.

=============================================================================
*/

#define	OV_TILE		64

static byte			*ov_now, *ov_was;		// one for each square
static pt_rect_t	*ov_rects;
static int			ov_cols, ov_rows;
static uint32_t		*ov_pixels;				// the overlay these are about
static int			ov_width, ov_height;
static qboolean		ov_all;					// nothing is known of what the backend has

static qboolean Draw_SameOverlay (void)
{
	return ov_pixels == rpt.overlay && ov_width == rpt.width && ov_height == rpt.height;
}

// the overlay is not the one the notes are about: a new mode, a new renderer
static void Draw_NewOverlay (void)
{
	free (ov_now);
	free (ov_was);
	free (ov_rects);
	ov_pixels = rpt.overlay;
	ov_width = rpt.width;
	ov_height = rpt.height;
	ov_cols = (ov_width + OV_TILE - 1) / OV_TILE;
	ov_rows = (ov_height + OV_TILE - 1) / OV_TILE;
	ov_now = calloc (ov_cols * ov_rows, 1);
	ov_was = calloc (ov_cols * ov_rows, 1);
	ov_rects = malloc (ov_cols * ov_rows * sizeof(ov_rects[0]));
	ov_all = true;
}

/*
=============
Draw_Touch

Something is being drawn from x0, y0 up to but not including x1, y1
=============
*/
void Draw_Touch (int x0, int y0, int x1, int y1)
{
	int		tx, ty, tx1, ty1;

	if (!Draw_SameOverlay ())
		Draw_NewOverlay ();

	if (x0 < 0)
		x0 = 0;
	if (y0 < 0)
		y0 = 0;
	if (x1 > ov_width)
		x1 = ov_width;
	if (y1 > ov_height)
		y1 = ov_height;
	if (x0 >= x1 || y0 >= y1)
		return;

	tx1 = (x1 - 1) / OV_TILE;
	ty1 = (y1 - 1) / OV_TILE;
	for (ty=y0/OV_TILE ; ty<=ty1 ; ty++)
		for (tx=x0/OV_TILE ; tx<=tx1 ; tx++)
			ov_now[ty * ov_cols + tx] = 1;
}

/*
=============
Draw_ClearOverlay

Before a frame's drawing: empties what the frame before drew on
=============
*/
void Draw_ClearOverlay (void)
{
	byte	*swap;
	int		tx, ty, x0, x1, y, y1;

	if (!Draw_SameOverlay ())
	{
		Draw_NewOverlay ();
		memset (rpt.overlay, 0, rpt.width * rpt.height * sizeof(uint32_t));
		return;
	}

	for (ty=0 ; ty<ov_rows ; ty++)
	{
		y1 = (ty + 1) * OV_TILE < ov_height ? (ty + 1) * OV_TILE : ov_height;
		for (tx=0 ; tx<ov_cols ; tx++)
		{
			if (!ov_now[ty * ov_cols + tx])
				continue;
			// a run of squares side by side is cleared as one
			x0 = tx * OV_TILE;
			while (tx + 1 < ov_cols && ov_now[ty * ov_cols + tx + 1])
				tx++;
			x1 = (tx + 1) * OV_TILE < ov_width ? (tx + 1) * OV_TILE : ov_width;
			for (y=ty*OV_TILE ; y<y1 ; y++)
				memset (rpt.overlay + y * ov_width + x0, 0, (x1 - x0) * sizeof(uint32_t));
		}
	}

	swap = ov_was;
	ov_was = ov_now;
	ov_now = swap;
	memset (ov_now, 0, ov_cols * ov_rows);
}

/*
=============
Draw_Changed

After a frame's drawing: the parts of the overlay that may not be as they
were when this was last asked. Returns how many, or -1 for all of it.
=============
*/
int Draw_Changed (pt_rect_t **rects)
{
	pt_rect_t	*r;
	int			tx, ty, x0, x1, y0, y1, num, i;

	if (!Draw_SameOverlay ())
		Draw_NewOverlay ();
	*rects = ov_rects;
	if (ov_all)
	{
		ov_all = false;
		return -1;
	}

	num = 0;
	for (ty=0 ; ty<ov_rows ; ty++)
	{
		y0 = ty * OV_TILE;
		y1 = y0 + OV_TILE < ov_height ? y0 + OV_TILE : ov_height;
		for (tx=0 ; tx<ov_cols ; tx++)
		{
			if (!ov_now[ty * ov_cols + tx] && !ov_was[ty * ov_cols + tx])
				continue;
			x0 = tx * OV_TILE;
			while (tx + 1 < ov_cols && (ov_now[ty * ov_cols + tx + 1] || ov_was[ty * ov_cols + tx + 1]))
				tx++;
			x1 = (tx + 1) * OV_TILE < ov_width ? (tx + 1) * OV_TILE : ov_width;

			// the same run in the row above makes one taller rectangle
			for (i=num-1 ; i>=0 ; i--)
				if (ov_rects[i].x == x0 && ov_rects[i].width == x1 - x0 && ov_rects[i].y + ov_rects[i].height == y0)
					break;
			if (i >= 0)
			{
				ov_rects[i].height += y1 - y0;
				continue;
			}

			r = &ov_rects[num++];
			r->x = x0;
			r->y = y0;
			r->width = x1 - x0;
			r->height = y1 - y0;
		}
	}
	return num;
}

/*
===============
Draw_InitLocal
===============
*/
void Draw_InitLocal (void)
{
	draw_chars = Draw_FindPic ("conchars");
	if (!draw_chars)
		ri.Sys_Error (ERR_FATAL, "Couldn't load pics/conchars.pcx");
	R_SetPalette (NULL);
}

/*
=============
Draw_FindPic
=============
*/
image_t *Draw_FindPic (char *name)
{
	char	fullname[MAX_QPATH];

	if (name[0] != '/' && name[0] != '\\')
	{
		Com_sprintf (fullname, sizeof(fullname), "pics/%s.pcx", name);
		return R_FindImage (fullname, it_pic);
	}
	return R_FindImage (name+1, it_pic);
}

/*
=============
Draw_GetPicSize
=============
*/
void Draw_GetPicSize (int *w, int *h, char *pic)
{
	image_t *image;

	image = Draw_FindPic (pic);
	if (!image)
	{
		*w = *h = -1;
		return;
	}
	*w = image->width;
	*h = image->height;
}

/*
=============
Blend

source over destination, both premultiplied
=============
*/
static uint32_t Blend (uint32_t src, uint32_t dst)
{
	unsigned	ia = 255 - (src >> 24);
	uint32_t	rb, ga;

	rb = (((dst & 0x00ff00ff) * ia + 0x00800080) >> 8) & 0x00ff00ff;
	ga = ((((dst >> 8) & 0x00ff00ff) * ia + 0x00800080) >> 8) & 0x00ff00ff;
	return src + (rb | (ga << 8));
}

/*
=============
Draw_Scaled

Nearest neighbour copy of a source rectangle to a screen rectangle.
src is 32 bit premultiplied, or 8 bit through pal when pal is given.
=============
*/
static void Draw_Scaled (int x, int y, int w, int h,
	const void *src, int sw, int sh, const uint32_t *pal)
{
	int			x0, y0, x1, y1, dx, dy, sy, sx;
	uint32_t	*dest, c;

	if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
		return;

	x0 = x < 0 ? 0 : x;
	y0 = y < 0 ? 0 : y;
	x1 = x + w > rpt.width ? rpt.width : x + w;
	y1 = y + h > rpt.height ? rpt.height : y + h;
	Draw_Touch (x0, y0, x1, y1);

	for (dy=y0 ; dy<y1 ; dy++)
	{
		sy = (int)((int64_t)(dy - y) * sh / h);
		dest = rpt.overlay + dy * rpt.width;

		for (dx=x0 ; dx<x1 ; dx++)
		{
			sx = (w == sw) ? dx - x : (int)((int64_t)(dx - x) * sw / w);
			if (pal)
				c = pal[((const byte *)src)[sy * sw + sx]];
			else
				c = ((const uint32_t *)src)[sy * sw + sx];

			if (c >> 24 == 255)
				dest[dx] = c;
			else if (c >> 24)
				dest[dx] = Blend (c, dest[dx]);
		}
	}
}

/*
=============
Draw_StretchPic
=============
*/
void Draw_StretchPic (int x, int y, int w, int h, char *name)
{
	image_t *image;

	image = Draw_FindPic (name);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}
	Draw_Scaled (x, y, w, h, image->pixels, image->width, image->height, NULL);
}

/*
=============
Draw_Pic
=============
*/
void Draw_Pic (int x, int y, char *name)
{
	image_t *image;

	image = Draw_FindPic (name);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}
	Draw_Scaled (x, y, image->width, image->height, image->pixels, image->width, image->height, NULL);
}

/*
================
Draw_Char

Draws one 8*8 graphics character
It can be clipped to the top of the screen to allow the console to be
smoothly scrolled off.
================
*/
void Draw_Char (int x, int y, int num)
{
	int			row, col, dx, dy, sx, sy;
	uint32_t	c;

	num &= 255;

	if ((num & 127) == 32)
		return;		// space
	if (y <= -8)
		return;		// totally off screen
	if (draw_chars->width < 128 || draw_chars->height < 128)
		return;

	row = (num >> 4) * 8;
	col = (num & 15) * 8;
	Draw_Touch (x, y, x + 8, y + 8);

	for (sy=0 ; sy<8 ; sy++)
	{
		dy = y + sy;
		if (dy < 0 || dy >= rpt.height)
			continue;
		for (sx=0 ; sx<8 ; sx++)
		{
			dx = x + sx;
			if (dx < 0 || dx >= rpt.width)
				continue;
			c = draw_chars->pixels[(row + sy) * draw_chars->width + col + sx];
			if (c >> 24)
				rpt.overlay[dy * rpt.width + dx] = c;
		}
	}
}

/*
================
Draw_String
================
*/
void Draw_String (int x, int y, const char *s)
{
	for ( ; *s ; s++, x += 8)
		Draw_Char (x, y, (byte)*s);
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h, char *name)
{
	image_t		*image;
	int			x1, y1, dx, dy;
	uint32_t	*dest, *srow;

	image = Draw_FindPic (name);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "Can't find pic: %s\n", name);
		return;
	}

	x1 = x + w > rpt.width ? rpt.width : x + w;
	y1 = y + h > rpt.height ? rpt.height : y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	Draw_Touch (x, y, x1, y1);
	for (dy=y ; dy<y1 ; dy++)
	{
		dest = rpt.overlay + dy * rpt.width;
		srow = image->pixels + (dy % image->height) * image->width;
		for (dx=x ; dx<x1 ; dx++)
			dest[dx] = srow[dx % image->width] | 0xff000000;
	}
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c)
{
	int			x1, y1, dx, dy;
	uint32_t	*dest, color;

	color = d_8to24table[c & 255] | 0xff000000;

	x1 = x + w > rpt.width ? rpt.width : x + w;
	y1 = y + h > rpt.height ? rpt.height : y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	Draw_Touch (x, y, x1, y1);
	for (dy=y ; dy<y1 ; dy++)
	{
		dest = rpt.overlay + dy * rpt.width;
		for (dx=x ; dx<x1 ; dx++)
			dest[dx] = color;
	}
}

//=============================================================================

/*
================
Draw_FadeScreen

Darkens whatever is on screen, as black at 80% over everything so far
================
*/
void Draw_FadeScreen (void)
{
	const uint32_t	black = 204u << 24;
	int				i, count;

	Draw_Touch (0, 0, rpt.width, rpt.height);
	count = rpt.width * rpt.height;
	for (i=0 ; i<count ; i++)
		rpt.overlay[i] = Blend (black, rpt.overlay[i]);
}

/*
================
Draw_FadeBox

Darkens a box, as black at 60%, for text to be read against
================
*/
void Draw_FadeBox (int x, int y, int w, int h)
{
	const uint32_t	black = 153u << 24;
	int				x1, y1, dx, dy;

	x1 = x + w > rpt.width ? rpt.width : x + w;
	y1 = y + h > rpt.height ? rpt.height : y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	Draw_Touch (x, y, x1, y1);
	for (dy=y ; dy<y1 ; dy++)
		for (dx=x ; dx<x1 ; dx++)
			rpt.overlay[dy * rpt.width + dx] = Blend (black, rpt.overlay[dy * rpt.width + dx]);
}

//====================================================================

/*
=============
R_SetPalette

Palette for Draw_StretchRaw; NULL selects the game palette
=============
*/
void R_SetPalette (const unsigned char *palette)
{
	int		i;

	for (i=0 ; i<256 ; i++)
	{
		if (palette)
			r_rawpalette[i] = (uint32_t)palette[i*3] | ((uint32_t)palette[i*3+1] << 8)
				| ((uint32_t)palette[i*3+2] << 16) | 0xff000000;
		else
			r_rawpalette[i] = d_8to24table[i] | 0xff000000;
	}
}

/*
=============
Draw_StretchRaw

Cinematic frames
=============
*/
void Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data)
{
	Draw_Scaled (x, y, w, h, data, cols, rows, r_rawpalette);
}

/*
=============
Draw_Blend

Tints a rectangle with an rgba colour, 0-1 each
=============
*/
void Draw_Blend (int x, int y, int w, int h, float *blend)
{
	int			x1, y1, dx, dy, a;
	uint32_t	*dest, color;

	a = blend[3] * 255;
	if (a <= 0)
		return;
	if (a > 255)
		a = 255;

	// premultiplied
	color = (uint32_t)(blend[0] * a) | ((uint32_t)(blend[1] * a) << 8)
		| ((uint32_t)(blend[2] * a) << 16) | ((uint32_t)a << 24);

	x1 = x + w > rpt.width ? rpt.width : x + w;
	y1 = y + h > rpt.height ? rpt.height : y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;

	Draw_Touch (x, y, x1, y1);
	for (dy=y ; dy<y1 ; dy++)
	{
		dest = rpt.overlay + dy * rpt.width;
		for (dx=x ; dx<x1 ; dx++)
			dest[dx] = Blend (color, dest[dx]);
	}
}
