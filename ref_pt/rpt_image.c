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
// rpt_image.c -- image loading and the image cache

#include "rpt_local.h"

#define	MAX_RPT_IMAGES	1024

static image_t	r_images[MAX_RPT_IMAGES];
static int		numr_images;

uint32_t	d_8to24table[256];

#define	RGBA(r,g,b,a)	((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))

/*
=================================================================

PCX LOADING

=================================================================
*/

/*
==============
LoadPCX

Returns malloc'd 8 bit pixels, and the 768 byte palette if asked for
==============
*/
static byte *LoadPCX (char *filename, byte *palette, int *width, int *height)
{
	byte	*raw, *end, *out, *pix;
	pcx_t	*pcx;
	int		len, w, h, x, y;
	int		dataByte, runLength;

	len = ri.FS_LoadFile (filename, (void **)&raw);
	if (!raw)
		return NULL;

	pcx = (pcx_t *)raw;
	w = LittleShort (pcx->xmax) + 1;
	h = LittleShort (pcx->ymax) + 1;

	if (len < (int)sizeof(pcx_t) + 768
		|| pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| w <= 0 || h <= 0 || w > 4096 || h > 4096)
	{
		ri.Con_Printf (PRINT_ALL, "Bad pcx file %s\n", filename);
		ri.FS_FreeFile (raw);
		return NULL;
	}

	if (palette)
		memcpy (palette, raw + len - 768, 768);

	out = malloc (w * h);
	pix = out;
	raw = &pcx->data;
	end = (byte *)pcx + len - 768;

	for (y=0 ; y<h ; y++, pix += w)
	{
		for (x=0 ; x<w ; )
		{
			if (raw >= end)
			{	// truncated file: leave the rest black
				memset (pix + x, 0, w - x);
				break;
			}
			dataByte = *raw++;

			if ((dataByte & 0xC0) == 0xC0)
			{
				runLength = dataByte & 0x3F;
				dataByte = raw < end ? *raw++ : 0;
			}
			else
				runLength = 1;

			while (runLength-- > 0 && x < w)
				pix[x++] = dataByte;
		}
	}

	ri.FS_FreeFile (pcx);

	*width = w;
	*height = h;
	return out;
}

static uint32_t *Expand8 (const byte *in, int count)
{
	uint32_t	*out;
	int			i;

	out = malloc (count * sizeof(uint32_t));
	for (i=0 ; i<count ; i++)
		out[i] = d_8to24table[in[i]];
	return out;
}

/*
=================================================================

WAL LOADING

=================================================================
*/

static uint32_t *LoadWal (char *name, int *width, int *height)
{
	miptex_t	*mt;
	uint32_t	*out;
	int			len, w, h, ofs;

	len = ri.FS_LoadFile (name, (void **)&mt);
	if (!mt)
		return NULL;

	w = LittleLong (mt->width);
	h = LittleLong (mt->height);
	ofs = LittleLong (mt->offsets[0]);

	if (len < (int)sizeof(*mt) || w <= 0 || h <= 0 || w > 4096 || h > 4096
		|| ofs < 0 || ofs > len - w * h)
	{
		ri.Con_Printf (PRINT_ALL, "Bad wal file %s\n", name);
		ri.FS_FreeFile (mt);
		return NULL;
	}

	out = Expand8 ((byte *)mt + ofs, w * h);
	ri.FS_FreeFile (mt);

	*width = w;
	*height = h;
	return out;
}

/*
=================================================================

TARGA LOADING

=================================================================
*/

/*
=============
LoadTGA

Uncompressed and RLE true colour, 24 or 32 bit
=============
*/
static uint32_t *LoadTGA (char *name, int *width, int *height)
{
	byte		*raw, *p, *end;
	uint32_t	*out, pixel;
	int			len, w, h, x, y, bpp, type, idlen, run, i;
	qboolean	rle, topdown;

	len = ri.FS_LoadFile (name, (void **)&raw);
	if (!raw)
		return NULL;

	if (len < 18)
	{
		ri.FS_FreeFile (raw);
		return NULL;
	}

	idlen = raw[0];
	type = raw[2];
	w = raw[12] | (raw[13] << 8);
	h = raw[14] | (raw[15] << 8);
	bpp = raw[16];
	topdown = (raw[17] & 0x20) != 0;
	rle = (type == 10);

	if (raw[1] != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32)
		|| w <= 0 || h <= 0 || w > 4096 || h > 4096)
	{
		ri.Con_Printf (PRINT_ALL, "Unsupported tga file %s\n", name);
		ri.FS_FreeFile (raw);
		return NULL;
	}

	p = raw + 18 + idlen;
	end = raw + len;
	out = malloc (w * h * sizeof(uint32_t));
	memset (out, 0, w * h * sizeof(uint32_t));

	pixel = 0;
	run = 0;		// pixels left in the current packet
	i = 0;			// 1 if the current packet repeats one pixel
	for (y=0 ; y<h ; y++)
	{
		uint32_t *row = out + (topdown ? y : h - 1 - y) * w;

		for (x=0 ; x<w ; x++)
		{
			if (rle && !run)
			{
				if (p >= end)
					goto done;
				i = (*p & 0x80) != 0;
				run = (*p++ & 0x7f) + 1;
				if (i)
				{
					if (p + bpp / 8 > end)
						goto done;
					pixel = RGBA (p[2], p[1], p[0], bpp == 32 ? p[3] : 255);
					p += bpp / 8;
				}
			}
			if (!rle || !i)
			{
				if (p + bpp / 8 > end)
					goto done;
				pixel = RGBA (p[2], p[1], p[0], bpp == 32 ? p[3] : 255);
				p += bpp / 8;
			}
			if (rle)
				run--;
			row[x] = pixel;
		}
	}
