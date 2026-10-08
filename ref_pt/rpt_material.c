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
// metallic a surface is and which way its small details face. What kind of
// thing it is is guessed here from the texture's name, and a pt_materials.txt
// in the game directory overrides the guesses; its relief and where it is
// rougher, smoother, bare metal or painted over are read from its picture,
// by pt/material.

#include "rpt_local.h"
#include "../pt/material/pt_material.h"
#include "../pt/png/pt_png.h"

typedef struct
{
	char		pattern[48];
	matinfo_t	info;
} matrule_t;

#define	MAX_MATRULES	512

static matrule_t	mat_userrules[MAX_MATRULES];
static int			mat_numuserrules;

static void R_MaterialShow_f (void);

// Matched against the texture's file name, without directory or extension.
// The first rule containing a match wins, so the specific come first.
static const struct
{
	const char	*words;		// space separated
	matinfo_t	info;		// roughness, metallic, bump
} mat_walls[] =
{
	{ "wndow window wndw brwind glass", { 0.08f, 0.0f, 0.2f, 0.0f } },
	{ "rock mine cindr cinder cindb geowal sand mud grass brick marble flesh blood dirt stone drag crys pyramid mont rrock", { 0.90f, 0.0f, 1.0f, 0.0f } },
	{ "lava", { 0.70f, 0.0f, 1.2f, 0.0f } },
	{ "comp mon sign num arrow keypad but btn swt exit location caution banner lever", { 0.30f, 0.0f, 0.6f, 1.0f } },
	{ "light lite baselt wslt wstlt redlt ctylt pallt minlt grlt rlight tlight citlit geolit prwlt lsrlt lzr glo", { 0.35f, 0.1f, 0.5f, 1.5f } },
	{ "grate grat wire cable pip duc", { 0.40f, 0.7f, 0.9f, 0.0f } },
	{ "floor flr flor stairs plat", { 0.45f, 0.4f, 0.7f, 0.0f } },
	{ "metal met mtl metl mach support supprt door dr belt tram train turret lead thinm troof slot notch pilr pillar "
	  "core pow pwr fuse shutl timpod tcm box crate ceil tunl hall elev refl", { 0.45f, 0.5f, 0.7f, 0.0f } },
};

// matched against the whole path
static const struct
{
	const char	*word;
	matinfo_t	info;
} mat_models[] =
{
	{ "models/weapons/", { 0.40f, 0.6f, 0.25f, 0.0f } },
	{ "models/monsters/", { 0.60f, 0.1f, 0.3f, 0.0f } },
	{ "players/", { 0.55f, 0.2f, 0.3f, 0.0f } },
	{ "models/items/", { 0.40f, 0.5f, 0.25f, 0.0f } },
	{ "models/objects/", { 0.50f, 0.4f, 0.3f, 0.0f } },
};

static const matinfo_t	mat_defaultwall = { 0.55f, 0.3f, 0.7f, 0.0f };
static const matinfo_t	mat_defaultmodel = { 0.55f, 0.2f, 0.3f, 0.0f };

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
	<path with * wildcards> <roughness> <metallic> <bump> [glow]
