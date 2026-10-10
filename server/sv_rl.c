/*
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
// sv_rl.c -- the server driven one frame at a time by another program.
//
// Started with "+set rl_shm <file> +set rl_fd_in <n> +set rl_fd_out <n>",
// the server maps the file (see game/g_rl.h for what is in it) and then does
// nothing until a byte arrives on the first descriptor. The request in the
// block is carried out, a byte goes back on the second, and the server waits
// again. A step is exactly one server frame of 100 ms; the wall clock is
// never read.
//
// The player is client 0 of a one-player game: a slot with no network
// address, filled as a connecting client's would be, so that the server
// builds its frames and gathers its messages. Those are what a demo is, and
// SV_RL_SendClient writes them out as a client recording would have.

#include "server.h"
#include "../game/g_rl.h"

#ifndef _WIN32

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

static qboolean		rl_started;
static rl_shared_t	*rl;
static int			rl_fd_in = -1, rl_fd_out = -1;
static qboolean		rl_stepped;		// the frame now running is a step to answer for

static FILE			*rl_demo;
static int			rl_demo_last;	// the last frame written, to delta from

/*
================
SV_RL_Init

Looks for the settings once. Without rl_shm the server runs as it always has.
================
*/
static void SV_RL_Init (void)
{
	char	*name;
	int		fd;
	void	*block;

	rl_started = true;

	name = Cvar_Get ("rl_shm", "", CVAR_NOSET)->string;
	if (!name[0])
		return;

	rl_fd_in = (int)Cvar_Get ("rl_fd_in", "-1", CVAR_NOSET)->value;
	rl_fd_out = (int)Cvar_Get ("rl_fd_out", "-1", CVAR_NOSET)->value;

	fd = open (name, O_RDWR);
	if (fd == -1)
		Com_Error (ERR_FATAL, "rl_shm: cannot open %s", name);
	block = mmap (NULL, sizeof(*rl), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	close (fd);
	if (block == MAP_FAILED)
		Com_Error (ERR_FATAL, "rl_shm: cannot map %s", name);
	rl = (rl_shared_t *)block;

	if (rl->magic != RL_MAGIC || rl->version != RL_VERSION || rl->size != (int)sizeof(*rl))
		Com_Error (ERR_FATAL, "rl_shm: %s is not version %i of the block (%i bytes)",
			name, RL_VERSION, (int)sizeof(*rl));

	Cvar_FullSet (RL_BLOCK_CVAR, va("%p", (void *)rl), CVAR_NOSET);
}

qboolean SV_RL_Active (void)
{
	if (!rl_started)
		SV_RL_Init ();
	return rl != NULL;
}

/*
================
SV_RL_Game

Passes a command to the game through the entry the "sv" console command uses.
================
*/
static void SV_RL_Game (char *what)
{
	if (!ge)
		return;
	Cmd_TokenizeString (va("sv rl %s", what), false);
	ge->ServerCommand ();
}

// ---------------------------------------------------------------------- demo

static void SV_RL_DemoBlock (sizebuf_t *buf)
{
	int		len;

	if (!buf->cursize)
		return;
	len = LittleLong (buf->cursize);
	fwrite (&len, 4, 1, rl_demo);
	fwrite (buf->data, buf->cursize, 1, rl_demo);
	buf->cursize = 0;
}

static void SV_RL_DemoStop (void)
{
	int		len;

	if (!rl_demo)
		return;
	len = -1;
	fwrite (&len, 4, 1, rl_demo);
	fclose (rl_demo);
	rl_demo = NULL;
}

/*
================
SV_RL_DemoStart

Writes what a client's "record" writes before the first frame: the server's
description, every configstring and every baseline, then the command that
makes the playing client load the map.
================
*/
static void SV_RL_DemoStart (char *name)
{
	byte		buf_data[MAX_MSGLEN];
	sizebuf_t	buf;
	int			i;
	entity_state_t	nullstate;

	rl_demo = fopen (name, "wb");
	if (!rl_demo)
	{
		rl->error = 1;
		Com_sprintf (rl->error_text, sizeof(rl->error_text), "cannot write %s", name);
		return;
	}
	rl_demo_last = -1;
	rl->demo_frames = 0;
	rl->demo_dropped = 0;

	SZ_Init (&buf, buf_data, sizeof(buf_data));

	MSG_WriteByte (&buf, svc_serverdata);
	MSG_WriteLong (&buf, PROTOCOL_VERSION);
	MSG_WriteLong (&buf, svs.spawncount);
	MSG_WriteByte (&buf, 1);		// a demo plays as an attract loop
	MSG_WriteString (&buf, Cvar_VariableString ("gamedir"));
	MSG_WriteShort (&buf, 0);		// the player's number
	MSG_WriteString (&buf, sv.configstrings[CS_NAME]);

	for (i=0 ; i<MAX_CONFIGSTRINGS ; i++)
	{
		if (!sv.configstrings[i][0])
			continue;
		if (buf.cursize + (int)strlen (sv.configstrings[i]) + 32 > buf.maxsize)
			SV_RL_DemoBlock (&buf);
		MSG_WriteByte (&buf, svc_configstring);
		MSG_WriteShort (&buf, i);
		MSG_WriteString (&buf, sv.configstrings[i]);
	}

	memset (&nullstate, 0, sizeof(nullstate));
	for (i=0 ; i<MAX_EDICTS ; i++)
	{
		if (!sv.baselines[i].modelindex && !sv.baselines[i].sound && !sv.baselines[i].effects)
			continue;
		if (buf.cursize + 64 > buf.maxsize)
			SV_RL_DemoBlock (&buf);
		MSG_WriteByte (&buf, svc_spawnbaseline);
		MSG_WriteDeltaEntity (&nullstate, &sv.baselines[i], &buf, true, true);
	}

	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, "precache\n");
	SV_RL_DemoBlock (&buf);
}

