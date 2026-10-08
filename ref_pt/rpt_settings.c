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
// rpt_settings.c -- the path tracers' console variables and quality presets
//
// Both path tracers read the same variables, so a setting means the same
// thing whichever one is running.

#include "rpt_local.h"

cvar_t	*pt_stats;				// 1: frame rate and timings on screen, 2: and in the console
cvar_t	*pt_debug;				// one component of the picture, see below
cvar_t	*pt_view;				// the scene with its materials overridden, see below
cvar_t	*pt_simd;				// CPU path tracer: 1 = the build for SSE even where there is AVX2
cvar_t	*pt_material_cache;		// keep the detail maps read from the pictures in pt_cache in the game directory

static cvar_t	*pt_quality;			// 0 low, 1 medium, 2 high, 3 ultra, -1 custom
static cvar_t	*pt_quality_applied;	// the preset the variables were last set from

// image
static cvar_t	*pt_scale;				// internal resolution as a fraction of the window
static cvar_t	*pt_taa;				// temporal anti-aliasing
static cvar_t	*pt_filter;				// 2 filtered, 1 raw but adding up at rest, 0 raw
static cvar_t	*pt_show_filter;		// keep the panel of what depends on earlier frames on screen
static cvar_t	*pt_denoise;			// passes of the spatial filter, 0-4
static cvar_t	*pt_history;			// frames of lighting kept while things change
static cvar_t	*pt_reflection_history;	// reflections are followed where they appear to be
static cvar_t	*pt_exposure;
static cvar_t	*pt_auto_exposure;		// adapt to how bright the scene is
static cvar_t	*pt_tonemap;				// 0 filmic, 1 neutral, 2 clipped like the original
static cvar_t	*pt_saturation;
static cvar_t	*pt_contrast;
static cvar_t	*pt_bloom;				// glow around bright things, 0 = none
static cvar_t	*pt_texture_filter;		// 0: the original's blocky texels
static cvar_t	*pt_threads;			// 0 = all

// lighting
static cvar_t	*pt_samples;			// paths per pixel per frame
static cvar_t	*pt_bounces;			// bounces of indirect light
static cvar_t	*pt_light_samples;		// lights weighed per shading point
static cvar_t	*pt_firefly_clamp;		// brightest a single path may be
static cvar_t	*pt_fog;					// haze and light shafts
static cvar_t	*pt_fog_density;
static cvar_t	*pt_sky;				// sky brightness
static cvar_t	*pt_lamp_glow;			// how bright lamp fixtures look to the eye
static cvar_t	*pt_surface_light;		// scales the light from glowing surfaces
static cvar_t	*pt_point_light;			// scales the map's point lights
static cvar_t	*pt_liquid_glow;			// glowing slime and lava light whole rooms; this reins them in
static cvar_t	*pt_detail_glow;			// screens, buttons and indicator lights

// reflections
static cvar_t	*pt_reflections;		// 0 none, 1 glass and water, 2 everything shiny
static cvar_t	*pt_reflection_bounces;	// 0 = as many as pt_bounces
static cvar_t	*pt_reflection_rate;	// how often rough surfaces get a reflection path
static cvar_t	*pt_refraction;			// water bends the view
static cvar_t	*pt_normal_flip;			// 1 = hand made normal maps have green pointing up
static cvar_t	*pt_water;				// 0 classic, 1 realistic, 2 simulated
static cvar_t	*pt_water_cell;			// size of a simulation cell, in map units
static cvar_t	*pt_water_caustics;		// strength of the light patterns waves throw under and beside water; 0 = none
static cvar_t	*pt_water_height;		// how tall the simulated waves are, 1 = normal
static cvar_t	*pt_water_damping;		// how fast waves die down, 1 = normal
static cvar_t	*pt_waves;				// ripple strength on liquids

