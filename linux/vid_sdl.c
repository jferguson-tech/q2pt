/*
Copyright (C) 1997-2001 Id Software, Inc.

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
// vid_sdl.c -- loading the renderer libraries, video modes, and the keyboard
// and mouse, where there is no Win32. The renderer makes the window with SDL
// (ref_pt/rpt_sdl.c); SDL's events for it arrive here.

#include <assert.h>
#include <dlfcn.h>
#include <SDL.h>

#include "../client/client.h"

// Structure containing functions exported from refresh DLL
refexport_t	re;

// Console variables that we need to access from this module
cvar_t		*vid_gamma;
cvar_t		*vid_ref;			// Name of Refresh DLL loaded
cvar_t		*vid_xpos;			// X coordinate of window position
cvar_t		*vid_ypos;			// Y coordinate of window position
cvar_t		*vid_fullscreen;

// Global variables used internally by this module
viddef_t	viddef;				// global video state; used by other modules
static void	*reflib_library;	// Handle to refresh DLL
static qboolean	reflib_active = 0;
static SDL_Window	*vid_window;	// set by the renderer through R_Init

extern	unsigned	sys_frame_time;

static cvar_t	*in_mouse;
cvar_t		*in_joystick;		// the menu has a setting for it; there is no joystick code here
static cvar_t	*m_filter;
static qboolean	mouse_active;
static qboolean	mlooking;
static int		mouse_dx, mouse_dy;
static int		old_mouse_x, old_mouse_y;
static qboolean	app_active = true;

/*
==========================================================================

DLL GLUE

==========================================================================
*/

#define	MAXPRINTMSG	4096
void VID_Printf (int print_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsnprintf (msg, sizeof(msg), fmt,argptr);
	va_end (argptr);

	if (print_level == PRINT_ALL)
		Com_Printf ("%s", msg);
	else
		Com_DPrintf ("%s", msg);
}

void VID_Error (int err_level, char *fmt, ...)
{
	va_list		argptr;
	char		msg[MAXPRINTMSG];

	va_start (argptr,fmt);
	vsnprintf (msg, sizeof(msg), fmt,argptr);
	va_end (argptr);

	Com_Error (err_level,"%s", msg);
}

//==========================================================================

/*
============
VID_Restart_f

Console command to re-start the video mode and refresh DLL.
============
*/
void VID_Restart_f (void)
{
	vid_ref->modified = true;
}

/*
============
VID_PtCycle_f

Steps CPU path traced -> RTX path traced -> CPU. The original renderers are
not built here. The RTX step is skipped once it has failed to start in this
session.
============
*/
static qboolean	vid_rtx_failed;

void VID_PtCycle_f (void)
{
	if ( strcmp (vid_ref->string, "ptcpu") == 0 && !vid_rtx_failed )
	{
		Com_Printf ("Renderer: RTX path traced\n");
		Cvar_Set ("vid_ref", "ptrtx");
	}
	else if ( strcmp (vid_ref->string, "ptcpu") != 0 )
	{
		Com_Printf ("Renderer: CPU path traced\n");
		Cvar_Set ("vid_ref", "ptcpu");
	}
}

/*
** VID_GetModeInfo
*/
typedef struct vidmode_s
{
	const char *description;
	int         width, height;
	int         mode;
} vidmode_t;

