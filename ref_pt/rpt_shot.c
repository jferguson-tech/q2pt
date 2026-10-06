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
// rpt_shot.c -- screenshots
//
// "screenshot" saves the frame as it is on screen. "pt_screenshot [paths]"
// first renders the view again at full resolution with that many paths per
// pixel (64 unless given), and saves it without the status bar.

#include "rpt_local.h"

#define	SHOT_PLAIN		1
#define	SHOT_CLEAN		2

#define	PATHS_PER_PASS	4

static int	shot_pending;
static int	shot_paths;

static void R_ScreenShot_f (void)
{
	shot_pending = SHOT_PLAIN;
}

static void R_PathShot_f (void)
{
	shot_paths = ri.Cmd_Argc () > 1 ? atoi (ri.Cmd_Argv (1)) : 64;
	if (shot_paths < PATHS_PER_PASS)
		shot_paths = PATHS_PER_PASS;
	if (shot_paths > 4096)
		shot_paths = 4096;
	shot_pending = SHOT_CLEAN;
}

void R_InitShots (void)
{
	ri.Cmd_AddCommand ("screenshot", R_ScreenShot_f);
	ri.Cmd_AddCommand ("pt_screenshot", R_PathShot_f);
}

void R_ShutdownShots (void)
{
	ri.Cmd_RemoveCommand ("screenshot");
	ri.Cmd_RemoveCommand ("pt_screenshot");
	shot_pending = 0;
}

/*
===============
R_ShotPasses

How many times to render this view. For a clean shot it is rendered over
and over from the same place, each pass adding to the last, at settings
that would be far too slow to play with.
===============
*/
int R_ShotPasses (pt_view_t *view)
{
	if (shot_pending != SHOT_CLEAN)
		return 1;

	view->scale = 1;
	view->samples = PATHS_PER_PASS;
	view->antialias = 1;
	view->filter = 2;
	view->debug = 0;
	// one more than the paths need: the first starts afresh at the new size
	return (shot_paths + PATHS_PER_PASS - 1) / PATHS_PER_PASS + 1;
}

/*
===============
R_ShotFinish

After the frame has been shown: save it if one was asked for
===============
*/
void R_ShotFinish (void)
{
	uint32_t	*pixels;
	byte		*buffer;
	char		picname[80];
	char		checkname[MAX_OSPATH];
	int			i, x, y, size;
	FILE		*f;

	if (!shot_pending)
		return;

	size = rpt.width * rpt.height;
	pixels = malloc (size * sizeof(uint32_t));
	buffer = malloc (size * 3 + 18);
	if (!pixels || !buffer || !rpt.backend->read_pixels (rpt.backend, pixels, shot_pending == SHOT_PLAIN))
	{
		ri.Con_Printf (PRINT_ALL, "Couldn't read the frame back for a screenshot\n");
		free (pixels);
		free (buffer);
		shot_pending = 0;
		return;
	}
	shot_pending = 0;

	// create the scrnshots directory if it doesn't exist
	Com_sprintf (checkname, sizeof(checkname), "%s/scrnshot", ri.FS_Gamedir ());
	Sys_Mkdir (checkname);

	// find a file name to save it to
	strcpy (picname, "quake00.tga");
	for (i=0 ; i<=99 ; i++)
	{
		picname[5] = i/10 + '0';
		picname[6] = i%10 + '0';
		Com_sprintf (checkname, sizeof(checkname), "%s/scrnshot/%s", ri.FS_Gamedir (), picname);
		f = fopen (checkname, "rb");
		if (!f)
			break;	// file doesn't exist
		fclose (f);
	}
	if (i == 100)
	{
		ri.Con_Printf (PRINT_ALL, "R_ShotFinish: Couldn't create a file\n");
		free (pixels);
		free (buffer);
		return;
	}

	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = rpt.width & 255;
	buffer[13] = rpt.width >> 8;
	buffer[14] = rpt.height & 255;
	buffer[15] = rpt.height >> 8;
	buffer[16] = 24;	// pixel size

	// the file wants blue first and the bottom row first
	for (y=0 ; y<rpt.height ; y++)
	{
		const uint32_t	*in = pixels + (rpt.height - 1 - y) * rpt.width;
		byte			*out = buffer + 18 + y * rpt.width * 3;

		for (x=0 ; x<rpt.width ; x++, out+=3)
		{
			out[0] = (in[x] >> 16) & 255;
			out[1] = (in[x] >> 8) & 255;
			out[2] = in[x] & 255;
		}
	}

	f = fopen (checkname, "wb");
	if (f)
	{
		fwrite (buffer, 1, size * 3 + 18, f);
		fclose (f);
		ri.Con_Printf (PRINT_ALL, "Wrote %s\n", picname);
	}
	else
		ri.Con_Printf (PRINT_ALL, "R_ShotFinish: Couldn't write %s\n", checkname);

	free (pixels);
	free (buffer);
}
