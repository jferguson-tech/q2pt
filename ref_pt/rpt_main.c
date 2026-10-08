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
// rpt_main.c -- refresh entry points, window and backend lifetime

#include "rpt_local.h"

#ifdef RPT_RTX
#define	RPT_CREATE	pt_rtx_create
#define	RPT_LABEL	"RTX PATH TRACER"
#else
#define	RPT_CREATE	pt_cpu_create
#define	RPT_LABEL	"CPU PATH TRACER"
#endif

#ifdef _WIN32
#define	WINDOW_CLASS_NAME	"Quake 2"
#define	WINDOW_STYLE		(WS_OVERLAPPED|WS_BORDER|WS_CAPTION|WS_VISIBLE)
#else
// rpt_sdl.c
qboolean R_SetMode (void);
void R_DestroyWindow (void);
void R_WindowHandles (pt_create_t *ci);
void R_WindowActivate (qboolean active);
#endif


refimport_t	ri;
rptstate_t	rpt;
int			registration_sequence;


static cvar_t	*vid_fullscreen;
static cvar_t	*gl_mode;		// shared with ref_gl so toggling keeps the window size
#ifdef RPT_RTX
static cvar_t	*pt_rtx_disable;
#endif

static char		r_skyname[MAX_QPATH];
static char		r_worldname[MAX_QPATH];
static qboolean	r_worlddirty;
static float		r_skyrotate;
static vec3_t		r_skyaxis;


//=============================================================================

static void R_BackendLog (const char *msg)
{
	ri.Con_Printf (PRINT_ALL, "%s", (char *)msg);
}

#ifdef _WIN32
/*
** R_DestroyWindow
*/
static void R_DestroyWindow (void)
{
	if (rpt.backend)
	{
		rpt.backend->destroy (rpt.backend);
		rpt.backend = NULL;
	}
	if (rpt.overlay)
	{
		free (rpt.overlay);
		rpt.overlay = NULL;
	}
	if (rpt.hWnd)
	{
		DestroyWindow (rpt.hWnd);
		rpt.hWnd = NULL;
		UnregisterClass (WINDOW_CLASS_NAME, rpt.hInstance);
	}
	if (rpt.changed_display)
	{
		ChangeDisplaySettings (0, 0);
		rpt.changed_display = false;
	}
}

/*
** R_CreateWindow
*/
static qboolean R_CreateWindow (int width, int height, qboolean fullscreen)
{
	WNDCLASS	wc;
	RECT		r;
	cvar_t		*vid_xpos, *vid_ypos;
	int			stylebits, exstyle;
	int			x, y;

	memset (&wc, 0, sizeof(wc));
	wc.lpfnWndProc   = (WNDPROC)rpt.wndproc;
	wc.hInstance     = rpt.hInstance;
	wc.hCursor       = LoadCursor (NULL, IDC_ARROW);
	wc.hbrBackground = GetStockObject (BLACK_BRUSH);
	wc.lpszClassName = WINDOW_CLASS_NAME;

	if (!RegisterClass (&wc))
		ri.Sys_Error (ERR_FATAL, "Couldn't register window class");

	if (fullscreen)
	{
		exstyle = WS_EX_TOPMOST;
		stylebits = WS_POPUP|WS_VISIBLE;
		x = 0;
		y = 0;
	}
	else
	{
		exstyle = 0;
		stylebits = WINDOW_STYLE;
		vid_xpos = ri.Cvar_Get ("vid_xpos", "0", 0);
		vid_ypos = ri.Cvar_Get ("vid_ypos", "0", 0);
		x = vid_xpos->value;
		y = vid_ypos->value;
	}

	r.left = 0;
	r.top = 0;
	r.right  = width;
	r.bottom = height;
	AdjustWindowRect (&r, stylebits, FALSE);

	rpt.hWnd = CreateWindowEx (
		exstyle,
		WINDOW_CLASS_NAME,
		"Quake 2",
		stylebits,
		x, y, r.right - r.left, r.bottom - r.top,
		NULL,
		NULL,
		rpt.hInstance,
		NULL);

	if (!rpt.hWnd)
	{
		UnregisterClass (WINDOW_CLASS_NAME, rpt.hInstance);
		ri.Sys_Error (ERR_FATAL, "Couldn't create window");
	}

	ShowWindow (rpt.hWnd, SW_SHOW);
	UpdateWindow (rpt.hWnd);
	SetForegroundWindow (rpt.hWnd);
	SetFocus (rpt.hWnd);

	// Windows makes no window bigger than the desktop. The picture is made
	// the size the window turned out, or its right and bottom would be cut off.
	if (GetClientRect (rpt.hWnd, &r) && r.right > 0 && r.bottom > 0)
	{
		width = r.right;
		height = r.bottom;
	}

	rpt.width = width;
	rpt.height = height;
	rpt.fullscreen = fullscreen;
	return true;
}

