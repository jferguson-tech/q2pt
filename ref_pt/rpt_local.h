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
} image_t;

typedef struct model_s
{
	char		name[MAX_QPATH];
	int			registration_sequence;		// 0 = free
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
