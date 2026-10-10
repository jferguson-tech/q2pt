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
// rpt_perf.c -- pt_perf: where the time of every frame goes, drawn as it is played
//
// Only in a build made with PT_PERF; in any other this file is empty and
// the timers it reads are not in the program.
//
// A frame runs from one being shown to the next. Its time is told apart in
// two ways that overlap, so they are drawn as two graphs:
//
//   the processor: PERF (name) puts the time since the last such call on a
//   part of that name, the backend adds the parts of its own work (see perf
//   in pt.h), and the program adds what the game's server and the sound took
//   (see PERF_TIMED in qcommon.h). What is left over is "untimed".
//
//   the card: each pass of the latest view whose times it has given, which
//   is the one before the frame they are drawn beside.

#include "rpt_local.h"

#ifdef PT_PERF

#define	MAX_PARTS		40		// of either kind
#define	PERF_FRAMES		240		// as many are kept and drawn
#define	PERF_COLUMN		2		// pixels across for each
#define	PERF_HEIGHT		120
#define	PERF_ROWS		(PERF_HEIGHT / 10)
#define	PERF_BAR		120		// the longest bar beside the graph

typedef struct
{
	const char	*name;
	int			where;			// as in pt_stage_t
	float		now;			// this frame, so far
	float		smooth;
	float		worst;			// in the last second
} perfpart_t;

typedef struct
{
	perfpart_t	parts[MAX_PARTS];
	int			num;
	float		kept[PERF_FRAMES][MAX_PARTS];
} perfside_t;

static perfside_t	perf_sides[2];			// the processor, the card
static float		perf_total[PERF_FRAMES];	// each frame, shown to shown
static double		perf_when[PERF_FRAMES];
static int			perf_head, perf_count;		// the next to be written, and how many are
static float		perf_smooth_total;
static double		perf_mark_at, perf_frame_at;
static qboolean		perf_frozen;
static cvar_t		*pt_perf;				// 0 nothing, 1 the graphs, 2 and what each part comes to
static cvar_t		*pt_perf_spike;			// frames longer than this many ms are written down, 0 = none
static cvar_t		*pt_perf_server, *pt_perf_sound;	// summed by the program, see qcommon.h

// one for each part, in the order they are first met
static const byte perf_colors[16][3] = {
	{ 230, 80, 70 }, { 80, 170, 240 }, { 110, 210, 110 }, { 240, 200, 70 },
	{ 190, 120, 240 }, { 70, 220, 210 }, { 240, 140, 60 }, { 240, 130, 190 },
	{ 150, 150, 150 }, { 160, 220, 60 }, { 100, 120, 240 }, { 200, 170, 120 },
	{ 250, 250, 250 }, { 130, 90, 60 }, { 60, 140, 110 }, { 200, 60, 150 },
};

static int Perf_Part (int side, const char *name, int where)
{
	perfside_t	*s = &perf_sides[side];
	int			i;

	for (i=0 ; i<s->num ; i++)
		if (s->parts[i].name == name || !strcmp (s->parts[i].name, name))
			return i;
	if (s->num == MAX_PARTS)
		return MAX_PARTS - 1;
	memset (&s->parts[s->num], 0, sizeof(s->parts[0]));
	s->parts[s->num].name = name;
	s->parts[s->num].where = where;
	return s->num++;
}

/*
===============
R_PerfMark
===============
*/
void R_PerfMark (const char *name)
{
	double	now = Sys_PerfMs ();

	if (name)
		perf_sides[0].parts[Perf_Part (0, name, 0)].now += (float)(now - perf_mark_at);
	perf_mark_at = now;
}

/*
===============
Perf_Spike

A frame that took too long: what it was made of, in the console and added
to pt_perf.txt in the game's folder
===============
*/
static void Perf_Spike (float total)
{
	char	text[1024], path[MAX_OSPATH];
	FILE	*f;
	int		side, i, len;

	Com_sprintf (text, sizeof(text), "pt_perf: a frame of %.2f ms:", total);
	len = strlen (text);
	for (side=0 ; side<2 ; side++)
	{
		if (side && len < (int)sizeof(text) - 16)
		{
			strcpy (text + len, " | card:");
			len += 8;
		}
		for (i=0 ; i<perf_sides[side].num ; i++)
		{
			const perfpart_t	*p = &perf_sides[side].parts[i];

			if (p->now < 0.05f || len > (int)sizeof(text) - 40)
				continue;
			Com_sprintf (text + len, sizeof(text) - len, " %s %.2f", p->name, p->now);
			len += strlen (text + len);
		}
	}
	ri.Con_Printf (PRINT_ALL, "%s\n", text);

	Com_sprintf (path, sizeof(path), "%s/pt_perf.txt", ri.FS_Gamedir ());
	f = fopen (path, "a");
	if (f)
	{
		fprintf (f, "%s\n", text);
		fclose (f);
	}
}

