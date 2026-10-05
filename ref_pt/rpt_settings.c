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

cvar_t	*pt_stats;				// 1: timings on screen, 2: and in the console
cvar_t	*pt_debug;				// one component of the picture, see below

static cvar_t	*pt_quality;			// 0 low, 1 medium, 2 high, 3 ultra, -1 custom
static cvar_t	*pt_quality_applied;	// the preset the variables were last set from

// image
static cvar_t	*pt_scale;				// internal resolution as a fraction of the window
static cvar_t	*pt_taa;				// temporal anti-aliasing
static cvar_t	*pt_denoise;			// passes of the spatial filter, 0-4
static cvar_t	*pt_history;			// frames of lighting kept while things change
static cvar_t	*pt_exposure;
static cvar_t	*pt_texture_filter;		// 0: the original's blocky texels
static cvar_t	*pt_threads;			// 0 = all

// lighting
static cvar_t	*pt_samples;			// paths per pixel per frame
static cvar_t	*pt_bounces;			// bounces of indirect light
static cvar_t	*pt_light_samples;		// lights weighed per shading point
static cvar_t	*pt_firefly_clamp;		// brightest a single path may be
static cvar_t	*pt_sky;				// sky brightness
static cvar_t	*pt_lamp_glow;			// how bright lamp fixtures look to the eye

// reflections
static cvar_t	*pt_reflections;		// 0 none, 1 glass and water, 2 everything shiny
static cvar_t	*pt_reflection_bounces;	// 0 = as many as pt_bounces
static cvar_t	*pt_reflection_rate;	// how often rough surfaces get a reflection path
static cvar_t	*pt_refraction;			// water bends the view
static cvar_t	*pt_waves;				// ripple strength on liquids

// materials: scale what rpt_material.c decides
static cvar_t	*pt_bump;
static cvar_t	*pt_roughness;
static cvar_t	*pt_metallic;

float	r_skyscale = 2;
float	r_lampglow = 1.5f;
float	r_bumpscale = 1, r_roughscale = 1, r_metalscale = 1;

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
	pt_stats = ri.Cvar_Get ("pt_stats", "0", 0);
	// 1 surface colour, 2 diffuse light, 3 specular, 4 glass and water layers,
	// 5 unfiltered extras, 6 normals, 7 history length, 8 layer history length,
	// 9 seen through water, 10 depth
	pt_debug = ri.Cvar_Get ("pt_debug", "0", 0);

	pt_quality = ri.Cvar_Get ("pt_quality", "1", CVAR_ARCHIVE);
	pt_quality_applied = ri.Cvar_Get ("pt_quality_applied", "1", CVAR_ARCHIVE);

	pt_scale = ri.Cvar_Get ("pt_scale", "0.5", CVAR_ARCHIVE);
	pt_taa = ri.Cvar_Get ("pt_taa", "1", CVAR_ARCHIVE);
	pt_denoise = ri.Cvar_Get ("pt_denoise", "4", CVAR_ARCHIVE);
	pt_history = ri.Cvar_Get ("pt_history", "32", CVAR_ARCHIVE);
	pt_exposure = ri.Cvar_Get ("pt_exposure", "2", CVAR_ARCHIVE);
	pt_texture_filter = ri.Cvar_Get ("pt_texture_filter", "1", CVAR_ARCHIVE);
	pt_threads = ri.Cvar_Get ("pt_threads", "0", CVAR_ARCHIVE);

	pt_samples = ri.Cvar_Get ("pt_samples", "1", CVAR_ARCHIVE);
	pt_bounces = ri.Cvar_Get ("pt_bounces", "3", CVAR_ARCHIVE);
	pt_light_samples = ri.Cvar_Get ("pt_light_samples", "8", CVAR_ARCHIVE);
	pt_firefly_clamp = ri.Cvar_Get ("pt_firefly_clamp", "40", CVAR_ARCHIVE);
	pt_sky = ri.Cvar_Get ("pt_sky", "2", CVAR_ARCHIVE);
	pt_lamp_glow = ri.Cvar_Get ("pt_lamp_glow", "1.5", CVAR_ARCHIVE);

	pt_reflections = ri.Cvar_Get ("pt_reflections", "2", CVAR_ARCHIVE);
	pt_reflection_bounces = ri.Cvar_Get ("pt_reflection_bounces", "0", CVAR_ARCHIVE);
	pt_reflection_rate = ri.Cvar_Get ("pt_reflection_rate", "1", CVAR_ARCHIVE);
	pt_refraction = ri.Cvar_Get ("pt_refraction", "1", CVAR_ARCHIVE);
	pt_waves = ri.Cvar_Get ("pt_waves", "1", CVAR_ARCHIVE);

	pt_bump = ri.Cvar_Get ("pt_bump", "1", CVAR_ARCHIVE);
	pt_roughness = ri.Cvar_Get ("pt_roughness", "1", CVAR_ARCHIVE);
	pt_metallic = ri.Cvar_Get ("pt_metallic", "1", CVAR_ARCHIVE);

	r_skyscale = pt_sky->value;
	r_lampglow = pt_lamp_glow->value;
	r_bumpscale = pt_bump->value;
	r_roughscale = pt_roughness->value;
	r_metalscale = pt_metallic->value;
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

	if (pt_sky->value != r_skyscale || pt_lamp_glow->value != r_lampglow)
	{
		r_skyscale = pt_sky->value;
		r_lampglow = pt_lamp_glow->value;
		reload = true;
	}

	if (pt_bump->value != r_bumpscale || pt_roughness->value != r_roughscale
		|| pt_metallic->value != r_metalscale)
	{
		r_bumpscale = pt_bump->value;
		r_roughscale = pt_roughness->value;
		r_metalscale = pt_metallic->value;
		R_MaterialsChanged ();		// the generated maps hold the old values
		reload = true;
	}

	return reload;
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
	view->threads = pt_threads->value;
}