done:
	ri.FS_FreeFile (raw);

	// premultiply
	for (i=0 ; i<w*h ; i++)
	{
		unsigned a = out[i] >> 24;
		if (a != 255)
			out[i] = RGBA ((out[i] & 0xff) * a / 255, ((out[i] >> 8) & 0xff) * a / 255,
				((out[i] >> 16) & 0xff) * a / 255, a);
	}

	*width = w;
	*height = h;
	return out;
}

//=======================================================

/*
===============
R_FindImage

Finds or loads the given image
===============
*/
image_t	*R_FindImage (char *name, imagetype_t type)
{
	image_t		*image;
	int			i, len, width, height;
	uint32_t	*pixels;
	byte		*pic8;

	if (!name)
		return NULL;
	len = strlen (name);
	if (len < 5 || len >= MAX_QPATH)
		return NULL;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (image->registration_sequence && !strcmp (name, image->name))
		{
			image->registration_sequence = registration_sequence;
			return image;
		}
	}

	pixels = NULL;
	if (!strcmp (name+len-4, ".pcx"))
	{
		pic8 = LoadPCX (name, NULL, &width, &height);
		if (pic8)
		{
			pixels = Expand8 (pic8, width * height);
			free (pic8);
		}
	}
	else if (!strcmp (name+len-4, ".wal"))
		pixels = LoadWal (name, &width, &height);
	else if (!strcmp (name+len-4, ".tga"))
		pixels = LoadTGA (name, &width, &height);

	if (!pixels)
		return NULL;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
		if (!image->registration_sequence)
			break;
	if (i == numr_images)
	{
		if (numr_images == MAX_RPT_IMAGES)
			ri.Sys_Error (ERR_DROP, "MAX_RPT_IMAGES");
		numr_images++;
	}

	strcpy (image->name, name);
	image->type = type;
	image->width = width;
	image->height = height;
	image->pixels = pixels;
	image->registration_sequence = registration_sequence;
	return image;
}

/*
===============
R_ImageTexture

The backend's texture for an image, made the first time it is asked for.
Returns -1 if there is none.
===============
*/
int R_ImageTexture (image_t *image)
{
	pt_texture_t	tex;

	if (!image)
		return -1;
	if (!image->pt_texture)
	{
		tex.width = image->width;
		tex.height = image->height;
		tex.pixels = image->pixels;
		image->pt_texture = rpt.backend->texture_create (rpt.backend, &tex) + 1;
	}
	return image->pt_texture - 1;
}

static void R_FreeImage (image_t *image)
{
	if (image->pt_texture && rpt.backend)
		rpt.backend->texture_destroy (rpt.backend, image->pt_texture - 1);
	free (image->pixels);
	memset (image, 0, sizeof(*image));
}

/*
===============
R_RegisterSkin
===============
*/
struct image_s *R_RegisterSkin (char *name)
{
	return R_FindImage (name, it_skin);
}

/*
================
R_FreeUnusedImages

Any image that was not touched on this registration sequence
will be freed.
================
*/
void R_FreeUnusedImages (void)
{
	int		i;
	image_t	*image;

	for (i=0, image=r_images ; i<numr_images ; i++, image++)
	{
		if (!image->registration_sequence)
			continue;		// free slot
		if (image->registration_sequence == registration_sequence)
			continue;		// used this sequence
		if (image->type == it_pic)
			continue;		// don't free pics

		R_FreeImage (image);
	}
}

/*
===============
R_InitImages
===============
*/
void R_InitImages (void)
{
	byte	pal[768], *pic8;
	int		i, width, height;

	registration_sequence = 1;

	pic8 = LoadPCX ("pics/colormap.pcx", pal, &width, &height);
	if (!pic8)
		ri.Sys_Error (ERR_FATAL, "Couldn't load pics/colormap.pcx");
	free (pic8);

	for (i=0 ; i<256 ; i++)
		d_8to24table[i] = RGBA (pal[i*3], pal[i*3+1], pal[i*3+2], 255);
	d_8to24table[255] = 0;	// transparent
}

/*
===============
R_ShutdownImages
===============
*/
void R_ShutdownImages (void)
{
	int		i;

	for (i=0 ; i<numr_images ; i++)
		R_FreeImage (&r_images[i]);
	numr_images = 0;
}