/*
===============
R_PerfFrame

A frame has just been shown: what it came to is put by
===============
*/
void R_PerfFrame (void)
{
	pt_stage_t	stages[2 * MAX_PARTS];
	perfside_t	*s;
	perfpart_t	*p;
	double		now;
	float		total, sum, server, sound, game, *kept;
	int			side, i, num, part;

	now = Sys_PerfMs ();
	total = (float)(now - perf_frame_at);
	perf_frame_at = perf_mark_at = now;

	num = rpt.backend->perf ? rpt.backend->perf (rpt.backend, stages, 2 * MAX_PARTS) : 0;
	for (i=0 ; i<num ; i++)
	{
		side = stages[i].where == 1;
		perf_sides[side].parts[Perf_Part (side, stages[i].name, stages[i].where)].now += stages[i].ms;
	}

	// what the program has to say of the time before the view was begun
	server = pt_perf_server->value;
	sound = pt_perf_sound->value;
	pt_perf_server->value = pt_perf_sound->value = 0;
	part = Perf_Part (0, "game", 0);
	game = perf_sides[0].parts[part].now;
	perf_sides[0].parts[part].now = 0;
	if (server > game)
		server = game;
	if (sound > game - server)
		sound = game - server;
	perf_sides[0].parts[Perf_Part (0, "server", 0)].now = server;
	perf_sides[0].parts[Perf_Part (0, "sound", 0)].now = sound;
	perf_sides[0].parts[Perf_Part (0, "client", 0)].now = game - server - sound;

	sum = 0;
	for (i=0 ; i<perf_sides[0].num ; i++)
		sum += perf_sides[0].parts[i].now;
	part = Perf_Part (0, "untimed", 0);
	perf_sides[0].parts[part].now = total > sum ? total - sum : 0;

	// not the first frame, nor one after a pause or a map being loaded
	if (total > 0 && total < 2000 && !perf_frozen)
	{
		if (pt_perf_spike->value > 0 && total > pt_perf_spike->value)
			Perf_Spike (total);

		perf_total[perf_head] = total;
		perf_when[perf_head] = now;
		perf_smooth_total += (total - perf_smooth_total) * 0.1f;
		for (side=0 ; side<2 ; side++)
		{
			s = &perf_sides[side];
			kept = s->kept[perf_head];
			for (i=0, p=s->parts ; i<s->num ; i++, p++)
			{
				kept[i] = p->now;
				p->smooth += (p->now - p->smooth) * 0.1f;
			}
			for ( ; i<MAX_PARTS ; i++)
				kept[i] = 0;
		}
		perf_head = (perf_head + 1) % PERF_FRAMES;
		if (perf_count < PERF_FRAMES)
			perf_count++;
	}

	for (side=0 ; side<2 ; side++)
		for (i=0 ; i<perf_sides[side].num ; i++)
			perf_sides[side].parts[i].now = 0;
}

// ---------------------------------------------------------------- drawing

static uint32_t Perf_Color (int part, float shade)
{
	const byte	*c = perf_colors[part & 15];

	return (uint32_t)(c[0] * shade) | ((uint32_t)(c[1] * shade) << 8) | ((uint32_t)(c[2] * shade) << 16) | 0xff000000;
}

// the caller has told Draw_Touch of it
static void Perf_Fill (int x, int y, int w, int h, uint32_t color)
{
	uint32_t	*dest;
	int			x1, y1, dx;

	x1 = x + w > rpt.width ? rpt.width : x + w;
	y1 = y + h > rpt.height ? rpt.height : y + h;
	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	for ( ; y<y1 ; y++)
	{
		dest = rpt.overlay + y * rpt.width;
		for (dx=x ; dx<x1 ; dx++)
			dest[dx] = color;
	}
}