/*
** R_SetMode
**
** Fullscreen is a borderless window; the display mode is only changed when
** the requested size is not the desktop's.
*/
static qboolean R_SetMode (void)
{
	int			width, height;
	qboolean	fullscreen;
	DEVMODE		dm;

	fullscreen = vid_fullscreen->value != 0;
	gl_mode->modified = false;
	vid_fullscreen->modified = false;

	ri.Con_Printf (PRINT_ALL, "...setting mode %d:", (int)gl_mode->value);
	if (!ri.Vid_GetModeInfo (&width, &height, gl_mode->value))
	{
		ri.Con_Printf (PRINT_ALL, " invalid mode\n");
		return false;
	}
	ri.Con_Printf (PRINT_ALL, " %d %d %s\n", width, height, fullscreen ? "FS" : "W");

	if (fullscreen && (width != GetSystemMetrics (SM_CXSCREEN) || height != GetSystemMetrics (SM_CYSCREEN)))
	{
		memset (&dm, 0, sizeof(dm));
		dm.dmSize = sizeof(dm);
		dm.dmPelsWidth  = width;
		dm.dmPelsHeight = height;
		dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

		if (ChangeDisplaySettings (&dm, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL)
			rpt.changed_display = true;
		else
		{
			ri.Con_Printf (PRINT_ALL, "...display mode change failed, using a window\n");
			ri.Cvar_SetValue ("vid_fullscreen", 0);
			vid_fullscreen->modified = false;
			fullscreen = false;
		}
	}

	return R_CreateWindow (width, height, fullscreen);
}
#endif

/*
===============
R_Init
===============
*/
qboolean R_Init (void *hInstance, void *wndProc)
{
	pt_create_t	ci;
	char		err[1024];

	ri.Con_Printf (PRINT_ALL, "ref_pt version: "REF_VERSION" ("RPT_LABEL")\n");

#ifdef _WIN32
	rpt.hInstance = (HINSTANCE)hInstance;
	rpt.wndproc = wndProc;

#else
	rpt.window_slot = (void **)hInstance;
#endif

	vid_fullscreen = ri.Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE);
	gl_mode = ri.Cvar_Get ("gl_mode", "3", CVAR_ARCHIVE);

	R_InitSettings ();

#ifdef RPT_RTX
	pt_rtx_disable = ri.Cvar_Get ("pt_rtx_disable", "0", CVAR_ARCHIVE);
	if (pt_rtx_disable->value)
	{
		ri.Con_Printf (PRINT_ALL, "RTX path tracer: turned off by pt_rtx_disable\n");
		return -1;
	}
#endif

	if (!R_SetMode ())
		return -1;

	rpt.overlay = malloc (rpt.width * rpt.height * sizeof(uint32_t));
	memset (rpt.overlay, 0, rpt.width * rpt.height * sizeof(uint32_t));

	memset (&ci, 0, sizeof(ci));
#ifdef _WIN32
	ci.hinstance = rpt.hInstance;
	ci.hwnd = rpt.hWnd;
#else
	R_WindowHandles (&ci);
#endif
	ci.width = rpt.width;
	ci.height = rpt.height;
	ci.log = R_BackendLog;
	// A setting the backend is made with is marked as taken where it is read,
	// here, and not in R_SetMode, of which each system has its own: a mark left
	// standing has R_BeginFrame ask for a new renderer every frame.
	ci.simd = (int)pt_simd->value;
	pt_simd->modified = false;

	err[0] = 0;
	rpt.backend = RPT_CREATE (&ci, err, sizeof(err));
	if (!rpt.backend)
	{
		ri.Con_Printf (PRINT_ALL, RPT_LABEL" unavailable: %s\n", err);
		return -1;		// the engine calls R_Shutdown for us
	}

	// let the sound and input subsystems know about the new window
	ri.Vid_NewWindow (rpt.width, rpt.height);

	R_InitImages ();
	R_InitMaterials ();
	Draw_InitLocal ();
	R_InitShots ();
	R_InitOffline ();
	R_InitExport ();
	R_InitBench ();

	ri.Vid_MenuInit ();

	return true;
}