// the same list as win32/vid_dll.c
vidmode_t vid_modes[] =
{
	{ "Mode 0: 320x240",   320, 240,   0 },
	{ "Mode 1: 400x300",   400, 300,   1 },
	{ "Mode 2: 512x384",   512, 384,   2 },
	{ "Mode 3: 640x480",   640, 480,   3 },
	{ "Mode 4: 800x600",   800, 600,   4 },
	{ "Mode 5: 960x720",   960, 720,   5 },
	{ "Mode 6: 1024x768",  1024, 768,  6 },
	{ "Mode 7: 1152x864",  1152, 864,  7 },
	{ "Mode 8: 1280x960",  1280, 960, 8 },
	{ "Mode 9: 1600x1200", 1600, 1200, 9 },
	{ "Mode 10: 1280x720", 1280, 720, 10 },
	{ "Mode 11: 1920x1080", 1920, 1080, 11 },
	{ "Mode 12: 2560x1440", 2560, 1440, 12 },
	{ "Mode 13: 3840x2160", 3840, 2160, 13 },
	{ "Mode 14: 1366x768", 1366, 768, 14 },
	{ "Mode 15: 1600x900", 1600, 900, 15 },
	{ "Mode 16: 2560x1080", 2560, 1080, 16 },
	{ "Mode 17: 3440x1440", 3440, 1440, 17 },
	{ "Mode 18: 3840x1600", 3840, 1600, 18 },
	{ "Mode 19: 3840x1080", 3840, 1080, 19 },
	{ "Mode 20: 5120x1440", 5120, 1440, 20 },
	{ "Mode 21: desktop", 0, 0, 21 }
};

#define VID_NUM_MODES ( sizeof( vid_modes ) / sizeof( vid_modes[0] ) )

qboolean VID_GetModeInfo( int *width, int *height, int mode )
{
	SDL_DisplayMode	desktop;

	if ( mode < 0 || mode >= VID_NUM_MODES )
		return false;

	*width  = vid_modes[mode].width;
	*height = vid_modes[mode].height;

	// the mode without a size is whatever the desktop is
	if ( !*width )
	{
		if ( SDL_WasInit (SDL_INIT_VIDEO) && SDL_GetDesktopDisplayMode (0, &desktop) == 0 )
		{
			*width  = desktop.w;
			*height = desktop.h;
		}
		else
		{
			*width  = 1920;
			*height = 1080;
		}
	}

	return true;
}

/*
** VID_NewWindow
*/
void VID_NewWindow ( int width, int height)
{
	viddef.width  = width;
	viddef.height = height;

	cl.force_refdef = true;		// can't use a paused refdef
}

void VID_FreeReflib (void)
{
	if (reflib_library)
		dlclose (reflib_library);
	memset (&re, 0, sizeof(re));
	reflib_library = NULL;
	reflib_active  = false;
	vid_window = NULL;
}

/*
==============
VID_LoadRefresh
==============
*/
qboolean VID_LoadRefresh( char *name )
{
	refimport_t	ri;
	GetRefAPI_t	GetRefAPI;

	if ( reflib_active )
	{
		re.Shutdown();
		VID_FreeReflib ();
	}

	Com_Printf( "------- Loading %s -------\n", name );

	if ( ( reflib_library = dlopen( name, RTLD_NOW | RTLD_LOCAL ) ) == 0 )
	{
		Com_Printf( "dlopen(\"%s\") failed: %s\n", name, dlerror() );
		return false;
	}

	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Con_Printf = VID_Printf;
	ri.Sys_Error = VID_Error;
	ri.FS_LoadFile = FS_LoadFile;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_Gamedir = FS_Gamedir;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.Vid_GetModeInfo = VID_GetModeInfo;
	ri.Vid_MenuInit = VID_MenuInit;
	ri.Vid_NewWindow = VID_NewWindow;

	if ( ( GetRefAPI = (GetRefAPI_t) dlsym( reflib_library, "GetRefAPI" ) ) == 0 )
		Com_Error( ERR_FATAL, "dlsym failed on %s", name );

	re = GetRefAPI( ri );

	if (re.api_version != API_VERSION)
	{
		VID_FreeReflib ();
		Com_Error (ERR_FATAL, "%s has incompatible api_version", name);
	}

	// the renderer notes its window in vid_window
	if ( re.Init( &vid_window, NULL ) == -1 )
	{
		re.Shutdown();
		VID_FreeReflib ();
		return false;
	}

	Com_Printf( "------------------------------------\n");
	reflib_active = true;
	vidref_val = VIDREF_OTHER;
	mouse_active = false;		// the new window has not taken the mouse yet

	return true;
}

