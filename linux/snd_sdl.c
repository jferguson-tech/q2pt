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
// snd_sdl.c -- sound through SDL: the mixer paints into a ring of samples
// and SDL's callback plays from it

#include <SDL.h>

#include "../client/client.h"
#include "../client/snd_loc.h"

static qboolean	snd_inited;
static int		snd_pos;		// where in the ring the card is, in bytes
static int		snd_size;		// of the ring, in bytes

static void SNDDMA_Callback (void *userdata, Uint8 *stream, int len)
{
	int		first;

	if (!snd_inited || !dma.buffer)
	{
		memset (stream, 0, len);
		return;
	}
	while (len > 0)
	{
		first = snd_size - snd_pos;
		if (first > len)
			first = len;
		memcpy (stream, dma.buffer + snd_pos, first);
		stream += first;
		len -= first;
		snd_pos = (snd_pos + first) % snd_size;
	}
}

qboolean SNDDMA_Init (void)
{
	SDL_AudioSpec	want, have;
	cvar_t			*s_khz;

	if (snd_inited)
		return true;

	if (SDL_InitSubSystem (SDL_INIT_AUDIO) < 0)
	{
		Com_Printf ("SDL audio: %s\n", SDL_GetError ());
		return false;
	}

	s_khz = Cvar_Get ("s_khz", "22", CVAR_ARCHIVE);

	memset (&want, 0, sizeof(want));
	want.freq = s_khz->value >= 44 ? 44100 : (s_khz->value >= 22 ? 22050 : 11025);
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = want.freq >= 44100 ? 1024 : 512;
	want.callback = SNDDMA_Callback;

	// SDL converts if the card wants something else
	if (SDL_OpenAudio (&want, &have) < 0)
	{
		Com_Printf ("SDL audio: %s\n", SDL_GetError ());
		SDL_QuitSubSystem (SDL_INIT_AUDIO);
		return false;
	}

	dma.samplebits = 16;
	dma.channels = have.channels;
	dma.speed = have.freq;
	dma.samples = 32768;		// mono samples in the ring: a power of two
	dma.submission_chunk = 1;
	dma.samplepos = 0;
	snd_size = dma.samples * (dma.samplebits / 8);
	dma.buffer = calloc (1, snd_size);
	snd_pos = 0;

	snd_inited = true;
	SDL_PauseAudio (0);
	return true;
}

int SNDDMA_GetDMAPos (void)
{
	return snd_pos / (dma.samplebits / 8);
}

void SNDDMA_Shutdown (void)
{
	if (!snd_inited)
		return;
	snd_inited = false;
	SDL_PauseAudio (1);
	SDL_CloseAudio ();
	SDL_QuitSubSystem (SDL_INIT_AUDIO);
	free (dma.buffer);
	dma.buffer = NULL;
}

void SNDDMA_BeginPainting (void)
{
	SDL_LockAudio ();
}

void SNDDMA_Submit (void)
{
	SDL_UnlockAudio ();
}

void S_Activate (qboolean active)
{
	if (snd_inited)
		SDL_PauseAudio (!active);
}