/*
===============
R_Shutdown

Also called by the engine after a failed R_Init
===============
*/
void R_Shutdown (void)
{
	R_ShutdownBench ();		// while there is still a backend to speak of
	R_ShutdownShots ();
	R_ShutdownOffline ();
	R_ShutdownExport ();
	R_ShutdownMaterials ();
	R_WaterReset ();		// while the backend that holds its pictures is still there
	R_ShutdownImages ();
	R_ShutdownModels ();
	R_SceneShutdown ();
	r_worldname[0] = 0;
	r_skyname[0] = 0;
	r_worlddirty = false;
	R_DestroyWindow ();
}


/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginRegistration

Specifies the model that will be used as the world
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginRegistration (char *model)
{
	char	fullname[MAX_QPATH];

	registration_sequence++;

	Com_sprintf (fullname, sizeof(fullname), "maps/%s.bsp", model);
	R_RegisterModel (fullname);

	strcpy (r_worldname, fullname);
	r_worlddirty = true;
}

/*
@@@@@@@@@@@@@@@@@@@@@
R_EndRegistration
@@@@@@@@@@@@@@@@@@@@@
*/
void R_EndRegistration (void)
{
	R_FreeUnusedModels ();
	R_FreeUnusedImages ();
	R_MakeSkinMaterials ();
}

/*
============
R_SetSky
============
*/
void R_SetSky (char *name, float rotate, vec3_t axis)
{
	if (strcmp (r_skyname, name))
		r_worlddirty = true;
	strncpy (r_skyname, name, sizeof(r_skyname)-1);
	r_skyname[sizeof(r_skyname)-1] = 0;
	r_skyrotate = rotate;
	VectorCopy (axis, r_skyaxis);
}

//=============================================================================

/*
@@@@@@@@@@@@@@@@@@@@@
R_BeginFrame
@@@@@@@@@@@@@@@@@@@@@
*/
void R_BeginFrame (float camera_separation)
{
	/*
	** change modes if necessary, or make the backend again with a setting
	** it is made with (R_Init takes the mark off those)
	*/
	if (gl_mode->modified || vid_fullscreen->modified || pt_simd->modified)
	{	// FIXME: only restart if CDS is required
		cvar_t	*ref;

		ref = ri.Cvar_Get ("vid_ref", "gl", 0);
		ref->modified = true;
	}

	Draw_ClearOverlay ();
}

static void R_DrawStats (refdef_t *fd);