/*
===============
Perf_Graph

The frames kept, the oldest on the left, each a column of its parts one on
another. top is the time the full height stands for.
===============
*/
static void Perf_Graph (int side, int x, int y, float top)
{
	static const int	rates[] = { 30, 60, 120, 240, 480 };
	const perfside_t	*s = &perf_sides[side];
	const float			scale = PERF_HEIGHT / top;
	const float			*kept;
	char				text[16];
	float				sum;
	int					i, k, frame, from, to, line;

	Draw_FadeBox (x, y, PERF_FRAMES * PERF_COLUMN, PERF_HEIGHT);
	for (k=0 ; k<perf_count ; k++)
	{
		frame = (perf_head - perf_count + k + PERF_FRAMES) % PERF_FRAMES;
		kept = s->kept[frame];
		sum = 0;
		from = 0;
		for (i=0 ; i<s->num && from<PERF_HEIGHT ; i++)
		{
			if (kept[i] <= 0)
				continue;
			sum += kept[i];
			to = (int)(sum * scale + 0.5f);
			if (to > PERF_HEIGHT)
				to = PERF_HEIGHT;
			if (to > from)
				Perf_Fill (x + k * PERF_COLUMN, y + PERF_HEIGHT - to, PERF_COLUMN, to - from, Perf_Color (i, 1));
			from = to;
		}
		if (sum * scale > PERF_HEIGHT)
			Perf_Fill (x + k * PERF_COLUMN, y, PERF_COLUMN, 2, 0xffffffff);		// there was more of it
		if (side)
		{	// and how long the whole frame was, to see what the card had to spare
			to = (int)(perf_total[frame] * scale + 0.5f);
			if (to > 0 && to <= PERF_HEIGHT)
				Perf_Fill (x + k * PERF_COLUMN, y + PERF_HEIGHT - to, PERF_COLUMN, 1, 0xffffffff);
		}
	}

	// the frame times of round frame rates
	for (i=0 ; i<5 ; i++)
	{
		line = (int)(1000.0f / rates[i] * scale + 0.5f);
		if (line < 12 || line > PERF_HEIGHT)
			continue;
		Perf_Fill (x, y + PERF_HEIGHT - line, PERF_FRAMES * PERF_COLUMN, 1, 0xff909090);
		Com_sprintf (text, sizeof(text), "%d", rates[i]);
		Draw_String (x + PERF_FRAMES * PERF_COLUMN - 8 * (int)strlen (text) - 2, y + PERF_HEIGHT - line + 2, text);
	}
}

/*
===============
Perf_Bars

What each part comes to, the longest first: smoothed over a few frames, and
the worst of the last second
===============
*/
static void Perf_Bars (int side, int x, int y, float top)
{
	perfside_t	*s = &perf_sides[side];
	char		text[64];
	int			order[MAX_PARTS];
	int			i, j, k, num, frame, width;
	double		since;

	since = perf_frame_at - 1000.0;
	for (i=0 ; i<s->num ; i++)
		s->parts[i].worst = 0;
	for (k=0 ; k<perf_count ; k++)
	{
		frame = (perf_head - 1 - k + PERF_FRAMES) % PERF_FRAMES;
		if (perf_when[frame] < since && !perf_frozen)
			break;
		for (i=0 ; i<s->num ; i++)
			if (s->kept[frame][i] > s->parts[i].worst)
				s->parts[i].worst = s->kept[frame][i];
	}

	num = 0;
	for (i=0 ; i<s->num ; i++)
	{
		if (s->parts[i].smooth < 0.005f && s->parts[i].worst < 0.05f)
			continue;
		for (j=num++ ; j>0 && s->parts[order[j-1]].smooth < s->parts[i].smooth ; j--)
			order[j] = order[j-1];
		order[j] = i;
	}
	if (num > PERF_ROWS)
		num = PERF_ROWS;

	Draw_FadeBox (x, y, 12 + 27 * 8 + 8 + PERF_BAR, PERF_HEIGHT);
	for (j=0 ; j<num ; j++)
	{
		const perfpart_t	*p = &s->parts[order[j]];

		Perf_Fill (x + 2, y + j * 10 + 1, 8, 8, Perf_Color (order[j], 1));
		Com_sprintf (text, sizeof(text), "%-14.14s%6.2f%7.2f", p->name, p->smooth, p->worst);
		Draw_String (x + 12, y + j * 10 + 1, text);
		width = (int)(p->smooth / top * PERF_BAR + 0.5f);
		if (width > PERF_BAR)
			width = PERF_BAR;
		Perf_Fill (x + 12 + 27 * 8 + 6, y + j * 10 + 2, width, 6, Perf_Color (order[j], p->where == 2 ? 0.5f : 1));
	}
}

static int Perf_Longer (const void *a, const void *b)
{
	return (*(const float *)a > *(const float *)b) - (*(const float *)a < *(const float *)b);
}

