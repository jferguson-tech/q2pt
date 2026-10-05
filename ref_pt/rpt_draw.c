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

	for (dy=y ; dy<y1 ; dy++)
	{
		dest = rpt.overlay + dy * rpt.width;
		for (dx=x ; dx<x1 ; dx++)
			dest[dx] = Blend (color, dest[dx]);
	}
}