/*
@@@@@@@@@@@@@@@@@@@@@
R_RenderFrame
@@@@@@@@@@@@@@@@@@@@@
*/
void R_RenderFrame (refdef_t *fd)
{
	pt_view_t	view;
	pt_scene_t	scene;
	static float	styles[MAX_LIGHTSTYLES];
	int			i;

	if (fd->rdflags & RDF_NOWORLDMODEL)
		return;		// menu model previews

	if (R_UpdateSettings ())
		r_worlddirty = true;

	if (r_worlddirty)
	{
		r_worlddirty = false;
		R_LoadWorld (r_worldname, r_skyname);
		R_MaterialsReport ();
	}

	R_WaterFrame (fd);		// before the scene is built, and where the player really is

	// a fixed camera, for looking at a place without walking there
	{
		static cvar_t	*pt_camera;
		float			c[5];

		if (!pt_camera)
			pt_camera = ri.Cvar_Get ("pt_camera", "", 0);
		if (sscanf (pt_camera->string, "%f%*[ ,]%f%*[ ,]%f%*[ ,]%f%*[ ,]%f", &c[0], &c[1], &c[2], &c[3], &c[4]) == 5)
		{
			VectorSet (fd->vieworg, c[0], c[1], c[2]);
			VectorSet (fd->viewangles, c[3], c[4], 0);
			fd->rdflags &= ~RDF_UNDERWATER;
		}
	}

	memset (&view, 0, sizeof(view));
	view.x = fd->x;
	view.y = fd->y;
	view.width = fd->width;
	view.height = fd->height;
	view.time = fd->time;
	VectorCopy (fd->vieworg, view.origin);
	AngleVectors (fd->viewangles, view.forward, view.right, view.up);
	view.fov_x = fd->fov_x;
	view.fov_y = fd->fov_y;
	R_ViewSettings (&view);
	if (fd->rdflags & RDF_UNDERWATER)
		R_WaterAround (fd->vieworg, view.medium_absorb);
	R_BuildScene (fd, &scene);
	view.scene = &scene;

	for (i=0 ; i<MAX_LIGHTSTYLES ; i++)
		styles[i] = fd->lightstyles ? fd->lightstyles[i].white : 1;
	view.light_styles = styles;
	view.num_light_styles = MAX_LIGHTSTYLES;
	view.anim_frame = (int)(fd->time * 2);
	VectorCopy (r_skyaxis, view.sky_axis);
	view.sky_angle = fd->time * r_skyrotate;
	if (R_Offline ())
		R_OfflineRender (fd, &view);
	else
	{	// more than once only for a screenshot
		for (i=R_ShotPasses (&view) ; i>0 ; i--)
			rpt.backend->render_view (rpt.backend, &view);
		R_BenchView ();
	}

	// damage flashes, underwater tint and the like
	Draw_Blend (fd->x, fd->y, fd->width, fd->height, fd->blend);

	if (pt_stats->value && !R_Offline ())
		R_DrawStats (fd);
	if (!R_Offline ())
		R_DrawFilterPanel (fd);
}

/*
===============
R_DrawStats

pt_stats 1: how fast the game is running and where the time goes, in the
corner of the view. 2: also in the console now and then.

The rates are of whole frames, from one being shown to the next, so they
include the game itself; the backend's lines are about the picture alone.
===============
*/
static int		perf_last;				// when the last frame was shown
static int		perf_since, perf_frames, perf_worst;
static float	perf_fps, perf_ms, perf_worst_ms;

static void R_CountFrame (void)
{
	int		now, took;

	now = Sys_Milliseconds ();
	took = now - perf_last;
	perf_last = now;
	if (took < 0 || took > 2000)
	{	// the first frame, or a pause: start counting again
		perf_since = now;
		perf_frames = perf_worst = 0;
		return;
	}

	perf_frames++;
	if (took > perf_worst)
		perf_worst = took;
	if (now - perf_since >= 500)
	{
		perf_ms = (float)(now - perf_since) / perf_frames;
		perf_fps = 1000.0f / perf_ms;
		perf_worst_ms = perf_worst;
		perf_since = now;
		perf_frames = perf_worst = 0;
	}
}