/*
===============
R_PerfDraw
===============
*/
void R_PerfDraw (void)
{
	static const float	tops[] = { 2.0833f, 4.1667f, 8.3333f, 16.667f, 33.333f, 66.667f, 133.33f, 266.67f };
	char		text[160];
	const char	*limit;
	float		sorted[PERF_FRAMES];
	float		top, most, card, waiting, busy;
	int			i, x, y, width, bars;

	if (pt_perf->value <= 0 || R_Offline ())
		return;

	// The usual frame fills a little over half the height. One that took far
	// longer runs off the top, and is marked there: fitted in, it would
	// leave all the others too small to see anything of.
	memcpy (sorted, perf_total, perf_count * sizeof(float));
	qsort (sorted, perf_count, sizeof(float), Perf_Longer);
	most = perf_count ? sorted[perf_count / 2] : 0;
	top = tops[7];
	for (i=0 ; i<8 ; i++)
	{
		if (tops[i] >= most * 1.5f)
		{
			top = tops[i];
			break;
		}
	}

	width = PERF_FRAMES * PERF_COLUMN;
	bars = pt_perf->value >= 2 && rpt.width >= width + 360;
	x = 8;
	y = 48;		// clear of the lines the console puts at the top
	Draw_Touch (x, y, x + width + 8 + 12 + 27 * 8 + 8 + PERF_BAR, y + 2 * (PERF_HEIGHT + 12) + 12);

	card = waiting = 0;
	for (i=0 ; i<perf_sides[1].num ; i++)
		card += perf_sides[1].parts[i].smooth;
	for (i=0 ; i<perf_sides[0].num ; i++)
		if (perf_sides[0].parts[i].where == 2)
			waiting += perf_sides[0].parts[i].smooth;
	busy = perf_smooth_total - waiting;

	// Which of the two the frame waits for. The card is, if its work fills
	// the frame; the processor, if its own does without waiting for anything.
	if (perf_smooth_total <= 0)
		limit = "";
	else if (card >= 0.85f * perf_smooth_total)
		limit = "the card";
	else if (busy >= 0.85f * perf_smooth_total)
		limit = "the processor";
	else
		limit = "neither: the screen or a frame limit";

	Com_sprintf (text, sizeof(text), "processor %.2f ms at work, %.2f waiting    full height %.1f ms", busy, waiting, top);
	Draw_FadeBox (x, y, width, 10);
	Draw_String (x + 2, y + 1, text);
	y += 10;
	Perf_Graph (0, x, y, top);
	if (bars)
		Perf_Bars (0, x + width + 8, y, top);
	y += PERF_HEIGHT + 4;

	Com_sprintf (text, sizeof(text), "card %.2f ms, for the view before    white: the whole frame", card);
	Draw_FadeBox (x, y, width, 10);
	Draw_String (x + 2, y + 1, text);
	y += 10;
	Perf_Graph (1, x, y, top);
	if (bars)
		Perf_Bars (1, x + width + 8, y, top);
	y += PERF_HEIGHT + 4;

	Com_sprintf (text, sizeof(text), "frame %.2f ms, %.0f a second    held up by %s%s", perf_smooth_total,
		perf_smooth_total > 0 ? 1000.0f / perf_smooth_total : 0, limit, perf_frozen ? "    FROZEN" : "");
	Draw_FadeBox (x, y, width, 10);
	Draw_String (x + 2, y + 1, text);
}

// pt_perf_freeze: the graphs stand still to be looked at, or go on again
static void R_PerfFreeze_f (void)
{
	perf_frozen = !perf_frozen;
}

void R_InitPerf (void)
{
	pt_perf = ri.Cvar_Get ("pt_perf", "2", 0);
	pt_perf_spike = ri.Cvar_Get ("pt_perf_spike", "0", 0);
	pt_perf_server = ri.Cvar_Get ("pt_perf_server", "0", 0);
	pt_perf_sound = ri.Cvar_Get ("pt_perf_sound", "0", 0);
	ri.Cmd_AddCommand ("pt_perf_freeze", R_PerfFreeze_f);

	memset (perf_sides, 0, sizeof(perf_sides));
	perf_head = perf_count = 0;
	perf_smooth_total = 0;
	perf_frozen = false;
	perf_frame_at = perf_mark_at = Sys_PerfMs () - 1.0e6;	// the first frame is not one
}

void R_ShutdownPerf (void)
{
	ri.Cmd_RemoveCommand ("pt_perf_freeze");
}

#endif