// materials: scale what rpt_material.c decides
static cvar_t	*pt_bump;
static cvar_t	*pt_roughness;
static cvar_t	*pt_metallic;
static cvar_t	*pt_material_maps;		// 1: relief and roughness read from each picture's painted light, 0: brightness as height

float	r_skyscale = 2;
float	r_lampglow = 1.5f;
float	r_surfacelight = 1, r_pointlight = 1, r_liquidglow = 0.25f;
float	r_detailglow = 1;
int		r_watermode = 2;
int		r_normalflip;
float	r_watercell = 8, r_waterwaves = 1, r_watercaustics = 0, r_waterdamping = 1;
float	r_bumpscale = 1, r_roughscale = 1, r_metalscale = 1;
int		r_materialmaps = 1;

#define	NUM_PRESETS	4

// what each quality preset sets. Everything else is left as it is.
static struct
{
	char	*name;
	cvar_t	**var;
	float	value[NUM_PRESETS];		// low, medium, high, ultra
} presets[] =
{
	{ "pt_scale",				&pt_scale,				{ 0.33f, 0.5f, 0.75f, 1 } },
	{ "pt_samples",				&pt_samples,			{ 1, 1, 1, 2 } },
	{ "pt_bounces",				&pt_bounces,			{ 1, 3, 3, 4 } },
	{ "pt_light_samples",		&pt_light_samples,		{ 4, 8, 12, 16 } },
	{ "pt_reflections",			&pt_reflections,		{ 1, 2, 2, 2 } },
	{ "pt_reflection_bounces",	&pt_reflection_bounces,	{ 1, 0, 0, 0 } },
	{ "pt_reflection_rate",		&pt_reflection_rate,	{ 0.5f, 1, 1, 1 } },
};

#define	NUM_PRESET_VARS	(sizeof(presets) / sizeof(presets[0]))