/*
============
VID_SetTitle

The window's title bar, for showing progress where it is not in the picture
============
*/
void VID_SetTitle (char *title)
{
	if (vid_window)
		SDL_SetWindowTitle (vid_window, title ? title : "Quake 2");
}

/*
============
VID_CheckChanges

This function gets called once just before drawing each frame, and it's sole purpose in life
is to check to see if any of the video mode parameters have changed, and if they have to
update the rendering DLL and/or video mode to match.
============
*/
void VID_CheckChanges (void)
{
	char name[MAX_OSPATH];

	if ( vid_ref->modified )
	{
		cl.force_refdef = true;		// can't use a paused refdef
		S_StopAllSounds();
	}
	while (vid_ref->modified)
	{
		/*
		** refresh has changed
		*/
		vid_ref->modified = false;
		vid_fullscreen->modified = true;
		cl.refresh_prepped = false;
		cls.disable_screen = true;

		Com_sprintf( name, sizeof(name), "./ref_%s.so", vid_ref->string );
		if ( !VID_LoadRefresh( name ) )
		{
			// anything -> ptrtx -> ptcpu
			if ( strcmp (vid_ref->string, "ptcpu") == 0 )
				Com_Error (ERR_FATAL, "Couldn't start the CPU path tracer either");

			if ( strcmp (vid_ref->string, "ptrtx") == 0 )
			{
				vid_rtx_failed = true;
				Com_Printf ("Couldn't start ref_ptrtx, falling back to ref_ptcpu\n");
				Cvar_Set( "vid_ref", "ptcpu" );
			}
			else
			{
				Com_Printf ("ref_%s is not built here, using ref_ptrtx\n", vid_ref->string);
				Cvar_Set( "vid_ref", "ptrtx" );
			}
		}
		cls.disable_screen = false;
	}
}

/*
============
VID_Init
============
*/
void VID_Init (void)
{
	/* Create the video variables so we know how to start the graphics drivers */
	vid_ref = Cvar_Get ("vid_ref", "ptrtx", CVAR_ARCHIVE);
	vid_xpos = Cvar_Get ("vid_xpos", "0", CVAR_ARCHIVE);
	vid_ypos = Cvar_Get ("vid_ypos", "0", CVAR_ARCHIVE);
	vid_fullscreen = Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_gamma = Cvar_Get( "vid_gamma", "1", CVAR_ARCHIVE );

	/* Add some console commands that we want to handle */
	Cmd_AddCommand ("vid_restart", VID_Restart_f);
	Cmd_AddCommand ("pt_cycle", VID_PtCycle_f);
	if ( !keybindings[K_F8] )
		Key_SetBinding (K_F8, "pt_cycle");

	/* Start the graphics mode and load refresh DLL */
	VID_CheckChanges();
}

/*
============
VID_Shutdown
============
*/
void VID_Shutdown (void)
{
	if ( reflib_active )
	{
		re.Shutdown ();
		VID_FreeReflib ();
	}
}

/*
==========================================================================

KEYBOARD AND WINDOW EVENTS

==========================================================================
*/