/*
================
SV_RL_SendClient

Called by SV_SendClientMessages for the driven client in place of the
network. While a demo is being recorded the frame is written to it:

	the reliable messages (prints, changed configstrings, the inventory) in
	a block of their own;
	the frame, as a delta from the last one written, followed by the frame's
	sounds and effects if they fit. On a real connection they are dropped
	when they do not, and so they are here.

A frame too large for a block is left out. The next is then a delta from the
last one that was written, as after a lost packet.
================
*/
void SV_RL_SendClient (client_t *c)
{
	byte		msg_buf[MAX_MSGLEN];
	sizebuf_t	msg;

	// never timed out
	c->lastmessage = svs.realtime;

	if (rl_demo && sv.state == ss_game)
	{
		SV_RL_DemoBlock (&c->netchan.message);

		SV_BuildClientFrame (c);

		SZ_Init (&msg, msg_buf, sizeof(msg_buf));
		msg.allowoverflow = true;
		c->lastframe = rl_demo_last;
		SV_WriteFrameToClient (c, &msg);
		if (msg.overflowed)
			rl->demo_dropped++;
		else
		{
			if (!c->datagram.overflowed && msg.cursize + c->datagram.cursize <= msg.maxsize)
				SZ_Write (&msg, c->datagram.data, c->datagram.cursize);
			SV_RL_DemoBlock (&msg);
			rl_demo_last = sv.framenum;
			rl->demo_frames++;
		}
	}

	// SZ_Clear takes the overflowed mark off too: more than a block of
	// reliable messages in one frame loses them, and nothing else
	SZ_Clear (&c->netchan.message);
	SZ_Clear (&c->datagram);
}

qboolean SV_RL_IsClient (client_t *c)
{
	return rl && c == svs.clients && c->state == cs_spawned;
}

// --------------------------------------------------------------------- reset