static void R_DrawStats (refdef_t *fd)
{
	static int	count;
	char		lines[6][80];
	char		text[400];
	char		*s, *bar;
	int			i, num, width, x, y;

	num = 0;
	Com_sprintf (lines[num++], sizeof(lines[0]), "%3.0f fps  %.1f ms  worst %.0f ms", perf_fps, perf_ms, perf_worst_ms);
	Com_sprintf (lines[num++], sizeof(lines[0]), "%s  %dx%d", rpt.backend->name, rpt.width, rpt.height);

	strncpy (text, rpt.backend->stats (rpt.backend), sizeof(text) - 1);
	text[sizeof(text) - 1] = 0;
	if (pt_stats->value >= 2 && !(++count % 20))
	{
		char	flat[400];

		strcpy (flat, text);
		for (s=flat ; *s ; s++)
			if (*s == '|')
				*s = ' ';
		ri.Con_Printf (PRINT_ALL, "pt: %.0f fps %s\n", perf_fps, flat);
	}
	for (s=text ; s && *s && num<6 ; s=bar)
	{
		bar = strchr (s, '|');
		if (bar)
			*bar++ = 0;
		Com_sprintf (lines[num++], sizeof(lines[0]), "%.78s", s);
	}

	width = 0;
	for (i=0 ; i<num ; i++)
		if ((int)strlen (lines[i]) > width)
			width = strlen (lines[i]);

	// top right, clear of the console's notify lines, on a dark plate
	x = fd->x + fd->width - width * 8 - 12;
	y = fd->y + 4;
	if (x < fd->x)
		x = fd->x;
	Draw_FadeBox (x, y, width * 8 + 8, num * 10 + 6);
	for (i=0 ; i<num ; i++)
		Draw_String (x + 4, y + 4 + i * 10, lines[i]);
}

/*
@@@@@@@@@@@@@@@@@@@@@
R_EndFrame
@@@@@@@@@@@@@@@@@@@@@
*/
void R_EndFrame (void)
{
	pt_rect_t	*changed;
	int			num;

	num = Draw_Changed (&changed);
	rpt.backend->present (rpt.backend, rpt.overlay, changed, num);
	R_CountFrame ();
	R_BenchFrame ();
	R_ShotFinish ();
	R_OfflineFinish ();
}

/*
** R_AppActivate
*/
void R_AppActivate (qboolean active)
{
#ifndef _WIN32
	R_WindowActivate (active);
#else
	if (!rpt.hWnd)
		return;

	if (active)
	{
		SetForegroundWindow (rpt.hWnd);
		ShowWindow (rpt.hWnd, SW_RESTORE);
	}
	else
	{
		if (rpt.fullscreen)
			ShowWindow (rpt.hWnd, SW_MINIMIZE);
	}
#endif
}

//=============================================================================

/*
@@@@@@@@@@@@@@@@@@@@@
GetRefAPI

@@@@@@@@@@@@@@@@@@@@@
*/
refexport_t GetRefAPI (refimport_t rimp)
{
	refexport_t	re;

	ri = rimp;

	re.api_version = API_VERSION;

	re.BeginRegistration = R_BeginRegistration;
	re.RegisterModel = R_RegisterModel;
	re.RegisterSkin = R_RegisterSkin;
	re.RegisterPic = Draw_FindPic;
	re.SetSky = R_SetSky;
	re.EndRegistration = R_EndRegistration;

	re.RenderFrame = R_RenderFrame;

	re.DrawGetPicSize = Draw_GetPicSize;
	re.DrawPic = Draw_Pic;
	re.DrawStretchPic = Draw_StretchPic;
	re.DrawChar = Draw_Char;
	re.DrawTileClear = Draw_TileClear;
	re.DrawFill = Draw_Fill;
	re.DrawFadeScreen = Draw_FadeScreen;

	re.DrawStretchRaw = Draw_StretchRaw;

	re.Init = R_Init;
	re.Shutdown = R_Shutdown;

	re.CinematicSetPalette = R_SetPalette;
	re.BeginFrame = R_BeginFrame;
	re.EndFrame = R_EndFrame;

	re.AppActivate = R_AppActivate;

	Swap_Init ();

	return re;
}

// this is only here so the functions in q_shared.c and q_shwin.c can link
void Sys_Error (char *error, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, error);
	vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	ri.Sys_Error (ERR_FATAL, "%s", text);
}

void Com_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[1024];

	va_start (argptr, fmt);
	vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);

	ri.Con_Printf (PRINT_ALL, "%s", text);
}