/*
===============
R_InitSettings
===============
*/
void R_InitSettings (void)
{
	pt_stats = ri.Cvar_Get ("pt_stats", "1", CVAR_ARCHIVE);
	// 1 surface colour, 2 diffuse light, 3 specular, 4 glass and water layers,
	// 5 unfiltered extras, 6 normals, 7 history length, 8 layer history length,
	// 9 seen through water, 10 depth
	pt_debug = ri.Cvar_Get ("pt_debug", "0", 0);
	// 1 clay: every surface matte grey, 2 mirror: every surface smooth.
	// Not kept in the config: the game does not start in one of these.
	pt_view = ri.Cvar_Get ("pt_view", "0", 0);
	// which build of the CPU path tracer runs is settled when it starts, so
	// changing this starts the renderer again: see R_BeginFrame
	pt_simd = ri.Cvar_Get ("pt_simd", "0", 0);

	pt_quality = ri.Cvar_Get ("pt_quality", "1", CVAR_ARCHIVE);
	pt_quality_applied = ri.Cvar_Get ("pt_quality_applied", "1", CVAR_ARCHIVE);

	pt_scale = ri.Cvar_Get ("pt_scale", "0.5", CVAR_ARCHIVE);
	pt_taa = ri.Cvar_Get ("pt_taa", "1", CVAR_ARCHIVE);
	// The picture as the paths alone make it, noise and all, for when what
	// blending frames and filtering leave behind is worse than the noise.
	// 1 lets frames add up while the eye is at rest; 0 never does.
	pt_filter = ri.Cvar_Get ("pt_filter", "2", CVAR_ARCHIVE);
	pt_show_filter = ri.Cvar_Get ("pt_show_filter", "0", 0);
	pt_denoise = ri.Cvar_Get ("pt_denoise", "4", CVAR_ARCHIVE);
	pt_history = ri.Cvar_Get ("pt_history", "32", CVAR_ARCHIVE);
	pt_reflection_history = ri.Cvar_Get ("pt_reflection_history", "1", CVAR_ARCHIVE);
	pt_exposure = ri.Cvar_Get ("pt_exposure", "2", CVAR_ARCHIVE);
	pt_auto_exposure = ri.Cvar_Get ("pt_auto_exposure", "1", CVAR_ARCHIVE);
	pt_tonemap = ri.Cvar_Get ("pt_tonemap", "0", CVAR_ARCHIVE);
	pt_saturation = ri.Cvar_Get ("pt_saturation", "1", CVAR_ARCHIVE);
	pt_contrast = ri.Cvar_Get ("pt_contrast", "1", CVAR_ARCHIVE);
	pt_bloom = ri.Cvar_Get ("pt_bloom", "0.3", CVAR_ARCHIVE);
	pt_texture_filter = ri.Cvar_Get ("pt_texture_filter", "1", CVAR_ARCHIVE);
	pt_threads = ri.Cvar_Get ("pt_threads", "0", CVAR_ARCHIVE);

	pt_samples = ri.Cvar_Get ("pt_samples", "1", CVAR_ARCHIVE);
	pt_bounces = ri.Cvar_Get ("pt_bounces", "3", CVAR_ARCHIVE);
	pt_light_samples = ri.Cvar_Get ("pt_light_samples", "8", CVAR_ARCHIVE);
	pt_firefly_clamp = ri.Cvar_Get ("pt_firefly_clamp", "40", CVAR_ARCHIVE);
	pt_fog = ri.Cvar_Get ("pt_fog", "1", CVAR_ARCHIVE);
	pt_fog_density = ri.Cvar_Get ("pt_fog_density", "0.0004", CVAR_ARCHIVE);
	pt_sky = ri.Cvar_Get ("pt_sky", "2", CVAR_ARCHIVE);
	pt_lamp_glow = ri.Cvar_Get ("pt_lamp_glow", "1.5", CVAR_ARCHIVE);
	pt_surface_light = ri.Cvar_Get ("pt_surface_light", "1", CVAR_ARCHIVE);
	pt_point_light = ri.Cvar_Get ("pt_point_light", "1", CVAR_ARCHIVE);
	pt_liquid_glow = ri.Cvar_Get ("pt_liquid_glow", "0.25", CVAR_ARCHIVE);
	pt_detail_glow = ri.Cvar_Get ("pt_detail_glow", "1", CVAR_ARCHIVE);

	pt_reflections = ri.Cvar_Get ("pt_reflections", "2", CVAR_ARCHIVE);
	pt_reflection_bounces = ri.Cvar_Get ("pt_reflection_bounces", "0", CVAR_ARCHIVE);
	pt_reflection_rate = ri.Cvar_Get ("pt_reflection_rate", "1", CVAR_ARCHIVE);
	pt_refraction = ri.Cvar_Get ("pt_refraction", "1", CVAR_ARCHIVE);
	pt_waves = ri.Cvar_Get ("pt_waves", "1", CVAR_ARCHIVE);
	pt_water = ri.Cvar_Get ("pt_water", "2", CVAR_ARCHIVE);
	pt_normal_flip = ri.Cvar_Get ("pt_normal_flip", "0", CVAR_ARCHIVE);
	pt_water_cell = ri.Cvar_Get ("pt_water_cell", "8", CVAR_ARCHIVE);
	pt_water_caustics = ri.Cvar_Get ("pt_water_caustics", "0", CVAR_ARCHIVE);
	pt_water_damping = ri.Cvar_Get ("pt_water_damping", "1", CVAR_ARCHIVE);
	pt_water_height = ri.Cvar_Get ("pt_water_height", "1", CVAR_ARCHIVE);

	pt_bump = ri.Cvar_Get ("pt_bump", "1", CVAR_ARCHIVE);
	pt_roughness = ri.Cvar_Get ("pt_roughness", "1", CVAR_ARCHIVE);
	pt_metallic = ri.Cvar_Get ("pt_metallic", "1", CVAR_ARCHIVE);
	pt_material_maps = ri.Cvar_Get ("pt_material_maps", "1", CVAR_ARCHIVE);
	pt_material_cache = ri.Cvar_Get ("pt_material_cache", "1", CVAR_ARCHIVE);

	r_skyscale = pt_sky->value;
	r_lampglow = pt_lamp_glow->value;
	r_surfacelight = pt_surface_light->value;
	r_pointlight = pt_point_light->value;
	r_liquidglow = pt_liquid_glow->value;
	r_detailglow = pt_detail_glow->value;
	r_watermode = (int)pt_water->value;
	r_watercell = pt_water_cell->value < 2 ? 2 : pt_water_cell->value;
	r_bumpscale = pt_bump->value;
	r_roughscale = pt_roughness->value;
	r_metalscale = pt_metallic->value;
	r_materialmaps = pt_material_maps->value != 0;
}

