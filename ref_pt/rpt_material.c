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
// rpt_material.c -- what each texture is made of
//
// The game's art is colour only. The tracer wants to know how rough and how
// metallic a surface is and which way its small details face, so that is
// guessed here from the texture's name and derived from its picture. A
// pt_materials.txt in the game directory overrides the guesses.

#include "rpt_local.h"

typedef struct
{
	char		pattern[48];
	matinfo_t	info;
} matrule_t;

#define	MAX_MATRULES	512

static matrule_t	mat_userrules[MAX_MATRULES];
static int			mat_numuserrules;

// Matched against the texture's file name, without directory or extension.
// The first rule containing a match wins, so the specific come first.
static const struct
{
	const char	*words;		// space separated
	matinfo_t	info;		// roughness, metallic, bump
} mat_walls[] =
{
	{ "wndow window wndw brwind glass", { 0.08f, 0.0f, 0.2f } },
	{ "rock mine cindr cinder cindb geowal sand mud grass brick marble flesh blood dirt stone drag crys pyramid mont rrock", { 0.90f, 0.0f, 1.0f } },
	{ "lava", { 0.70f, 0.0f, 1.2f } },
	{ "comp mon sign num arrow keypad but btn swt exit location caution banner lever", { 0.30f, 0.0f, 0.6f } },
	{ "light lite baselt wslt wstlt redlt ctylt pallt minlt grlt rlight tlight citlit geolit prwlt lsrlt lzr glo", { 0.35f, 0.1f, 0.5f } },
	{ "grate grat wire cable pip duc", { 0.40f, 0.7f, 0.9f } },
	{ "floor flr flor stairs plat", { 0.45f, 0.4f, 0.7f } },
	{ "metal met mtl metl mach support supprt door dr belt tram train turret lead thinm troof slot notch pilr pillar "
	  "core pow pwr fuse shutl timpod tcm box crate ceil tunl hall elev refl", { 0.45f, 0.5f, 0.7f } },
};

// matched against the whole path
static const struct
{
	const char	*word;
	matinfo_t	info;
} mat_models[] =
{
	{ "models/weapons/", { 0.40f, 0.6f, 0.25f } },
	{ "models/monsters/", { 0.60f, 0.1f, 0.3f } },
	{ "players/", { 0.55f, 0.2f, 0.3f } },
	{ "models/items/", { 0.40f, 0.5f, 0.25f } },
	{ "models/objects/", { 0.50f, 0.4f, 0.3f } },
};

static const matinfo_t	mat_defaultwall = { 0.55f, 0.3f, 0.7f };
static const matinfo_t	mat_defaultmodel = { 0.55f, 0.2f, 0.3f };

//=============================================================================

// * matches any run of characters; everything else must match exactly
static qboolean Mat_Wildcard (const char *pattern, const char *text)
{
	for ( ; *pattern ; pattern++, text++)
	{
		if (*pattern == '*')
		{
			for (pattern++ ; ; text++)
			{
				if (Mat_Wildcard (pattern, text))
					return true;
				if (!*text)
					return false;
			}
		}
		if (*pattern != *text)
			return false;
	}
	return !*text;
}

static qboolean Mat_HasWord (const char *words, const char *name)
{
	char		word[32];
	const char	*end;
	int			len;

	while (*words)
	{
		while (*words == ' ')
			words++;
		end = strchr (words, ' ');
		len = end ? (int)(end - words) : (int)strlen (words);
		if (len > 0 && len < (int)sizeof(word))
		{
			memcpy (word, words, len);
			word[len] = 0;
			if (strstr (name, word))
				return true;
		}
		words += len;
	}
	return false;
}

static void Mat_Lookup (const char *name, matinfo_t *info)
{
	char	lower[MAX_QPATH], base[MAX_QPATH], *p;
	int		i;

	strncpy (lower, name, sizeof(lower)-1);
	lower[sizeof(lower)-1] = 0;
	strlwr (lower);

	for (i=0 ; i<mat_numuserrules ; i++)
	{
		if (Mat_Wildcard (mat_userrules[i].pattern, lower))
		{
			*info = mat_userrules[i].info;
			return;
		}
	}

	if (strncmp (lower, "textures/", 9))
	{
		for (i=0 ; i<(int)(sizeof(mat_models)/sizeof(mat_models[0])) ; i++)
		{
			if (strstr (lower, mat_models[i].word))
			{
				*info = mat_models[i].info;
				return;
			}
		}
		*info = mat_defaultmodel;
		return;
	}

	p = strrchr (lower, '/');
	strcpy (base, p ? p + 1 : lower);
	p = strrchr (base, '.');
	if (p)
		*p = 0;

	for (i=0 ; i<(int)(sizeof(mat_walls)/sizeof(mat_walls[0])) ; i++)
	{
		if (Mat_HasWord (mat_walls[i].words, base))
		{
			*info = mat_walls[i].info;
			return;
		}
	}
	*info = mat_defaultwall;
}

