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
/*
** GL_SDL.C
**
** The OpenGL window and context where there is no Win32, made with SDL. The
** engine reads the keyboard and mouse from SDL itself (linux/vid_sdl.c).
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_Shutdown
** GLimp_SetMode
*/
#include <SDL.h>

#include "../ref_gl/gl_local.h"

static SDL_Window		*gl_window;
static SDL_GLContext	gl_context;
static void				**gl_window_slot;	// the engine's own note of the window

static void GLimp_DestroyWindow (void)
{
	if (gl_context)
	{
		SDL_GL_DeleteContext (gl_context);
		gl_context = NULL;
	}
	if (gl_window)
	{
		SDL_DestroyWindow (gl_window);
		gl_window = NULL;
		if (gl_window_slot)
			*gl_window_slot = NULL;
	}
}

/*
** GLimp_SetMode
**
** Full screen is a borderless window the size of the desktop when that is
** the size asked for; any other size changes the display mode.
*/
int GLimp_SetMode( int *pwidth, int *pheight, int mode, qboolean fullscreen )
{
	int				width, height;
	int				x, y;
	Uint32			flags;
	SDL_DisplayMode	desktop;
	cvar_t			*vid_xpos, *vid_ypos;

	ri.Con_Printf( PRINT_ALL, "Initializing OpenGL display\n");
	ri.Con_Printf (PRINT_ALL, "...setting mode %d:", mode );

	if ( !ri.Vid_GetModeInfo( &width, &height, mode ) )
	{
		ri.Con_Printf( PRINT_ALL, " invalid mode\n" );
		return rserr_invalid_mode;
	}
	ri.Con_Printf( PRINT_ALL, " %d %d %s\n", width, height, fullscreen ? "FS" : "W" );

	// destroy the existing window
	GLimp_DestroyWindow ();

	SDL_GL_SetAttribute (SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute (SDL_GL_DOUBLEBUFFER, 1);

	flags = SDL_WINDOW_OPENGL;
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

	gl_window = SDL_CreateWindow ("Quake 2", x, y, width, height, flags);
	if (!gl_window)
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't create window: %s\n", SDL_GetError ());
		return fullscreen ? rserr_invalid_fullscreen : rserr_invalid_mode;
	}

	gl_context = SDL_GL_CreateContext (gl_window);
	if (!gl_context)
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't create an OpenGL context: %s\n", SDL_GetError ());
		GLimp_DestroyWindow ();
		return rserr_unknown;
	}
	SDL_GL_MakeCurrent (gl_window, gl_context);
	gl_swapinterval->modified = true;

	SDL_RaiseWindow (gl_window);
	SDL_PumpEvents ();

	// the picture is made the size the window turned out
	SDL_GL_GetDrawableSize (gl_window, &width, &height);

	if (gl_window_slot)
		*gl_window_slot = gl_window;
	*pwidth = width;
	*pheight = height;
	gl_state.fullscreen = fullscreen;

	// let the sound and input subsystems know about the new window
	ri.Vid_NewWindow (width, height);

	return rserr_ok;
}

/*
** GLimp_Shutdown
*/
void GLimp_Shutdown( void )
{
	GLimp_DestroyWindow ();
}

/*
** GLimp_Init
**
** The engine passes, where Windows has its HINSTANCE, the place it wants
** the window noted.
*/
int GLimp_Init( void *hinstance, void *wndproc )
{
	gl_window_slot = (void **)hinstance;
	gl_config.allow_cds = true;		// full screen may be switched while running

	if (!SDL_WasInit (SDL_INIT_VIDEO) && SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
	{
		ri.Con_Printf (PRINT_ALL, "SDL video: %s\n", SDL_GetError ());
		return false;
	}
	return true;
}

/*
** GLimp_BeginFrame
*/
void GLimp_BeginFrame( float camera_separation )
{
	if ( gl_swapinterval->modified )
	{
		gl_swapinterval->modified = false;
		SDL_GL_SetSwapInterval (gl_swapinterval->value ? 1 : 0);
	}

	if ( camera_separation < 0 && gl_state.stereo_enabled )
		qglDrawBuffer( GL_BACK_LEFT );
	else if ( camera_separation > 0 && gl_state.stereo_enabled )
		qglDrawBuffer( GL_BACK_RIGHT );
	else
		qglDrawBuffer( GL_BACK );
}

/*
** GLimp_EndFrame
**
** Responsible for doing a swapbuffers and possibly for other stuff
** as yet to be determined.
*/
void GLimp_EndFrame (void)
{
	if (gl_window)
		SDL_GL_SwapWindow (gl_window);
}

/*
** GLimp_AppActivate
*/
void GLimp_AppActivate( qboolean active )
{
	if (!gl_window)
		return;
	if (active)
		SDL_RaiseWindow (gl_window);
	else if (gl_state.fullscreen)
		SDL_MinimizeWindow (gl_window);
}