/*
===============
R_UpdatePreset

pt_quality may have been set from the menu while another renderer was
running, so it is compared with what was last applied rather than watched
for changes.
===============
*/
static void R_UpdatePreset (void)
{
	int		quality, i;

	quality = (int)pt_quality->value;
	if (quality >= NUM_PRESETS)
		quality = NUM_PRESETS - 1;

	if (quality != (int)pt_quality_applied->value)
	{
		if (quality >= 0)
		{
			for (i=0 ; i<(int)NUM_PRESET_VARS ; i++)
				ri.Cvar_SetValue (presets[i].name, presets[i].value[quality]);
		}
		else
			quality = -1;
		ri.Cvar_SetValue ("pt_quality", quality);
		ri.Cvar_SetValue ("pt_quality_applied", quality);
		return;
	}

	if (quality < 0)
		return;

	// one of the preset's variables was changed by hand: it is a custom setup now
	for (i=0 ; i<(int)NUM_PRESET_VARS ; i++)
	{
		if (fabs ((*presets[i].var)->value - presets[i].value[quality]) > 0.001)
		{
			ri.Cvar_SetValue ("pt_quality", -1);
			ri.Cvar_SetValue ("pt_quality_applied", -1);
			return;
		}
	}
}

/*
===============
R_UpdateSettings

Called every frame. Returns true if the map has to be handed to the
backend again because something baked into it changed.
===============
*/
qboolean R_UpdateSettings (void)
{
	qboolean	reload = false;

	R_UpdatePreset ();

	// these act on the running simulations
	r_waterwaves = pt_water_height->value;
	r_normalflip = pt_normal_flip->value != 0;	// takes effect when the materials are next made
	r_watercaustics = pt_water_caustics->value;
	r_waterdamping = pt_water_damping->value;

	// these change what the map's liquids are made of
	if ((int)pt_water->value != r_watermode || (pt_water_cell->value >= 2 && pt_water_cell->value != r_watercell))
	{
		r_watermode = (int)pt_water->value;
		r_watercell = pt_water_cell->value < 2 ? 2 : pt_water_cell->value;
		reload = true;
	}

	if (pt_sky->value != r_skyscale || pt_lamp_glow->value != r_lampglow
		|| pt_surface_light->value != r_surfacelight || pt_point_light->value != r_pointlight
		|| pt_liquid_glow->value != r_liquidglow || pt_detail_glow->value != r_detailglow)
	{
		r_detailglow = pt_detail_glow->value;
		r_skyscale = pt_sky->value;
		r_lampglow = pt_lamp_glow->value;
		r_surfacelight = pt_surface_light->value;
		r_pointlight = pt_point_light->value;
		r_liquidglow = pt_liquid_glow->value;
		reload = true;
	}

	if (pt_bump->value != r_bumpscale || pt_roughness->value != r_roughscale
		|| pt_metallic->value != r_metalscale || (pt_material_maps->value != 0) != r_materialmaps)
	{
		r_bumpscale = pt_bump->value;
		r_roughscale = pt_roughness->value;
		r_metalscale = pt_metallic->value;
		r_materialmaps = pt_material_maps->value != 0;
		R_MaterialsChanged ();		// the generated maps hold the old values
		reload = true;
	}

	return reload;
}