/*
===============
R_MaterialInfo

name is an image path such as textures/e1u1/metal1_1.wal
===============
*/
void R_MaterialInfo (const char *name, matinfo_t *info)
{
	Mat_Lookup (name, info);

	// pt_roughness, pt_metallic and pt_bump scale whatever was decided
	info->roughness *= r_roughscale;
	if (info->roughness < 0.02f)
		info->roughness = 0.02f;
	if (info->roughness > 1)
		info->roughness = 1;
	info->metallic *= r_metalscale;
	if (info->metallic < 0)
		info->metallic = 0;
	if (info->metallic > 1)
		info->metallic = 1;
	info->bump *= r_bumpscale;
	if (info->bump < 0)
		info->bump = 0;
}

/*
===============
R_InitMaterials

Reads pt_materials.txt if there is one. Each line is
	<path with * wildcards> <roughness> <metallic> <bump>
and anything after # is a comment. Earlier lines win.
===============
*/
void R_InitMaterials (void)
{
	char		*buffer, *text, *line, *next;
	matrule_t	*rule;
	int			len;

	mat_numuserrules = 0;

	len = ri.FS_LoadFile ("pt_materials.txt", (void **)&buffer);
	if (!buffer)
		return;

	text = malloc (len + 1);
	memcpy (text, buffer, len);
	text[len] = 0;
	ri.FS_FreeFile (buffer);

	for (line=text ; line && *line ; line=next)
	{
		next = strchr (line, '\n');
		if (next)
			*next++ = 0;
		if (strchr (line, '#'))
			*strchr (line, '#') = 0;

		if (mat_numuserrules == MAX_MATRULES)
			break;
		rule = &mat_userrules[mat_numuserrules];
		if (sscanf (line, "%47s %f %f %f", rule->pattern, &rule->info.roughness,
			&rule->info.metallic, &rule->info.bump) == 4)
		{
			strlwr (rule->pattern);
			mat_numuserrules++;
		}
	}
	free (text);

	ri.Con_Printf (PRINT_ALL, "pt_materials.txt: %d rules\n", mat_numuserrules);
}

/*
===============
R_ImageNormalMap

Builds, once, the image's detail map: RGB is a tangent space normal made by
treating brightness as height, alpha is roughness, a little lower where the
picture is bright and higher where it is dark.
===============
*/
uint32_t *R_ImageNormalMap (image_t *image, const matinfo_t *info)
{
	float		*height, strength, dx, dy, len, rough;
	uint32_t	c, *out;
	int			x, y, w, h, xm, xp, ym, yp, nx, ny, nz, a;

	if (image->normalmap)
		return image->normalmap;

	w = image->width;
	h = image->height;
	height = malloc (w * h * sizeof(float));
	for (y=0 ; y<w*h ; y++)
	{
		c = image->pixels[y];
		height[y] = ((c & 0xff) * 0.299f + ((c >> 8) & 0xff) * 0.587f + ((c >> 16) & 0xff) * 0.114f) * (1.0f / 255.0f);
	}

	out = malloc (w * h * sizeof(uint32_t));
	strength = info->bump * 2.0f;

	for (y=0 ; y<h ; y++)
	{
		ym = (y + h - 1) % h;
		yp = (y + 1) % h;
		for (x=0 ; x<w ; x++)
		{
			xm = (x + w - 1) % w;
			xp = (x + 1) % w;

			// Sobel
			dx = (height[ym*w+xp] + 2 * height[y*w+xp] + height[yp*w+xp]
				- height[ym*w+xm] - 2 * height[y*w+xm] - height[yp*w+xm]) * 0.25f;
			dy = (height[yp*w+xm] + 2 * height[yp*w+x] + height[yp*w+xp]
				- height[ym*w+xm] - 2 * height[ym*w+x] - height[ym*w+xp]) * 0.25f;

			dx *= -strength;
			dy *= -strength;
			len = 1.0f / sqrt (dx * dx + dy * dy + 1.0f);

			rough = info->roughness + (0.5f - height[y*w+x]) * 0.3f;
			if (rough < 0.04f)
				rough = 0.04f;
			if (rough > 1.0f)
				rough = 1.0f;

			nx = (dx * len * 0.5f + 0.5f) * 255.0f + 0.5f;
			ny = (dy * len * 0.5f + 0.5f) * 255.0f + 0.5f;
			nz = (len * 0.5f + 0.5f) * 255.0f + 0.5f;
			a = rough * 255.0f + 0.5f;
			out[y*w+x] = (uint32_t)nx | ((uint32_t)ny << 8) | ((uint32_t)nz << 16) | ((uint32_t)a << 24);
		}
	}

	free (height);
	image->normalmap = out;
	return out;
}
