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
// rpt_model.c -- model loading and caching

#include "rpt_local.h"

#define	MAX_RPT_MODELS	1024

static model_t	r_models[MAX_RPT_MODELS];
static int		numr_models;

/*
=================
Mod_LoadAlias

Keeps the file, byte swapped and checked so the scene builder can index it
without looking over its shoulder
=================
*/
static qboolean Mod_LoadAlias (model_t *mod, byte *buffer, int filelen)
{
	dmdl_t			*hdr;
	dstvert_t		*st;
	dtriangle_t		*tri;
	daliasframe_t	*frame;
	int				i, j;
	int64_t			end;

	if (filelen < (int)sizeof(*hdr))
		return false;

	hdr = malloc (filelen);
	memcpy (hdr, buffer, filelen);
	for (i=0 ; i<(int)(sizeof(*hdr)/4) ; i++)
		((int *)hdr)[i] = LittleLong (((int *)hdr)[i]);

	if (hdr->version != ALIAS_VERSION
		|| hdr->num_xyz <= 0 || hdr->num_xyz > MAX_VERTS
		|| hdr->num_st <= 0 || hdr->num_tris <= 0 || hdr->num_tris > MAX_TRIANGLES
		|| hdr->num_frames <= 0 || hdr->num_frames > MAX_FRAMES
		|| hdr->num_skins < 0 || hdr->num_skins > MAX_MD2SKINS
		|| hdr->skinwidth <= 0 || hdr->skinheight <= 0
		|| hdr->framesize < (int)(sizeof(daliasframe_t) - sizeof(dtrivertx_t) + hdr->num_xyz * sizeof(dtrivertx_t))
		|| hdr->ofs_st < 0 || hdr->ofs_tris < 0 || hdr->ofs_frames < 0 || hdr->ofs_skins < 0)
		goto bad;

	end = filelen;
	if ((int64_t)hdr->ofs_st + (int64_t)hdr->num_st * sizeof(dstvert_t) > end
		|| (int64_t)hdr->ofs_tris + (int64_t)hdr->num_tris * sizeof(dtriangle_t) > end
		|| (int64_t)hdr->ofs_frames + (int64_t)hdr->num_frames * hdr->framesize > end
		|| (int64_t)hdr->ofs_skins + (int64_t)hdr->num_skins * MAX_SKINNAME > end)
		goto bad;

	st = (dstvert_t *)((byte *)hdr + hdr->ofs_st);
	for (i=0 ; i<hdr->num_st ; i++)
	{
		st[i].s = LittleShort (st[i].s);
		st[i].t = LittleShort (st[i].t);
	}

	tri = (dtriangle_t *)((byte *)hdr + hdr->ofs_tris);
	for (i=0 ; i<hdr->num_tris ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			tri[i].index_xyz[j] = LittleShort (tri[i].index_xyz[j]);
			tri[i].index_st[j] = LittleShort (tri[i].index_st[j]);
			if (tri[i].index_xyz[j] < 0 || tri[i].index_xyz[j] >= hdr->num_xyz
				|| tri[i].index_st[j] < 0 || tri[i].index_st[j] >= hdr->num_st)
				goto bad;
		}
	}

	for (i=0 ; i<hdr->num_frames ; i++)
	{
		frame = (daliasframe_t *)((byte *)hdr + hdr->ofs_frames + i * hdr->framesize);
		for (j=0 ; j<3 ; j++)
		{
			frame->scale[j] = LittleFloat (frame->scale[j]);
			frame->translate[j] = LittleFloat (frame->translate[j]);
		}
	}

	mod->type = mod_alias;
	mod->data = hdr;
	mod->numskins = hdr->num_skins;
	return true;

bad:
	free (hdr);
	return false;
}

static qboolean Mod_LoadSprite (model_t *mod, byte *buffer, int filelen)
{
	dsprite_t	*spr;
	int			i;

	if (filelen < (int)sizeof(*spr))
		return false;

	spr = malloc (filelen);
	memcpy (spr, buffer, filelen);
	spr->version = LittleLong (spr->version);
	spr->numframes = LittleLong (spr->numframes);

	if (spr->version != SPRITE_VERSION || spr->numframes <= 0 || spr->numframes > MAX_MD2SKINS
		|| (int64_t)sizeof(*spr) + (int64_t)(spr->numframes - 1) * sizeof(dsprframe_t) > filelen)
	{
		free (spr);
		return false;
	}

	for (i=0 ; i<spr->numframes ; i++)
	{
		spr->frames[i].width = LittleLong (spr->frames[i].width);
		spr->frames[i].height = LittleLong (spr->frames[i].height);
		spr->frames[i].origin_x = LittleLong (spr->frames[i].origin_x);
		spr->frames[i].origin_y = LittleLong (spr->frames[i].origin_y);
		spr->frames[i].name[MAX_SKINNAME-1] = 0;
	}

	mod->type = mod_sprite;
	mod->data = spr;
	mod->numskins = spr->numframes;
	return true;
}