and anything after # is a comment. Earlier lines win.
===============
*/
void R_InitMaterials (void)
{
	char		*buffer, *text, *line, *next;
	matrule_t	*rule;
	int			len;

	mat_numuserrules = 0;

	ri.Cmd_AddCommand ("pt_material_show", R_MaterialShow_f);

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
		rule->info.glow = 0;		// optional fifth column
		if (sscanf (line, "%47s %f %f %f %f", rule->pattern, &rule->info.roughness,
			&rule->info.metallic, &rule->info.bump, &rule->info.glow) >= 4)
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
Mat_FindMap

A map made for an image by hand: the image's name with its extension
replaced by the suffix. NULL if there is none.
===============
*/
static image_t *Mat_FindMap (image_t *image, const char *suffix)
{
	char	name[MAX_QPATH + 8];
	char	*dot;

	strcpy (name, image->name);
	dot = strrchr (name, '.');
	if (!dot || (dot - name) + strlen (suffix) >= MAX_QPATH)
		return NULL;
	strcpy (dot, suffix);
	return R_FindImage (name, image->type);
}

/*
===============
R_ImageGlowMap

<name>_e.tga beside the image: what glows, and in what colour
===============
*/
image_t *R_ImageGlowMap (image_t *image)
{
	return Mat_FindMap (image, "_e.tga");
}

/*
=================================================================

DETAIL MAPS

One for each picture, made the first time it is asked for: a tangent space
normal, and alpha is roughness. Read from the picture (image->normal_metal)
the normal is in red and green alone and blue is how metallic the surface is
there; made the plain way, blue is the normal's z and the metal is the
material's everywhere.

With the map comes the picture again with its painted light taken out
(image->colour), where there was any to take.

=================================================================
*/

static int	mat_made, mat_msec;		// since the last report: maps worked out and how long that took
static int	mat_kept;				// maps read back from pt_cache

/*
===============
Mat_Plain

The map as it was first made, and still is with pt_material_maps 0 or for
a picture too large to read: brightness is taken as height, and roughness
is a little lower where the picture is bright and higher where it is dark.
===============
*/
static uint32_t *Mat_Plain (image_t *image, const matinfo_t *info, int *width, int *height)
{
	float		*bright, strength, dx, dy, len, rough;
	uint32_t	c, *made;
	int			x, y, w, h, xm, xp, ym, yp, nx, ny, nz, a;

	w = image->width;
	h = image->height;
	bright = malloc (w * h * sizeof(float));
	for (y=0 ; y<w*h ; y++)
	{
		c = image->pixels[y];
		bright[y] = ((c & 0xff) * 0.299f + ((c >> 8) & 0xff) * 0.587f + ((c >> 16) & 0xff) * 0.114f) * (1.0f / 255.0f);
	}

	made = malloc (w * h * sizeof(uint32_t));
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
			dx = (bright[ym*w+xp] + 2 * bright[y*w+xp] + bright[yp*w+xp]
				- bright[ym*w+xm] - 2 * bright[y*w+xm] - bright[yp*w+xm]) * 0.25f;
			dy = (bright[yp*w+xm] + 2 * bright[yp*w+x] + bright[yp*w+xp]
				- bright[ym*w+xm] - 2 * bright[ym*w+x] - bright[ym*w+xp]) * 0.25f;

			dx *= -strength;
			dy *= -strength;
			len = 1.0f / sqrt (dx * dx + dy * dy + 1.0f);

			rough = info->roughness + (0.5f - bright[y*w+x]) * 0.3f;
			if (rough < 0.04f)
				rough = 0.04f;
			if (rough > 1.0f)
				rough = 1.0f;

			nx = (dx * len * 0.5f + 0.5f) * 255.0f + 0.5f;
			ny = (dy * len * 0.5f + 0.5f) * 255.0f + 0.5f;
			nz = (len * 0.5f + 0.5f) * 255.0f + 0.5f;
			a = rough * 255.0f + 0.5f;
			made[y*w+x] = (uint32_t)nx | ((uint32_t)ny << 8) | ((uint32_t)nz << 16) | ((uint32_t)a << 24);
		}
	}
	free (bright);

	*width = w;
	*height = h;
	return made;
}

/*
Reading a picture takes a few milliseconds and a map has a few hundred of
them, so what was read is kept in pt_cache in the game directory, one file a
picture, and read back the next time. Nothing in there is needed: it can be
deleted at any time and is made again.
*/

#define	MAT_CACHE_MAGIC		0x324d5450		// "PTM2"

