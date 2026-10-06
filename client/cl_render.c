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
// cl_render.c -- rendering a recorded demo offline, as pictures for a film
//
//	pt_render <demo> [frames a second] [paths a pixel] [start] [length]
//
// plays the demo with the game's clock stepped by exactly one film frame
// per frame drawn, however long the drawing takes. The path traced renderer
// is told (pt_offline) to spend that many paths on every pixel of every
// frame and to save each as a PNG; the sound that belongs to each frame is
// mixed into a WAV beside them. With a start and a length, in seconds, only
// that part of the demo is rendered: what comes before is played through
// at the same step but not kept. What comes out, in <game>/render/<demo>/:
//
//	frame00000.png ...	the pictures
//	sound.wav			the sound
//	render.bat			makes a video of the two with ffmpeg
//
//	pt_bench [demo] [seconds] [quit]
//
// plays a demo the same way, a sixtieth of a second to each frame drawn, but
// drawn as the game is when played and as fast as the renderer goes, to find
// out how fast that is. Every machine draws the same frames, so what they
// take can be compared. The renderer does the timing while pt_bench_run is set
// and says what it found when that is cleared (ref_pt/rpt_bench.c). The
// first second of the demo is played before the timing starts, for the
// picture to settle; demo1 and twenty seconds unless told otherwise, 0 for
// all of it.

#include "client.h"

#define	RENDER_IDLE		0
#define	RENDER_WAITING	1		// for the demo to load
#define	RENDER_RUNNING	2

static int		render_state;
static char		render_name[MAX_QPATH];
static char		render_dir[MAX_OSPATH];
static int		render_fps, render_paths;
static int		render_frame;			// frames of the demo begun so far
static int		render_first;			// the first one to keep
static int		render_count;			// how many to keep; 0 = to the end
static qboolean	render_keeping;			// frames are being rendered and saved
static int		render_kept_since;		// when that began, for the time left
static int		render_expected;		// roughly how many there will be
static int		render_started;			// when, by the clock on the wall
static qboolean	render_loading;			// the demo has begun to load
static qboolean	render_sound;
static float	render_fixedtime;		// what fixedtime was before
static qboolean	render_bench;			// the frames are timed, not kept
static qboolean	render_thenquit;		// leave the game when the timing is done
static int		render_quitframes;		// how many frames from now

qboolean CL_RenderBusy (void)
{
	return render_state != RENDER_IDLE;
}

/*
===============
CL_RenderEnd
===============
*/
static void CL_RenderEnd (qboolean complete)
{
	int		seconds;

	Cvar_SetValue ("fixedtime", render_fixedtime);
	Cvar_Set ("pt_offline", "0");
	Cvar_Set ("pt_bench_run", "0");		// at which the renderer says what it found
	S_CaptureStop ();
	VID_SetTitle (NULL);

	if (render_bench)
	{
		if (render_state != RENDER_RUNNING)
			Com_Printf ("Could not play %s to time it.\n", render_name);
		else if (!render_keeping)
			Com_Printf ("%s was over before the timing began, a second into it.\n", render_name);
		if (render_state == RENDER_RUNNING && complete && render_thenquit)
			render_quitframes = 3;		// after the renderer has had its say
	}
	else if (render_state == RENDER_RUNNING)
	{
		seconds = (Sys_Milliseconds () - render_started) / 1000;
		Com_Printf ("\n%s %s: %i frames (%.1f seconds at %i a second) in %i:%02i:%02i\n",
			complete ? "Rendered" : "Stopped rendering", render_name,
			render_frame > render_first ? render_frame - render_first : 0,
			(float)(render_frame > render_first ? render_frame - render_first : 0) / render_fps, render_fps,
			seconds / 3600, seconds / 60 % 60, seconds % 60);
		Com_Printf ("They are in %s\n", render_dir);
		Com_Printf ("Run render.bat there to make a video of them (needs ffmpeg).\n");
	}
	else
		Com_Printf ("Could not play %s to render it.\n", render_name);

	render_state = RENDER_IDLE;
}

/*
===============
CL_RenderStop

Before the demo is over: the escape key, or pt_render_stop
===============
*/
void CL_RenderStop (void)
{
	if (render_state == RENDER_IDLE)
		return;
	CL_RenderEnd (false);
	Cbuf_AddText ("disconnect\n");
}

