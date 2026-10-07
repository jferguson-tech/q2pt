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
// rpt_bench.c -- how fast the renderer is
//
// While pt_bench_run is set (the client does that while it plays a demo for the
// pt_bench command, see client/cl_render.c) the time from each frame being
// shown to the next is kept, and what the backend says of where its share of
// it went. When it is cleared again, what that comes to is printed and added
// to pt_bench.txt in the game's folder: enough of the settings to tell one
// run from another, the frame rate, how the frame times are spread, and the
// average and the worst of each part of the work.

#include "rpt_local.h"
#include <time.h>

#define	MAX_BENCH_STAGES	12

typedef struct
{
	char	name[16];
	double	sum;
	float	worst;
	int		count;
} benchstage_t;

static cvar_t	*pt_bench_run;			// frames are being timed
static cvar_t	*pt_bench_demo;			// what is being played, for the report

static qboolean	bench_on;
static float	*bench_ms;				// what each frame took
static int		bench_num, bench_room;
static benchstage_t	bench_stages[MAX_BENCH_STAGES];
static int		bench_numstages;
static qboolean	bench_viewed;			// this frame has a view in it
static qboolean	bench_timing;			// and so had the one before, shown at bench_last
static double	bench_last;				// in milliseconds, by Bench_Now
static char		bench_demo[MAX_QPATH];
static char		bench_what[80];			// the backend's own words for the picture it makes
static FILE		*bench_file;

// the time in milliseconds, from a clock fine enough to time a frame by and
// that never goes back
static double Bench_Now (void)
{
#ifdef _WIN32
	LARGE_INTEGER	now, freq;

	QueryPerformanceCounter (&now);
	QueryPerformanceFrequency (&freq);
	return (double)now.QuadPart * 1000.0 / (double)freq.QuadPart;
#else
	struct timespec	now;

	clock_gettime (CLOCK_MONOTONIC, &now);
	return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec * 1.0e-6;
#endif
}

void R_InitBench (void)
{
	pt_bench_run = ri.Cvar_Get ("pt_bench_run", "0", 0);
	pt_bench_demo = ri.Cvar_Get ("pt_bench_demo", "", 0);
}

static void Bench_AddFrame (float ms)
{
	float	*bigger;

	if (bench_num == bench_room)
	{
		bigger = realloc (bench_ms, (bench_room + 4096) * sizeof(float));
		if (!bigger)
			return;
		bench_ms = bigger;
		bench_room += 4096;
	}
	bench_ms[bench_num++] = ms;
}

static void Bench_AddStage (const char *name, float ms)
{
	benchstage_t	*stage;
	int				i;

	for (i=0, stage=bench_stages ; i<bench_numstages ; i++, stage++)
		if (!strcmp (stage->name, name))
			break;
	if (i == bench_numstages)
	{
		if (bench_numstages == MAX_BENCH_STAGES)
			return;
		memset (stage, 0, sizeof(*stage));
		strncpy (stage->name, name, sizeof(stage->name) - 1);
		bench_numstages++;
	}

	stage->sum += ms;
	stage->count++;
	if (ms > stage->worst)
		stage->worst = ms;
}

// to the console and to the file
static void Bench_Printf (char *fmt, ...)
{
	va_list		argptr;
	char		text[256];

	va_start (argptr, fmt);
	vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);

	ri.Con_Printf (PRINT_ALL, "%s", text);
	if (bench_file)
		fputs (text, bench_file);
}

static int Bench_Compare (const void *a, const void *b)
{
	float	fa = *(const float *)a, fb = *(const float *)b;

	return fa < fb ? -1 : (fa > fb ? 1 : 0);
}