typedef struct
{
	uint32_t	magic;
	uint32_t	key;				// what the maps were made from, see Mat_Key
	uint32_t	width, height;		// of the detail map that follows, as it is in memory
	uint32_t	colour_width, colour_height;	// of the colours after it; 0 if the picture's own are used
} matcache_t;

static uint32_t Mat_Hash (const void *data, size_t bytes, uint32_t hash)
{
	const byte	*p = data;

	while (bytes--)
		hash = (hash ^ *p++) * 16777619u;
	return hash;
}

/*
===============
Mat_Key

Everything a map depends on: the picture itself, what it was read as, and
the version of what read it. A kept map made from anything else is stale.
===============
*/
static uint32_t Mat_Key (image_t *image, const pt_material_from_t *from)
{
	uint32_t	words[4], hash;

	words[0] = PT_MATERIAL_VERSION;
	words[1] = image->width;
	words[2] = image->height;
	words[3] = (from->repeats ? 1 : 0) | (from->painted_light ? 2 : 0);
	hash = Mat_Hash (words, sizeof(words), 2166136261u);
	hash = Mat_Hash (&from->bump, sizeof(from->bump), hash);
	hash = Mat_Hash (&from->roughness, sizeof(from->roughness), hash);
	hash = Mat_Hash (&from->metallic, sizeof(from->metallic), hash);
	hash = Mat_Hash (&from->delight, sizeof(from->delight), hash);
	return Mat_Hash (image->pixels, (size_t)image->width * image->height * sizeof(uint32_t), hash);
}

// the image's name with whatever could lead out of the directory made harmless
static void Mat_FlatName (image_t *image, char *flat)
{
	int		i, c;

	for (i=0 ; image->name[i] ; i++)
	{
		c = image->name[i];
		flat[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
			|| c == '.' || c == '-' || c == '+') ? c : '_';
	}
	flat[i] = 0;
}

static void Mat_CachePath (image_t *image, char *path, int size)
{
	char	flat[MAX_QPATH];

	Mat_FlatName (image, flat);
	Com_sprintf (path, size, "%s/pt_cache/%s.ptm", ri.FS_Gamedir (), flat);
}

static uint32_t *Mat_ReadPixels (FILE *f, size_t count)
{
	uint32_t	*pixels;

	pixels = malloc (count * sizeof(uint32_t));
	if (pixels && fread (pixels, sizeof(uint32_t), count, f) != count)
	{	// cut short
		free (pixels);
		pixels = NULL;
	}
	return pixels;
}

// the maps kept for the image, if they were made from what key stands for
static qboolean Mat_CacheRead (image_t *image, uint32_t key, int width, int height, pt_material_maps_t *maps)
{
	char		path[MAX_OSPATH];
	matcache_t	header;
	FILE		*f;

	memset (maps, 0, sizeof(*maps));

	Mat_CachePath (image, path, sizeof(path));
	f = fopen (path, "rb");
	if (!f)
		return false;

	if (fread (&header, sizeof(header), 1, f) == 1 && header.magic == MAT_CACHE_MAGIC && header.key == key
		&& header.width == (uint32_t)width && header.height == (uint32_t)height
		&& (!header.colour_width || (header.colour_width == (uint32_t)image->width && header.colour_height == (uint32_t)image->height)))
	{
		maps->detail = Mat_ReadPixels (f, (size_t)width * height);
		maps->detail_width = width;
		maps->detail_height = height;
		if (maps->detail && header.colour_width)
		{
			maps->colour = Mat_ReadPixels (f, (size_t)image->width * image->height);
			maps->colour_width = image->width;
			maps->colour_height = image->height;
			if (!maps->colour)
			{
				free (maps->detail);
				maps->detail = NULL;
			}
		}
	}
	fclose (f);
	return maps->detail != NULL;
}