/*
===============
CL_RenderFrame

Called at the start of every client frame
===============
*/
void CL_RenderFrame (void)
{
	char	title[128];
	char	*doing;
	int		msec, left;

	if (render_quitframes && !--render_quitframes)
		Cbuf_AddText ("quit\n");

	if (render_state == RENDER_WAITING)
	{
		if (cls.state != ca_active)
			render_loading = true;
		else if (render_loading && cl.refresh_prepped)
		{
			// it is playing
			M_ForceMenuOff ();
			cls.key_dest = key_game;
			Con_ClearNotify ();
			render_frame = 0;
			render_keeping = false;
			render_started = Sys_Milliseconds ();
			render_state = RENDER_RUNNING;
		}

		if (render_state == RENDER_WAITING && Sys_Milliseconds () - render_started > 60000)
		{	// it never came up
			CL_RenderEnd (false);
			return;
		}
	}

	if (render_state != RENDER_RUNNING)
		return;

	if (cls.state != ca_active)
	{	// the demo is over
		CL_RenderEnd (true);
		return;
	}

	if (render_count && render_frame >= render_first + render_count)
	{	// that was the part asked for
		CL_RenderEnd (true);
		Cbuf_AddText ("disconnect\n");
		return;
	}
	if (!render_keeping && render_frame >= render_first)
	{
		if (render_bench)
		{	// from here on the frames are timed
			Cvar_Set ("pt_bench_demo", render_name);
			Cvar_Set ("pt_bench_run", "1");
		}
		else
		{	// from here on the frames are made properly and saved
			Cvar_Set ("pt_offline_dir", render_dir);
			Cvar_SetValue ("pt_offline", render_paths);
			render_sound = S_CaptureStart (va("%s/sound.wav", render_dir));
		}
		render_keeping = true;
	}

	// The game's clock counts whole milliseconds, which do not divide into
	// most frame rates: each frame is given however many take the total to
	// where it should be by then, so that the error never adds up.
	msec = (int)((double)(render_frame + 1) * 1000 / render_fps) - (int)((double)render_frame * 1000 / render_fps);
	if (msec < 1)
		msec = 1;
	Cvar_SetValue ("fixedtime", msec);
	if (render_keeping)
		S_CaptureStep (render_frame - render_first, render_fps);
	render_frame++;

	// progress goes in the title bar, where it is not in the picture; now
	// and then is enough while frames are timed, so as not to add to them
	if (render_bench && render_frame % 30)
		return;
	doing = render_bench ? "Timing" : "Rendering";
	if (!render_keeping)
		Com_sprintf (title, sizeof(title), "%s %s: playing up to the start, %i of %i - Esc stops",
			doing, render_name, render_frame, render_first);
	else if (render_frame - render_first > 4 && render_expected > render_frame)
	{
		// only the frames kept take any time to speak of
		if (!render_kept_since)
			render_kept_since = Sys_Milliseconds ();
		left = (int)((double)(Sys_Milliseconds () - render_kept_since) / 1000 * (render_expected - render_frame)
			/ (render_frame - render_first));
		Com_sprintf (title, sizeof(title), "%s %s: frame %i of about %i, about %i:%02i:%02i left - Esc stops",
			doing, render_name, render_frame - render_first, render_expected - render_first, left / 3600, left / 60 % 60, left % 60);
	}
	else
		Com_sprintf (title, sizeof(title), "%s %s: frame %i - Esc stops", doing, render_name, render_frame - render_first);
	VID_SetTitle (title);
}

/*
===============
CL_RenderBatch

A script beside the frames that makes a video of them
===============
*/
static void CL_RenderBatch (void)
{
	FILE	*f;

	f = fopen (va("%s/render.bat", render_dir), "w");
	if (!f)
		return;

	fprintf (f, "@echo off\n");
	fprintf (f, "rem Makes %s.mp4 from the frames and sound rendered into this folder.\n", render_name);
	fprintf (f, "rem Needs ffmpeg (https://ffmpeg.org) on the PATH.\n");
	fprintf (f, "cd /d \"%%~dp0\"\n");
	fprintf (f, "where ffmpeg >nul 2>nul\n");
	fprintf (f, "if errorlevel 1 goto noffmpeg\n");
	fprintf (f, "set VIDEO=-c:v libx264 -crf 16 -preset slow -pix_fmt yuv420p -vf \"pad=ceil(iw/2)*2:ceil(ih/2)*2\"\n");
	fprintf (f, "if not exist sound.wav goto silent\n");
	fprintf (f, "ffmpeg -y -framerate %i -i frame%%%%05d.png -i sound.wav %%VIDEO%% -c:a aac -b:a 192k -shortest \"..\\%s.mp4\"\n",
		render_fps, render_name);
	fprintf (f, "goto done\n");
	fprintf (f, ":silent\n");
	fprintf (f, "ffmpeg -y -framerate %i -i frame%%%%05d.png %%VIDEO%% \"..\\%s.mp4\"\n", render_fps, render_name);
	fprintf (f, "goto done\n");
	fprintf (f, ":noffmpeg\n");
	fprintf (f, "echo ffmpeg was not found. Install it from https://ffmpeg.org and put it on the PATH.\n");
	fprintf (f, ":done\n");
	fprintf (f, "pause\n");
	fclose (f);
}