/*
===============
Bench_Report
===============
*/
static void Bench_Report (void)
{
	// what a run has to have in common with another to be compared with it
	static char	*settings[] = {
		"pt_quality", "pt_scale", "pt_samples", "pt_bounces", "pt_light_samples", "pt_reflections",
		"pt_denoise", "pt_taa", "pt_fog", "pt_water", "pt_bloom", NULL
	};
	benchstage_t	*stage;
	char		path[MAX_OSPATH], when[32];
	time_t		now;
	double		total, rest;
	float		*sorted;
	int			i;

	if (bench_num < 2)
	{
		ri.Con_Printf (PRINT_ALL, "pt_bench: too few frames were drawn to say anything\n");
		return;
	}
	sorted = malloc (bench_num * sizeof(float));
	if (!sorted)
		return;
	memcpy (sorted, bench_ms, bench_num * sizeof(float));
	qsort (sorted, bench_num, sizeof(float), Bench_Compare);
	total = 0;
	for (i=0 ; i<bench_num ; i++)
		total += bench_ms[i];

	Com_sprintf (path, sizeof(path), "%s/pt_bench.txt", ri.FS_Gamedir ());
	bench_file = fopen (path, "a");

	time (&now);
	strftime (when, sizeof(when), "%Y-%m-%d %H:%M", localtime (&now));

	Bench_Printf ("\n---- pt_bench %s, %s ----\n", bench_demo, when);
	Bench_Printf ("%s: %s\n", rpt.backend->name, rpt.backend->device ? rpt.backend->device : "?");
	Bench_Printf ("%dx%d %s, traced %s\n", rpt.width, rpt.height, rpt.fullscreen ? "full screen" : "window", bench_what);
	for (i=0 ; settings[i] ; i++)
		Bench_Printf ("%s %g%s", settings[i], ri.Cvar_Get (settings[i], "0", 0)->value,
			!settings[i + 1] || i % 6 == 5 ? "\n" : ", ");

	Bench_Printf ("%d frames in %.2f seconds: %.1f a second\n", bench_num, total / 1000, bench_num * 1000.0 / total);
	// of the frames ranked by how long they took: the one in the middle, and
	// the one that only a hundredth of them were slower than
	Bench_Printf ("frame ms: average %.2f, median %.2f, 1 in 100 over %.2f, worst %.2f\n",
		total / bench_num, sorted[bench_num / 2], sorted[(int)((bench_num - 1) * 0.99)], sorted[bench_num - 1]);

	if (bench_numstages)
	{
		Bench_Printf ("%-10s %9s %9s\n", "ms", "average", "worst");
		rest = total / bench_num;
		for (i=0, stage=bench_stages ; i<bench_numstages ; i++, stage++)
		{
			Bench_Printf ("%-10s %9.2f %9.2f\n", stage->name, stage->sum / stage->count, stage->worst);
			rest -= stage->sum / stage->count;
		}
		// What of a frame none of the parts account for: the game's own
		// work, the 2D drawing, and getting the picture to the screen. On a
		// graphics card it is time the card mostly spends waiting.
		Bench_Printf ("%-10s %9.2f\n", "the rest", rest);
	}

	if (bench_file)
	{
		fclose (bench_file);
		bench_file = NULL;
		ri.Con_Printf (PRINT_ALL, "Added to %s\n", path);
	}
	free (sorted);
}

/*
===============
R_BenchView

This frame has a view in it: it is one to count
===============
*/
void R_BenchView (void)
{
	bench_viewed = true;
}

/*
===============
R_BenchFrame

After the frame has been shown
===============
*/
void R_BenchFrame (void)
{
	double			now;
	pt_stage_t		stages[MAX_BENCH_STAGES];
	qboolean		on;
	char			*s;
	int				i, num;

	on = pt_bench_run->value != 0;
	if (on && !bench_on)
	{
		bench_num = 0;
		bench_numstages = 0;
		bench_timing = false;
		bench_what[0] = 0;
		strncpy (bench_demo, pt_bench_demo->string, sizeof(bench_demo) - 1);
		bench_demo[sizeof(bench_demo) - 1] = 0;
	}
	else if (!on && bench_on)
		Bench_Report ();
	bench_on = on;
	if (!on)
	{
		bench_viewed = false;
		return;
	}

	now = Bench_Now ();
	if (bench_viewed && bench_timing)
		Bench_AddFrame ((float)(now - bench_last));
	// a frame with no view in it (a map being loaded) is not one to count,
	// and nor is the wait for the next one after it
	bench_timing = bench_viewed;
	bench_last = now;

	if (bench_viewed)
	{
		// the backend's first words say what size the picture is traced at
		strncpy (bench_what, rpt.backend->stats (rpt.backend), sizeof(bench_what) - 1);
		bench_what[sizeof(bench_what) - 1] = 0;
		s = strpbrk (bench_what, "|:");
		if (s)
			*s = 0;

		num = rpt.backend->stages ? rpt.backend->stages (rpt.backend, stages, MAX_BENCH_STAGES) : 0;
		for (i=0 ; i<num ; i++)
			Bench_AddStage (stages[i].name, stages[i].ms);
	}
	bench_viewed = false;
}

/*
===============
R_ShutdownBench

The game is closing, or the renderer being changed, with a demo still being
timed: say what there is
===============
*/
void R_ShutdownBench (void)
{
	if (bench_on && rpt.backend)
		Bench_Report ();
	bench_on = false;

	free (bench_ms);
	bench_ms = NULL;
	bench_num = bench_room = 0;
}