static void Mat_CacheWrite (image_t *image, uint32_t key, const pt_material_maps_t *maps)
{
	char		path[MAX_OSPATH];
	matcache_t	header;
	FILE		*f;

	Com_sprintf (path, sizeof(path), "%s/pt_cache", ri.FS_Gamedir ());
	Sys_Mkdir (path);

	Mat_CachePath (image, path, sizeof(path));
	f = fopen (path, "wb");
	if (!f)
		return;		// a game directory that cannot be written to: made again each time
	header.magic = MAT_CACHE_MAGIC;
	header.key = key;
	header.width = maps->detail_width;
	header.height = maps->detail_height;
	header.colour_width = maps->colour ? maps->colour_width : 0;
	header.colour_height = maps->colour ? maps->colour_height : 0;
	fwrite (&header, sizeof(header), 1, f);
	fwrite (maps->detail, sizeof(uint32_t), (size_t)maps->detail_width * maps->detail_height, f);
	if (maps->colour)
		fwrite (maps->colour, sizeof(uint32_t), (size_t)maps->colour_width * maps->colour_height, f);
	fclose (f);
}

// how the image's picture is to be read
static void Mat_From (image_t *image, const matinfo_t *info, pt_material_from_t *from)
{
	double		sum;
	uint32_t	c;
	int			i, count;

	// a wall's picture repeats, and was painted the way up it is seen. A
	// skin is cut into pieces and wrapped round its model.
	from->repeats = image->type == it_wall;
	from->painted_light = image->type == it_wall;
	from->bump = info->bump;
	from->metallic = info->metallic;
	// a screen or a lamp was painted bright for its own sake: that light stays
	from->delight = (from->painted_light && info->glow <= 0) ? r_materialdelight : 0;

	// a dark picture has always been taken for a rougher thing than a bright
	// one: the map varies the roughness about what Mat_Plain gives on average
	sum = 0;
	count = image->width * image->height;
	for (i=0 ; i<count ; i++)
	{
		c = image->pixels[i];
		sum += (c & 0xff) * 0.299 + ((c >> 8) & 0xff) * 0.587 + ((c >> 16) & 0xff) * 0.114;
	}
	from->roughness = info->roughness + (0.5f - (float)(sum / count) * (1.0f / 255.0f)) * 0.3f;
	if (from->roughness < 0.04f)
		from->roughness = 0.04f;
	if (from->roughness > 1.0f)
		from->roughness = 1.0f;
}

/*
===============
Mat_FromPicture

The map read from the picture's painted light, see pt/material, and with it
image->colour. NULL if it could not be made.
===============
*/
static uint32_t *Mat_FromPicture (image_t *image, const matinfo_t *info, int *width, int *height)
{
	pt_material_from_t	from;
	pt_material_maps_t	maps;
	uint32_t			key;
	int					scale, start;
	qboolean			keep;

	// asked now, not once a frame: skins are read before a level's first frame
	keep = pt_material_cache->value != 0;

	Mat_From (image, info, &from);
	scale = pt_material_detail_scale (image->width, image->height, from.repeats);
	*width = image->width * scale;
	*height = image->height * scale;

	key = keep ? Mat_Key (image, &from) : 0;
	if (keep && Mat_CacheRead (image, key, *width, *height, &maps))
		mat_kept++;
	else
	{
		start = Sys_Milliseconds ();
		if (!pt_material_read (image->pixels, image->width, image->height, &from, &maps))
			return NULL;
		mat_msec += Sys_Milliseconds () - start;
		mat_made++;

		if (keep)
			Mat_CacheWrite (image, key, &maps);
	}

	free (image->colour);
	image->colour = maps.colour;
	return maps.detail;
}