static int MapKey (SDL_Keycode sym)
{
	if (sym >= SDLK_SPACE && sym < SDLK_DELETE)
		return sym;		// plain ASCII, lower case letters

	switch (sym)
	{
	case SDLK_TAB:			return K_TAB;
	case SDLK_RETURN:		return K_ENTER;
	case SDLK_ESCAPE:		return K_ESCAPE;
	case SDLK_BACKSPACE:	return K_BACKSPACE;
	case SDLK_UP:			return K_UPARROW;
	case SDLK_DOWN:			return K_DOWNARROW;
	case SDLK_LEFT:			return K_LEFTARROW;
	case SDLK_RIGHT:		return K_RIGHTARROW;
	case SDLK_LALT:
	case SDLK_RALT:			return K_ALT;
	case SDLK_LCTRL:
	case SDLK_RCTRL:		return K_CTRL;
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:		return K_SHIFT;
	case SDLK_F1:			return K_F1;
	case SDLK_F2:			return K_F2;
	case SDLK_F3:			return K_F3;
	case SDLK_F4:			return K_F4;
	case SDLK_F5:			return K_F5;
	case SDLK_F6:			return K_F6;
	case SDLK_F7:			return K_F7;
	case SDLK_F8:			return K_F8;
	case SDLK_F9:			return K_F9;
	case SDLK_F10:			return K_F10;
	case SDLK_F11:			return K_F11;
	case SDLK_F12:			return K_F12;
	case SDLK_INSERT:		return K_INS;
	case SDLK_DELETE:		return K_DEL;
	case SDLK_PAGEDOWN:		return K_PGDN;
	case SDLK_PAGEUP:		return K_PGUP;
	case SDLK_HOME:			return K_HOME;
	case SDLK_END:			return K_END;
	case SDLK_PAUSE:		return K_PAUSE;
	case SDLK_KP_7:			return K_KP_HOME;
	case SDLK_KP_8:			return K_KP_UPARROW;
	case SDLK_KP_9:			return K_KP_PGUP;
	case SDLK_KP_4:			return K_KP_LEFTARROW;
	case SDLK_KP_5:			return K_KP_5;
	case SDLK_KP_6:			return K_KP_RIGHTARROW;
	case SDLK_KP_1:			return K_KP_END;
	case SDLK_KP_2:			return K_KP_DOWNARROW;
	case SDLK_KP_3:			return K_KP_PGDN;
	case SDLK_KP_ENTER:		return K_KP_ENTER;
	case SDLK_KP_0:			return K_KP_INS;
	case SDLK_KP_PERIOD:	return K_KP_DEL;
	case SDLK_KP_DIVIDE:	return K_KP_SLASH;
	case SDLK_KP_MINUS:		return K_KP_MINUS;
	case SDLK_KP_PLUS:		return K_KP_PLUS;
	case SDLK_KP_MULTIPLY:	return '*';
	default:				return 0;
	}
}

static int MapButton (int button)
{
	switch (button)
	{
	case SDL_BUTTON_LEFT:	return K_MOUSE1;
	case SDL_BUTTON_RIGHT:	return K_MOUSE2;
	case SDL_BUTTON_MIDDLE:	return K_MOUSE3;
	default:				return 0;
	}
}

void Sys_SendKeyEvents (void)
{
	SDL_Event	ev;
	int			key;
	unsigned	time;

	if (!SDL_WasInit (SDL_INIT_VIDEO))
	{
		sys_frame_time = Sys_Milliseconds ();
		return;
	}

	while (SDL_PollEvent (&ev))
	{
		time = Sys_Milliseconds ();
		switch (ev.type)
		{
		case SDL_QUIT:
			Cbuf_ExecuteText (EXEC_APPEND, "quit\n");
			break;

		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_CLOSE)
				Cbuf_ExecuteText (EXEC_APPEND, "quit\n");
			else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
			{
				app_active = true;
				S_Activate (true);
			}
			else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
			{
				app_active = false;
				Key_ClearStates ();
				S_Activate (false);
			}
			else if (ev.window.event == SDL_WINDOWEVENT_EXPOSED)
				SCR_DirtyScreen ();
			break;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
			// Alt+Enter: in and out of full screen
			if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))
			{
				if (!ev.key.repeat)
					Cvar_SetValue ("vid_fullscreen", !vid_fullscreen->value);
				break;
			}
			// Alt+F4, which a window that holds the mouse is not sent as a close
			if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F4 && (ev.key.keysym.mod & KMOD_ALT))
			{
				Cbuf_ExecuteText (EXEC_APPEND, "quit\n");
				break;
			}
			key = MapKey (ev.key.keysym.sym);
			if (key)
				Key_Event (key, ev.type == SDL_KEYDOWN, time);
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			key = MapButton (ev.button.button);
			if (key)
				Key_Event (key, ev.type == SDL_MOUSEBUTTONDOWN, time);
			break;

		case SDL_MOUSEWHEEL:
			key = ev.wheel.y > 0 ? K_MWHEELUP : (ev.wheel.y < 0 ? K_MWHEELDOWN : 0);
			if (key)
			{
				Key_Event (key, true, time);
				Key_Event (key, false, time);
			}
			break;

		case SDL_MOUSEMOTION:
			if (mouse_active)
			{
				mouse_dx += ev.motion.xrel;
				mouse_dy += ev.motion.yrel;
			}
			break;
		}
	}

	// the renderer made its window with the old setting: make it again
	if (vid_fullscreen->modified && reflib_active)
	{
		vid_fullscreen->modified = false;
		vid_ref->modified = true;
	}

	// grab frame time
	sys_frame_time = Sys_Milliseconds ();
}