/*
================
SV_RL_Reset

Starts an episode: seeds the random numbers, loads the map with single-player
rules and a freshly loaded game library, and puts the player in.
================
*/
static void SV_RL_Reset (void)
{
	client_t	*cl;
	edict_t		*ent;
	netadr_t	adr;
	char		map[sizeof(rl->map)];

	SV_RL_DemoStop ();

	rl->map[sizeof(rl->map)-1] = 0;
	rl->demo[sizeof(rl->demo)-1] = 0;
	strcpy (map, rl->map);
	if (FS_LoadFile (va("maps/%s.bsp", map), NULL) == -1)
	{
		rl->error = 1;
		Com_sprintf (rl->error_text, sizeof(rl->error_text), "no map %s", map);
		return;
	}

	Cvar_FullSet ("sv_singleplayer", "1", 0);
	Cvar_FullSet ("deathmatch", "0", CVAR_SERVERINFO | CVAR_LATCH);
	Cvar_FullSet ("coop", "0", CVAR_SERVERINFO | CVAR_LATCH);
	Cvar_FullSet ("skill", va("%i", rl->skill), CVAR_SERVERINFO | CVAR_LATCH);

	// Everything random from here on follows from the seed: the server and
	// the game draw from the same generator.
	srand ((unsigned)rl->seed);

	// From ss_dead SV_Map shuts the game library down and loads it again, so
	// nothing is left over from the episode before.
	sv.state = ss_dead;
	SV_Map (false, map, false);
	if (sv.state != ss_game)
	{
		rl->error = 1;
		Com_sprintf (rl->error_text, sizeof(rl->error_text), "%s did not start", map);
		return;
	}

	// A map starts with its clock a second ahead of the server's, and the
	// server would spend its first frames catching up without running the
	// game. Level here, the first step is a game frame like every other.
	svs.realtime = sv.time;

	// the player, as SV_DirectConnect and SV_Begin_f bring one in
	cl = &svs.clients[0];
	memset (cl, 0, sizeof(*cl));
	ent = EDICT_NUM(1);
	ent->s.number = 1;
	cl->edict = ent;
	strcpy (cl->userinfo, "\\name\\Player\\skin\\male/grunt\\hand\\2\\fov\\90\\msg\\0");
	if (!ge->ClientConnect (ent, cl->userinfo))
	{
		rl->error = 1;
		Com_sprintf (rl->error_text, sizeof(rl->error_text), "the game refused the player");
		return;
	}
	SV_UserinfoChanged (cl);

	memset (&adr, 0, sizeof(adr));
	adr.type = NA_LOOPBACK;
	Netchan_Setup (NS_SERVER, &cl->netchan, adr, 0);
	SZ_Init (&cl->datagram, cl->datagram_buf, sizeof(cl->datagram_buf));
	cl->datagram.allowoverflow = true;
	cl->lastmessage = svs.realtime;
	cl->lastframe = -1;
	cl->state = cs_spawned;

	sv_client = cl;
	sv_player = ent;
	ge->ClientBegin (ent);

	if (rl->demo[0])
		SV_RL_DemoStart (rl->demo);
	// what was sent while the player came in is in the demo's header already
	SZ_Clear (&cl->netchan.message);
	SZ_Clear (&cl->datagram);

	SV_RL_Game ("reset");
}

// --------------------------------------------------------------------- frame

static void SV_RL_Reply (void)
{
	char	b = 1;

	if (rl->done)
		SV_RL_DemoStop ();
	while (write (rl_fd_out, &b, 1) == -1 && errno == EINTR)
		;
}

/*
================
SV_RL_BeginFrame

Called before the server's frame with the time the frame was to be given.
When the server is driven this waits for a request and, for a step, returns
the step's length, so that SV_Frame runs one game frame and no more.
================
*/
int SV_RL_BeginFrame (int msec)
{
	char	b;
	int		n;

	if (!SV_RL_Active ())
		return msec;

	while (1)
	{
		n = read (rl_fd_in, &b, 1);
		if (n == -1 && errno == EINTR)
			continue;
		if (n != 1)
			Com_Quit ();		// the other program has gone

		rl->error = 0;
		rl->error_text[0] = 0;

		switch (rl->request)
		{
		case RL_REQ_RESET:
			SV_RL_Reset ();
			SV_RL_Reply ();
			break;

		case RL_REQ_STEP:
			if (sv.state != ss_game || rl->done)
			{
				rl->error = 1;
				strcpy (rl->error_text, "step without a running episode");
				SV_RL_Reply ();
				break;
			}
			SV_RL_Game ("act");
			rl_stepped = true;
			return RL_STEP_MSEC;

		case RL_REQ_QUIT:
			SV_RL_DemoStop ();
			Com_Quit ();
			break;

		default:
			rl->error = 1;
			strcpy (rl->error_text, "unknown request");
			SV_RL_Reply ();
			break;
		}
	}
}

/*
================
SV_RL_EndFrame

After the server's frame: has the game describe where the step left things,
and lets the other program go on.
================
*/
void SV_RL_EndFrame (void)
{
	if (!rl || !rl_stepped)
		return;
	rl_stepped = false;
	SV_RL_Game ("observe");
	SV_RL_Reply ();
}

#else	// _WIN32: the server is not driven from outside here

qboolean SV_RL_Active (void)
{
	return false;
}

qboolean SV_RL_IsClient (client_t *c)
{
	return false;
}

void SV_RL_SendClient (client_t *c)
{
}

int SV_RL_BeginFrame (int msec)
{
	return msec;
}

void SV_RL_EndFrame (void)
{
}

#endif
