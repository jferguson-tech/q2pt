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

#ifdef _WIN32
#include <windows.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
	int			normal_width, normal_height;
	int			pt_normal_texture;			// backend handle + 1 for normalmap
	qboolean	normal_metal;				// normalmap's blue is how metallic, not the normal's z
	qboolean	normal_byhand;				// a map made by hand went into normalmap
	uint32_t	*colour;					// made with normalmap: pixels with the painted light
											// taken out, or NULL where pixels are as good
	uint32_t	*lit;						// made on demand, see R_ImageLit: pixels where the picture
											// gives off light and black elsewhere, or NULL
	float		lit_share;					// how much of the picture's light is in lit
	qboolean	lit_read;					// lit is what the picture gave, NULL included
} image_t;

typedef struct
{
	float	roughness;	// 0 mirror - 1 matte
	float	metallic;	// how metallic its metal is: 1, or 0 for what is not metal. Where the
						// metal is, its picture says.
	float	bump;		// how deep the picture's detail is taken to be; 0 = flat
	float	glow;		// how strongly the bright parts of the picture light up; 0 = not at all
	qboolean	metal_known;	// it is metal, and its picture shows only what covers the metal;
								// else the picture says whether there is any
	float	metallic_unread;	// what the whole of it is given where its picture is not read
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
#ifdef _WIN32
	HINSTANCE	hInstance;
	void		*wndproc;
	HWND		hWnd;
#else
	void		**window_slot;		// the engine's own note of the window, see rpt_sdl.c
	void		*window;			// SDL_Window
#endif
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
void	Draw_FadeBox (int x, int y, int w, int h);
void	Draw_FadeScreen (void);
void	Draw_Touch (int x0, int y0, int x1, int y1);
void	Draw_ClearOverlay (void);
int		Draw_Changed (pt_rect_t **rects);
void	Draw_StretchRaw (int x, int y, int w, int h, int cols, int rows, byte *data);
void	R_SetPalette (const unsigned char *palette);
void	Draw_Blend (int x, int y, int w, int h, float *blend);

int		R_ImageTexture (image_t *image);
const uint32_t *R_ImageColours (image_t *image);
int		R_ImageNormalTexture (image_t *image);
void	R_MakeSkinMaterials (void);
void	R_MaterialsChanged (void);

//
// rpt_material.c
//
void	R_InitMaterials (void);
void	R_ShutdownMaterials (void);
void	R_MaterialsReport (void);
void	R_MaterialInfo (const char *name, matinfo_t *info);
float	R_MetalColour (void);
uint32_t *R_ImageNormalMap (image_t *image, const matinfo_t *info, int *width, int *height);
image_t	*R_ImageGlowMap (image_t *image);
const uint32_t *R_ImageLit (image_t *image, float *share);

// A light's picture gives off its light from the part of it that is lit only
// if this much of the light its colours stand for is in that part
#define	LIT_SHARE_LEAST		0.4f

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
// rpt_settings.c
//
extern	cvar_t	*pt_stats;
extern	cvar_t	*pt_debug;
extern	cvar_t	*pt_simd;
extern	cvar_t	*pt_material_cache;
extern	float	r_skyscale, r_lampglow;
extern	float	r_surfacelight, r_pointlight, r_liquidglow;
extern	float	r_detailglow;
extern	int		r_normalflip;
extern	int		r_watermode;		// 0 classic, 1 realistic, 2 simulated
extern	float	r_waterreach;
extern	float	r_watercell, r_waterwaves, r_watercaustics, r_waterdamping;
extern	float	r_bumpscale, r_roughscale, r_metalscale;
extern	int		r_materialmaps;		// detail maps are read from the pictures' painted light
extern	float	r_materialdelight;
extern	float	r_metaledge;	// and this much of that light is taken out of their colours, 0 - 1

void	R_InitSettings (void);
qboolean R_UpdateSettings (void);
void	R_ViewSettings (pt_view_t *view);
void	R_DrawFilterPanel (refdef_t *fd);

//
// rpt_shot.c
//
void	R_InitShots (void);
void	R_ShutdownShots (void);
int		R_ShotPasses (pt_view_t *view);
void	R_ShotFinish (void);

//
// rpt_offline.c
//
void	R_InitOffline (void);
void	R_ShutdownOffline (void);
qboolean R_Offline (void);
void	R_OfflineSettings (pt_view_t *view);
void	R_OfflineRender (refdef_t *fd, pt_view_t *view);
void	R_OfflineFinish (void);

//
// rpt_bench.c
//
void	R_InitBench (void);
void	R_ShutdownBench (void);
void	R_BenchView (void);
void	R_BenchFrame (void);

//
// rpt_water.c
//
void	R_WaterReset (void);
void	R_WaterAbsorb (image_t *image, const char *name, float *absorb);
int		R_WaterBody (image_t *image, const char *name, float z, float points[][3], int numpoints);
void	R_WaterSetMaterial (int body, int material);
void	R_WaterFinish (void);
void	R_WaterFrame (refdef_t *fd);
qboolean	R_WaterEyeUnder (const float *eye, float strength, qboolean under);
void	R_WaterAround (const float *eye, float *absorb);
pt_material_t *R_WorldMaterialPtr (int index);
float	R_EntityMoved (int index, entity_t *e);

//
// rpt_model.c
//
struct model_s *R_RegisterModel (char *name);
qboolean R_IsModel (struct model_s *mod);
void	R_BeginModelRegistration (void);
void	R_FreeUnusedModels (void);
void	R_ShutdownModels (void);

//
// rpt_scene.c
//
void	R_BuildScene (refdef_t *fd, pt_scene_t *scene);
void	R_SceneShutdown (void);
