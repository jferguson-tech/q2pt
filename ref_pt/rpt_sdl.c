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
// rpt_sdl.c -- the window where there is no Win32: made with SDL, on X11.
// The engine reads the keyboard and mouse from SDL itself (linux/vid_sdl.c);
// the backends are handed the X display and window underneath.

#include "rpt_local.h"

#include <SDL.h>
#include <SDL_syswm.h>

/*
** R_DestroyWindow
*/
void R_DestroyWindow (void)
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
	if (rpt.window)
	{
		SDL_DestroyWindow ((SDL_Window *)rpt.window);
		rpt.window = NULL;
		if (rpt.window_slot)
			*rpt.window_slot = NULL;
	}
}

/*
** R_SetMode
**
** Fullscreen is a borderless window the size of the desktop when that is
** the size asked for; any other size changes the display mode.
*/
qboolean R_SetMode (void)
{
	cvar_t			*vid_fullscreen, *gl_mode, *vid_xpos, *vid_ypos;
	int				width, height;
	int				x, y;
	qboolean		fullscreen;
	Uint32			flags;
	SDL_DisplayMode	desktop;
	SDL_Window		*window;

	vid_fullscreen = ri.Cvar_Get ("vid_fullscreen", "0", CVAR_ARCHIVE);
	gl_mode = ri.Cvar_Get ("gl_mode", "3", CVAR_ARCHIVE);

	fullscreen = vid_fullscreen->value != 0;
	gl_mode->modified = false;
	vid_fullscreen->modified = false;

	if (!SDL_WasInit (SDL_INIT_VIDEO) && SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL video: %s\n", SDL_GetError ());
		return false;
	}

	ri.Con_Printf (PRINT_ALL, "...setting mode %d:", (int)gl_mode->value);
	if (!ri.Vid_GetModeInfo (&width, &height, gl_mode->value))
	{
		ri.Con_Printf (PRINT_ALL, " invalid mode\n");
		return false;
	}
	ri.Con_Printf (PRINT_ALL, " %d %d %s\n", width, height, fullscreen ? "FS" : "W");

	flags = SDL_WINDOW_ALLOW_HIGHDPI;
	x = y = SDL_WINDOWPOS_CENTERED;
	if (fullscreen)
	{
		if (SDL_GetDesktopDisplayMode (0, &desktop) == 0 && desktop.w == width && desktop.h == height)
			flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
		else
			flags |= SDL_WINDOW_FULLSCREEN;
	}
	else
	{
		vid_xpos = ri.Cvar_Get ("vid_xpos", "0", 0);
		vid_ypos = ri.Cvar_Get ("vid_ypos", "0", 0);
		if (vid_xpos->value > 0 || vid_ypos->value > 0)
		{
			x = vid_xpos->value;
			y = vid_ypos->value;
		}
	}

	window = SDL_CreateWindow ("Quake 2", x, y, width, height, flags);
	if (!window && fullscreen)
	{
		ri.Con_Printf (PRINT_ALL, "...full screen failed, using a window: %s\n", SDL_GetError ());
		ri.Cvar_SetValue ("vid_fullscreen", 0);
		vid_fullscreen->modified = false;
		fullscreen = false;
		window = SDL_CreateWindow ("Quake 2", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, 0);
	}
	if (!window)
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't create window: %s\n", SDL_GetError ());
		return false;
	}
	SDL_RaiseWindow (window);
	SDL_PumpEvents ();

	// the picture is made the size the window turned out
	SDL_GetWindowSize (window, &width, &height);

	rpt.window = window;
	if (rpt.window_slot)
		*rpt.window_slot = window;
	rpt.width = width;
	rpt.height = height;
	rpt.fullscreen = fullscreen;
	return true;
}

/*
** R_WindowHandles
**
** What a backend presents into
*/
void R_WindowHandles (pt_create_t *ci)
{
	SDL_SysWMinfo	info;

	SDL_VERSION (&info.version);
	if (rpt.window && SDL_GetWindowWMInfo ((SDL_Window *)rpt.window, &info) && info.subsystem == SDL_SYSWM_X11)
	{
		ci->hinstance = info.info.x11.display;
		ci->hwnd = (void *)(uintptr_t)info.info.x11.window;
	}
	else
		ri.Con_Printf (PRINT_ALL, "The window is not an X11 one: set SDL_VIDEODRIVER=x11\n");
}

/*
** R_WindowActivate
*/
void R_WindowActivate (qboolean active)
{
	if (!rpt.window)
		return;
	if (active)
		SDL_RaiseWindow ((SDL_Window *)rpt.window);
	else if (rpt.fullscreen)
		SDL_MinimizeWindow ((SDL_Window *)rpt.window);
}