/*
===============
R_ViewMode

pt_view, which offline rendering and screenshots honour as the game does
===============
*/
static void R_ViewMode (pt_view_t *view)
{
	static int	was;
	int			mode;

	mode = (int)pt_view->value;
	if (mode < PT_VIEW_NORMAL || mode >= PT_NUM_VIEWS)
		mode = PT_VIEW_NORMAL;
	view->view_mode = mode;

	// nothing is followed off a surface with reflections off, and a mirror
	// would show nothing
	if (mode == PT_VIEW_MIRROR)
		view->reflections = 2;

	// a view that numbers are read off is shown as it is traced
	if (PT_VIEW_IS_MEASURE(mode))
	{
		view->exposure = 1;
		view->auto_exposure = 0;
		view->tonemap = 2;
		view->saturation = 1;
		view->contrast = 1;
		view->bloom = 0;
		// but the cost is of the picture as it is, the air's light included
		if (mode != PT_VIEW_COST)
			view->fog = 0;
	}

	// the light gathered so far is of the other view
	if (mode != was)
		view->restart = 1;
	was = mode;
}

/*
===============
R_ViewSettings
===============
*/
void R_ViewSettings (pt_view_t *view)
{
	view->scale = pt_scale->value;
	view->samples = pt_samples->value;
	view->bounces = pt_bounces->value;
	view->exposure = pt_exposure->value;
	view->antialias = pt_taa->value != 0;
	view->filter = pt_filter->value;
	view->debug = pt_debug->value;

	view->reflections = pt_reflections->value;
	view->reflection_bounces = pt_reflection_bounces->value;
	view->reflection_rate = pt_reflection_rate->value;
	view->refraction = pt_refraction->value != 0;
	view->wave_strength = pt_waves->value;

	view->light_samples = pt_light_samples->value;
	view->firefly_clamp = pt_firefly_clamp->value;

	view->texture_filter = pt_texture_filter->value != 0;
	view->denoise = pt_denoise->value;
	view->history = pt_history->value;
	view->reflection_history = pt_reflection_history->value != 0;
	view->threads = pt_threads->value;

	view->auto_exposure = pt_auto_exposure->value != 0;
	view->tonemap = pt_tonemap->value;
	view->saturation = pt_saturation->value;
	view->contrast = pt_contrast->value;
	view->bloom = pt_bloom->value;
	view->fog = pt_fog->value != 0;
	view->fog_density = pt_fog_density->value;

	if (R_Offline ())
		R_OfflineSettings (view);

	R_ViewMode (view);
}

/*
=============================================================================

What of the picture depends on earlier frames, on screen

Each of these can be switched on its own to find which one a fault in the
picture comes from (pt_filter, and pt_switch on the number pad). When
one of them changes, a panel lists them all for a few seconds, what is on
and what is off, with the one that changed marked. pt_show_filter 1 keeps
the panel up.

=============================================================================
*/

#define	NUM_FILTER_ROWS		7
#define	FILTER_ROW_CHARS	64
#define	FILTER_PANEL_MSEC	6000

// One row: what it is, ON or OFF, what it is set to, and whether that makes
// any difference at the moment. In a raw picture most of them make none
// whichever way they are set, and the row has to say both things.
static void R_FilterRow (char *row, const char *label, qboolean on, const char *detail, qboolean unused)
{
	Com_sprintf (row, FILTER_ROW_CHARS, "%-28s %-4s%-15s%s", label, on ? "ON" : "OFF",
		on ? detail : "", unused ? "no effect: raw" : "");
}