/*
===============
R_ImageNormalMap

Builds, once, the image's detail map. Returns NULL where the material is
flat.

It is read from the picture: its painted highlights and shadows as shape,
its colours as how rough and how metallic it is from place to place.
<name>_n.tga beside the image is used as the normals instead if it is there
(red to the right, green down the picture, as the made ones are; set
pt_normal_flip for maps with green up), and the red of <name>_r.tga as the
roughness.
===============
*/
uint32_t *R_ImageNormalMap (image_t *image, const matinfo_t *info, int *width, int *height)
{
	float		rough;
	uint32_t	c, *made, *out;
	image_t		*normals, *roughs;
	int			x, y, w, h, sx, sy;

	if (image->normalmap)
	{
		// made for a surface with relief; what is flat and shows the same picture has none
		if (info->bump <= 0 && !image->normal_byhand)
			return NULL;
		*width = image->normal_width;
		*height = image->normal_height;
		return image->normalmap;
	}

	normals = Mat_FindMap (image, "_n.tga");
	roughs = Mat_FindMap (image, "_r.tga");
	if (!normals && !roughs && info->bump <= 0)
		return NULL;

	// what can be told from the picture itself
	made = r_materialmaps ? Mat_FromPicture (image, info, &w, &h) : NULL;
	image->normal_metal = made != NULL;
	image->normal_byhand = normals || roughs;
	if (!made)
		made = Mat_Plain (image, info, &w, &h);

	out = made;
	if (normals || roughs)
	{
		// as fine as the finest of the maps given
		int		ow = w, oh = h;

		if (normals)
		{
			ow = normals->width;
			oh = normals->height;
		}
		if (roughs && roughs->width > ow)
		{
			ow = roughs->width;
			oh = roughs->height;
		}

		out = malloc (ow * oh * sizeof(uint32_t));
		for (y=0 ; y<oh ; y++)
		{
			for (x=0 ; x<ow ; x++)
			{
				c = made[(y * h / oh) * w + (x * w / ow)];
				if (normals)
				{
					sx = x * normals->width / ow;
					sy = y * normals->height / oh;
					// beside metal read from the picture there is room for the normal's x and y only
					if (image->normal_metal)
						c = (c & 0xffff0000) | (normals->pixels[sy * normals->width + sx] & 0x0000ffff);
					else
						c = (c & 0xff000000) | (normals->pixels[sy * normals->width + sx] & 0x00ffffff);
					if (r_normalflip)
						c = (c & 0xffff00ff) | ((255 - ((c >> 8) & 0xff)) << 8);
				}
				if (roughs)
				{
					sx = x * roughs->width / ow;
					sy = y * roughs->height / oh;
					rough = (roughs->pixels[sy * roughs->width + sx] & 0xff) * (1.0f / 255.0f) * r_roughscale;
					if (rough < 0.04f)
						rough = 0.04f;
					if (rough > 1.0f)
						rough = 1.0f;
					c = (c & 0x00ffffff) | ((uint32_t)(rough * 255.0f + 0.5f) << 24);
				}
				out[y * ow + x] = c;
			}
		}
		free (made);
		w = ow;
		h = oh;
	}

	image->normalmap = out;
	image->normal_width = *width = w;
	image->normal_height = *height = h;
	return out;
}

/*
===============
R_MaterialsReport

Says in the console what the detail maps asked for since the last report
cost, if any were
===============
*/
void R_MaterialsReport (void)
{
	if (mat_made)
		ri.Con_Printf (PRINT_ALL, "Materials: %d read from their pictures in %d ms, %d kept from before\n",
			mat_made, mat_msec, mat_kept);
	else if (mat_kept)
		ri.Con_Printf (PRINT_DEVELOPER, "Materials: %d kept from before\n", mat_kept);
	mat_made = mat_msec = mat_kept = 0;
}