/*
=================
Mod_TouchImages

Finds the model's images, which also marks them as in use
=================
*/
static void Mod_TouchImages (model_t *mod)
{
	dmdl_t		*hdr;
	dsprite_t	*spr;
	char		name[MAX_SKINNAME];
	int			i;

	if (mod->type == mod_alias)
	{
		hdr = mod->data;
		for (i=0 ; i<mod->numskins ; i++)
		{
			memcpy (name, (char *)hdr + hdr->ofs_skins + i * MAX_SKINNAME, MAX_SKINNAME);
			name[MAX_SKINNAME-1] = 0;
			mod->skins[i] = R_FindImage (name, it_skin);
		}
	}
	else if (mod->type == mod_sprite)
	{
		spr = mod->data;
		for (i=0 ; i<mod->numskins ; i++)
			mod->skins[i] = R_FindImage (spr->frames[i].name, it_sprite);
	}
}

/*
=============
R_IsModel

Is this one of the models this renderer has handed out and still holds?
The client keeps what it was given, and something it kept from a renderer
that has since been replaced points at memory that is no longer a model.
=============
*/
qboolean R_IsModel (struct model_s *mod)
{
	size_t	offset;

	if (mod < r_models || mod >= r_models + numr_models)
		return false;
	offset = (byte *)mod - (byte *)r_models;
	return offset % sizeof(r_models[0]) == 0 && mod->registration_sequence != 0;
}

/*
@@@@@@@@@@@@@@@@@@@@@
R_RegisterModel

@@@@@@@@@@@@@@@@@@@@@
*/
struct model_s *R_RegisterModel (char *name)
{
	model_t		*mod;
	byte		*buffer;
	int			i, filelen, ident;
	qboolean	ok;

	if (!name || !name[0] || strlen (name) >= MAX_QPATH)
		return NULL;

	for (i=0, mod=r_models ; i<numr_models ; i++, mod++)
	{
		if (mod->registration_sequence && !strcmp (mod->name, name))
		{
			mod->registration_sequence = registration_sequence;
			Mod_TouchImages (mod);
			return mod;
		}
	}

	for (i=0, mod=r_models ; i<numr_models ; i++, mod++)
		if (!mod->registration_sequence)
			break;
	if (i == numr_models)
	{
		if (numr_models == MAX_RPT_MODELS)
			ri.Sys_Error (ERR_DROP, "MAX_RPT_MODELS");
		numr_models++;
	}
	memset (mod, 0, sizeof(*mod));

	if (name[0] == '*')
	{
		// inline models are part of the map, which the world loader holds
		mod->type = mod_inline;
		mod->inlinenum = atoi (name + 1);
		if (mod->inlinenum < 1)
			return NULL;
	}
	else
	{
		filelen = ri.FS_LoadFile (name, (void **)&buffer);
		if (!buffer)
			return NULL;

		ok = false;
		ident = filelen >= 4 ? LittleLong (*(int *)buffer) : 0;
		if (ident == IDALIASHEADER)
			ok = Mod_LoadAlias (mod, buffer, filelen);
		else if (ident == IDSPRITEHEADER)
			ok = Mod_LoadSprite (mod, buffer, filelen);
		else if (ident == IDBSPHEADER)
		{
			mod->type = mod_world;
			ok = true;
		}
		ri.FS_FreeFile (buffer);

		if (!ok)
		{
			ri.Con_Printf (PRINT_ALL, "R_RegisterModel: can't use %s\n", name);
			memset (mod, 0, sizeof(*mod));
			return NULL;
		}
	}

	strcpy (mod->name, name);
	mod->registration_sequence = registration_sequence;
	Mod_TouchImages (mod);
	return mod;
}

static void Mod_Free (model_t *mod)
{
	free (mod->data);
	memset (mod, 0, sizeof(*mod));
}

/*
================
R_FreeUnusedModels
================
*/
void R_FreeUnusedModels (void)
{
	int		i;

	for (i=0 ; i<numr_models ; i++)
		if (r_models[i].registration_sequence && r_models[i].registration_sequence != registration_sequence)
			Mod_Free (&r_models[i]);
}

void R_ShutdownModels (void)
{
	int		i;

	for (i=0 ; i<numr_models ; i++)
		Mod_Free (&r_models[i]);
	numr_models = 0;
}