// the rows in the order of the number pad keys, the first being F7's
static void R_FilterRows (char rows[NUM_FILTER_ROWS][FILTER_ROW_CHARS], float values[NUM_FILTER_ROWS])
{
	static const char	*pictures[] = { "RAW", "RAW, adds up at rest", "FILTERED" };
	int			filter;
	qboolean	raw;
	char		text[32];

	filter = pt_filter->value < 0 ? 0 : (pt_filter->value > 2 ? 2 : (int)pt_filter->value);
	raw = filter != 2;

	values[0] = filter;
	Com_sprintf (rows[0], FILTER_ROW_CHARS, "%-28s %s", "F7 picture", pictures[filter]);

	values[1] = pt_taa->value != 0;
	R_FilterRow (rows[1], " 1 anti-aliasing, upscaler", values[1] != 0, "", raw);

	values[2] = pt_history->value;
	Com_sprintf (text, sizeof(text), "%d frames", (int)pt_history->value);
	R_FilterRow (rows[2], " 2 light history, moving", pt_history->value > 1, text, raw);

	values[3] = pt_denoise->value;
	Com_sprintf (text, sizeof(text), "%d passes", (int)pt_denoise->value);
	R_FilterRow (rows[3], " 3 noise filter", pt_denoise->value > 0, text, raw);

	values[4] = pt_auto_exposure->value != 0;
	R_FilterRow (rows[4], " 4 auto exposure", values[4] != 0, "", false);

	values[5] = pt_scale->value;
	Com_sprintf (text, sizeof(text), "traced at %d%%", (int)(pt_scale->value * 100 + 0.5f));
	R_FilterRow (rows[5], " 5 upscaling", pt_scale->value < 1, text, false);

	values[6] = pt_debug->value;
	Com_sprintf (text, sizeof(text), "debug view %d", (int)pt_debug->value);
	R_FilterRow (rows[6], " 6 history view", pt_debug->value != 0, pt_debug->value == 7 ? "" : text, false);
}

/*
=============
R_DrawFilterPanel

Called once a frame, after the view
=============
*/
void R_DrawFilterPanel (refdef_t *fd)
{
	static float	was[NUM_FILTER_ROWS];
	static int		changed_at[NUM_FILTER_ROWS];
	static int		shown_until;
	static qboolean	known;
	char			rows[NUM_FILTER_ROWS][FILTER_ROW_CHARS];
	float			values[NUM_FILTER_ROWS];
	char			line[FILTER_ROW_CHARS + 2];
	int				i, j, now, x, y;

	R_FilterRows (rows, values);
	now = Sys_Milliseconds ();
	for (i=0 ; i<NUM_FILTER_ROWS ; i++)
	{
		if (known && values[i] != was[i])
		{
			changed_at[i] = now;
			shown_until = now + FILTER_PANEL_MSEC;
		}
		was[i] = values[i];
	}
	if (!known)
	{
		// nothing has been changed yet: nothing to say
		known = true;
		shown_until = now - 1;
		for (i=0 ; i<NUM_FILTER_ROWS ; i++)
			changed_at[i] = now - FILTER_PANEL_MSEC;
	}
	if (now >= shown_until && !pt_show_filter->value)
		return;

	// under the console's lines at the top left
	x = fd->x + 4;
	y = fd->y + 44;
	Draw_FadeBox (x, y, (FILTER_ROW_CHARS + 1) * 8, NUM_FILTER_ROWS * 10 + 6);
	for (i=0 ; i<NUM_FILTER_ROWS ; i++)
	{
		// the one just changed: marked, and in the other colour
		const qboolean	fresh = now - changed_at[i] < FILTER_PANEL_MSEC;

		line[0] = fresh ? '>' : ' ';
		strncpy (line + 1, rows[i], FILTER_ROW_CHARS);
		line[FILTER_ROW_CHARS] = 0;
		if (fresh)
			for (j=0 ; line[j] ; j++)
				line[j] |= 128;
		Draw_String (x + 4, y + 4 + i * 10, line);
	}
}