/*
===============
R_MaterialShow_f

pt_material_show <image>: writes the picture beside what was read from it,
its colours without the painted light, its height, its normals, its
roughness and its metal, as one PNG in scrnshot
===============
*/
static void R_MaterialShow_f (void)
{
	char				name[MAX_QPATH], flat[MAX_QPATH], path[MAX_OSPATH], *dot, *slash;
	pt_material_from_t	from;
	pt_material_maps_t	maps;
	matinfo_t			info;
	image_t				*image;
	uint32_t			*sheet, *colour, c;
	unsigned char		*high;
	float				nx, ny;
	int					x, y, w, h, scale, at;

	if (ri.Cmd_Argc () != 2 || strlen (ri.Cmd_Argv (1)) > MAX_QPATH - 8)
	{
		ri.Con_Printf (PRINT_ALL, "usage: pt_material_show <image>, for example textures/e1u1/metal1_1\n");
		return;
	}
	strcpy (name, ri.Cmd_Argv (1));
	strlwr (name);
	dot = strrchr (name, '.');
	slash = strrchr (name, '/');
	if (!dot || (slash && dot < slash))
		strcat (name, strncmp (name, "textures/", 9) ? ".pcx" : ".wal");

	image = R_FindImage (name, strncmp (name, "textures/", 9) ? it_skin : it_wall);
	if (!image)
	{
		ri.Con_Printf (PRINT_ALL, "%s not found\n", name);
		return;
	}

	R_MaterialInfo (image->name, &info);
	Mat_From (image, &info, &from);
	scale = pt_material_detail_scale (image->width, image->height, from.repeats);
	w = image->width * scale;
	h = image->height * scale;
	high = pt_material_height (image->pixels, image->width, image->height, &from);
	sheet = malloc ((size_t)w * 6 * h * sizeof(uint32_t));
	if (!pt_material_read (image->pixels, image->width, image->height, &from, &maps) || !high || !sheet)
	{
		ri.Con_Printf (PRINT_ALL, "%s could not be read\n", name);
		free (maps.detail);
		free (maps.colour);
		free (high);
		free (sheet);
		return;
	}
	colour = maps.colour ? maps.colour : image->pixels;

	for (y=0 ; y<h ; y++)
	{
		for (x=0 ; x<w ; x++)
		{
			c = maps.detail[y * w + x];
			at = (y / scale) * image->width + x / scale;
			// the normal as such maps are usually shown, with its z worked out
			nx = (c & 0xff) * (2.0f / 255.0f) - 1.0f;
			ny = ((c >> 8) & 0xff) * (2.0f / 255.0f) - 1.0f;
			nx = 1.0f - nx * nx - ny * ny;

			sheet[y * w * 6 + x] = image->pixels[at] | 0xff000000;
			sheet[y * w * 6 + w + x] = colour[at] | 0xff000000;
			sheet[y * w * 6 + w * 2 + x] = high[y * w + x] * 0x010101u | 0xff000000;
			sheet[y * w * 6 + w * 3 + x] = (c & 0xffff) | ((uint32_t)((sqrt (nx > 0 ? nx : 0) * 0.5f + 0.5f) * 255.0f + 0.5f) << 16) | 0xff000000;
			sheet[y * w * 6 + w * 4 + x] = (c >> 24) * 0x010101u | 0xff000000;
			sheet[y * w * 6 + w * 5 + x] = ((c >> 16) & 0xff) * 0x010101u | 0xff000000;
		}
	}

	Com_sprintf (path, sizeof(path), "%s/scrnshot", ri.FS_Gamedir ());
	Sys_Mkdir (path);
	Mat_FlatName (image, flat);
	Com_sprintf (path, sizeof(path), "%s/scrnshot/material_%s.png", ri.FS_Gamedir (), flat);
	if (pt_png_write (path, sheet, w * 6, h))
		ri.Con_Printf (PRINT_ALL, "Wrote %s: picture, %s, height, normals, roughness, metal (the material's: %.2f)\n", path,
			maps.colour ? "its colours without the painted light" : "the same again (no painted light taken out)", info.metallic);
	else
		ri.Con_Printf (PRINT_ALL, "Couldn't write %s\n", path);

	free (maps.detail);
	free (maps.colour);
	free (high);
	free (sheet);
}

void R_ShutdownMaterials (void)
{
	ri.Cmd_RemoveCommand ("pt_material_show");
}