/*
===============
CL_RenderBegin

To make a film of a demo or, with bench, to time the frames of it as they are
drawn in play. Returns false, having said why, if it cannot begin.
===============
*/
static qboolean CL_RenderBegin (char *demo, int fps, int paths, float start, float duration, qboolean bench)
{
	char	name[MAX_OSPATH];
	char	*s;
	FILE	*f;
	int		length, blocks, size, at;

	if (render_state != RENDER_IDLE)
	{
		Com_Printf ("Already %s %s. pt_render_stop or Esc stops it.\n", render_bench ? "timing" : "rendering", render_name);
		return false;
	}
	if (Q_strncasecmp (Cvar_VariableString ("vid_ref"), "pt", 2))
	{
		Com_Printf ("Demos are %s the path traced renderers: pick one in the video menu first.\n",
			bench ? "timed with" : "rendered by");
		return false;
	}

	// the name without folder or extension
	s = strrchr (demo, '/');
	if (strrchr (demo, '\\') > s)
		s = strrchr (demo, '\\');
	Com_sprintf (name, sizeof(name), "%s", s ? s + 1 : demo);
	s = strrchr (name, '.');
	if (s)
		*s = 0;
	if (!name[0] || strlen (name) >= sizeof(render_name))
	{
		Com_Printf ("Bad demo name\n");
		return false;
	}
	strcpy (render_name, name);

	// how long it is: a demo is the messages the server sent, ten a second
	length = FS_FOpenFile (va("demos/%s.dm2", render_name), &f);
	if (!f)
	{
		Com_Printf ("There is no demo demos/%s.dm2\n", render_name);
		return false;
	}
	blocks = 0;
	for (at=0 ; at+4<=length ; at+=4+size)
	{
		FS_Read (&size, 4, f);
		size = LittleLong (size);
		if (size <= 0 || at + 4 + size > length)
			break;
		fseek (f, size, SEEK_CUR);
		blocks++;
	}
	FS_FCloseFile (f);

	render_fps = fps < 1 ? 1 : (fps > 240 ? 240 : fps);
	render_paths = paths < 4 ? 4 : (paths > 4096 ? 4096 : paths);
	render_expected = blocks * render_fps / 10;
	render_first = start > 0 ? (int)(start * render_fps) : 0;
	render_count = duration > 0 ? (int)(duration * render_fps + 0.5f) : 0;
	if (render_first >= render_expected)
	{
		Com_Printf ("%s is only about %.1f seconds long\n", render_name, blocks / 10.0f);
		return false;
	}
	if (render_count && render_first + render_count < render_expected)
		render_expected = render_first + render_count;
	render_keeping = false;
	render_kept_since = 0;
	render_bench = bench;
	render_thenquit = false;
	render_quitframes = 0;

	if (bench)
		Com_Printf ("Timing about %i frames of %s\n", render_expected - render_first, render_name);
	else
	{
		Com_sprintf (render_dir, sizeof(render_dir), "%s/render/%s", FS_Gamedir (), render_name);
		FS_CreatePath (va("%s/x", render_dir));

		// frames left from an earlier, longer render would end up in the video
		Com_sprintf (name, sizeof(name), "%s/frame*.png", render_dir);
		for (s = Sys_FindFirst (name, 0, 0) ; s ; s = Sys_FindNext (0, 0))
			remove (s);
		Sys_FindClose ();
		remove (va("%s/sound.wav", render_dir));

		CL_RenderBatch ();

		Com_Printf ("Rendering %s at %i frames a second, %i paths a pixel: about %i frames\n",
			render_name, render_fps, render_paths, render_expected - render_first);
	}

	render_fixedtime = Cvar_VariableValue ("fixedtime");
	render_loading = false;
	render_started = Sys_Milliseconds ();
	render_state = RENDER_WAITING;

	M_ForceMenuOff ();
	Cbuf_AddText (va("demomap %s.dm2\n", render_name));
	return true;
}

qboolean CL_RenderStart (char *demo, int fps, int paths, float start, float duration)
{
	return CL_RenderBegin (demo, fps, paths, start, duration, false);
}

static void CL_Bench_f (void)
{
	// sixty frames to a second of the demo, the first of them before the
	// timing starts
	if (CL_RenderBegin (Cmd_Argc () > 1 ? Cmd_Argv (1) : "demo1", 60, 0, 1,
		Cmd_Argc () > 2 ? atof (Cmd_Argv (2)) : 20, true))
		render_thenquit = Cmd_Argc () > 3 && !Q_stricmp (Cmd_Argv (3), "quit");
}

static void CL_Render_f (void)
{
	if (Cmd_Argc () < 2)
	{
		Com_Printf ("pt_render <demo> [frames a second] [paths a pixel] [start] [length]\n"
			"Renders a demo recorded with \"record\" as pictures and sound for a video.\n"
			"60 frames a second and 64 paths a pixel unless given. start and length,\n"
			"in seconds, pick a part of the demo; without them all of it is rendered.\n");
		return;
	}
	CL_RenderStart (Cmd_Argv (1), Cmd_Argc () > 2 ? atoi (Cmd_Argv (2)) : 60,
		Cmd_Argc () > 3 ? atoi (Cmd_Argv (3)) : 64,
		Cmd_Argc () > 4 ? atof (Cmd_Argv (4)) : 0, Cmd_Argc () > 5 ? atof (Cmd_Argv (5)) : 0);
}

void CL_InitRender (void)
{
	Cmd_AddCommand ("pt_render", CL_Render_f);
	Cmd_AddCommand ("pt_render_stop", CL_RenderStop);
	Cmd_AddCommand ("pt_bench", CL_Bench_f);
}
