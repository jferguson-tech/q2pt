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
// rpt_local.h -- glue between the engine's refresh interface and the
// engine independent path tracers in ../pt. Built twice: ref_ptcpu.dll and,
// with RPT_RTX defined, ref_ptrtx.dll.

#include <windows.h>
#include <stdio.h>
#include <math.h>

#include "../client/ref.h"
#include "../pt/include/pt.h"

#define	REF_VERSION	"PT 0.01"

typedef enum
{
	it_skin,
	it_sprite,
	it_wall,
	it_pic,
	it_sky
} imagetype_t;

typedef struct image_s
{
	char		name[MAX_QPATH];			// game path, including extension
	imagetype_t	type;
	int			width, height;
	int			registration_sequence;		// 0 = free
	uint32_t	*pixels;					// R,G,B,A bytes, premultiplied
	int			pt_texture;					// backend handle + 1, 0 = not created
	uint32_t	*normalmap;					// made on demand, see rpt_material.c
	int			pt_normal_texture;			// backend handle + 1 for normalmap
} image_t;

typedef struct
{
	float	roughness;	// 0 mirror - 1 matte
	float	metallic;	// 0 - 1
	float	bump;		// how deep the picture's detail is taken to be; 0 = flat
} matinfo_t;

typedef enum
{
	mod_bad,
	mod_world,		// the map itself; its triangles live in the backend
	mod_inline,		// "*N": a door, lift, ... inside the map
	mod_alias,
	mod_sprite
} modtype_t;

typedef struct model_s
{
	char		name[MAX_QPATH];
	int			registration_sequence;		// 0 = free
	modtype_t	type;
	int			inlinenum;					// mod_inline
	void		*data;						// mod_alias: dmdl_t, mod_sprite: dsprite_t; byte swapped file
	image_t		*skins[MAX_MD2SKINS];		// mod_alias: skins, mod_sprite: frames
	int			numskins;
} model_t;

typedef struct
{
	HINSTANCE	hInstance;
	void		*wndproc;
	HWND		hWnd;
	qboolean	fullscreen;
	qboolean	changed_display;	// we called ChangeDisplaySettings

	int			width, height;
	uint32_t	*overlay;			// everything 2D drawn this frame
	pt_backend_t *backend;
} rptstate_t;

extern	refimport_t	ri;
extern	rptstate_t	rpt;
extern	int			registration_sequence;

extern	uint32_t	d_8to24table[256];		// game palette, index 255 transparent

//
// rpt_image.c
//
void	R_InitImages (void);
void	R_ShutdownImages (void);
image_t	*R_FindImage (char *name, imagetype_t type);
void	R_FreeUnusedImages (void);
struct image_s *R_RegisterSkin (char *name);

//
// rpt_draw.c
//
void	Draw_InitLocal (void);
image_t	*Draw_FindPic (char *name);
void	Draw_GetPicSize (int *w, int *h, char *pic);
void	Draw_Pic (int x, int y, char *pic);
void	Draw_StretchPic (int x, int y, int w, int h, char *pic);
void	Draw_Char (int x, int y, int c);
void	Draw_String (int x, int y, const char *s);
void	Draw_TileClear (int x, int y, int w, int h, char *pic);
void	Draw_Fill (int x, int y, int w, int h, int c);
void	Draw_FadeScreen (void);
void	Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data);
void	R_SetPalette (const unsigned char *palette);
void	Draw_Blend (int x, int y, int w, int h, float *blend);

int		R_ImageTexture (image_t *image);
int		R_ImageNormalTexture (image_t *image);

//
// rpt_material.c
//
void	R_InitMaterials (void);
void	R_MaterialInfo (const char *name, matinfo_t *info);
uint32_t *R_ImageNormalMap (image_t *image, const matinfo_t *info);

//
// rpt_world.c
//
void	R_LoadWorld (char *name, char *skyname);
int		R_InlineModel (int num, float **positions, float **uvs, uint32_t **materials);
void	R_WorldMaterial (int index, pt_material_t *material, image_t **image);

// see rpt_world.c: brightness of a light entity or dynamic light of strength l
#define	LIGHT_UNIT	(3.14159265f / 255.0f)
#define	POINT_LIGHT_INTENSITY(l)	((l) * (l) * (l) / 8.0f * LIGHT_UNIT)

//
// rpt_model.c
//
struct model_s *R_RegisterModel (char *name);
void	R_BeginModelRegistration (void);
void	R_FreeUnusedModels (void);
void	R_ShutdownModels (void);

//
// rpt_scene.c
//
void	R_BuildScene (refdef_t *fd, pt_scene_t *scene);