/*
==========================================================================

MOUSE

==========================================================================
*/

static void IN_MLookDown (void) { mlooking = true; }
static void IN_MLookUp (void)
{
	mlooking = false;
	if (!freelook->value && lookspring->value)
		IN_CenterView ();
}

static void IN_ActivateMouse (qboolean active)
{
	if (active == mouse_active)
		return;
	mouse_active = active;
	mouse_dx = mouse_dy = 0;
	old_mouse_x = old_mouse_y = 0;
	SDL_SetRelativeMouseMode (active ? SDL_TRUE : SDL_FALSE);
}

void IN_Init (void)
{
	in_mouse = Cvar_Get ("in_mouse", "1", CVAR_ARCHIVE);
	m_filter = Cvar_Get ("m_filter", "0", 0);
	in_joystick = Cvar_Get ("in_joystick", "0", CVAR_ARCHIVE);

	Cmd_AddCommand ("+mlook", IN_MLookDown);
	Cmd_AddCommand ("-mlook", IN_MLookUp);
}

void IN_Shutdown (void)
{
	if (SDL_WasInit (SDL_INIT_VIDEO))
		IN_ActivateMouse (false);
}

void IN_Activate (qboolean active)
{
	app_active = active;
}

void IN_Commands (void)
{
}

/*
Called every frame, even if not generating commands: the game holds the
mouse while it is being played, and lets go for the console and the menus
unless it is full screen.
*/
void IN_Frame (void)
{
	if (!SDL_WasInit (SDL_INIT_VIDEO) || !reflib_active)
		return;

	if (!in_mouse->value || !app_active)
	{
		IN_ActivateMouse (false);
		return;
	}
	if (!cl.refresh_prepped || cls.key_dest == key_console || cls.key_dest == key_menu)
	{
		// temporarily deactivate if not full screen
		if (!vid_fullscreen->value)
		{
			IN_ActivateMouse (false);
			return;
		}
	}
	IN_ActivateMouse (true);
}

void IN_Move (usercmd_t *cmd)
{
	int		mx, my;

	if (!mouse_active)
		return;

	if (m_filter->value)
	{
		mx = (mouse_dx + old_mouse_x) * 0.5;
		my = (mouse_dy + old_mouse_y) * 0.5;
	}
	else
	{
		mx = mouse_dx;
		my = mouse_dy;
	}
	old_mouse_x = mouse_dx;
	old_mouse_y = mouse_dy;
	mouse_dx = mouse_dy = 0;

	mx *= sensitivity->value;
	my *= sensitivity->value;

	// add mouse X/Y movement to cmd
	if ( (in_strafe.state & 1) || (lookstrafe->value && mlooking ))
		cmd->sidemove += m_side->value * mx;
	else
		cl.viewangles[YAW] -= m_yaw->value * mx;

	if ( (mlooking || freelook->value) && !(in_strafe.state & 1))
		cl.viewangles[PITCH] += m_pitch->value * my;
	else
		cmd->forwardmove -= m_forward->value * my;
}
