# q2pt

[![build](https://github.com/jferguson-tech/q2pt/actions/workflows/build.yml/badge.svg)](https://github.com/jferguson-tech/q2pt/actions/workflows/build.yml)

A path traced renderer for the original Quake 2 source release (3.21), ported
to 64-bit Windows and Linux. The game can be switched while it runs between the original
OpenGL renderer and a path tracer that lights every frame by tracing paths
through the map: no lightmaps and no rasterized geometry. The path tracer
runs on the CPU, or on the GPU with an Nvidia RTX card. The path tracing core is
a separate, engine-independent library under the MIT license.

![Forty-three seconds of play in the jail, path traced: into a hall lit by red wall panels where guards wait, through hazy rooms with light coming down in shafts, past barred windows throwing their pattern on the floor, and out into a red-lit pit with sparks falling](docs/images/gameplay.webp)

*Rendered offline from a recorded demo, with fog and motion blur, at 30 frames
a second.*

## Features

**Path tracer** (on the CPU: `ref_ptcpu.dll`)

* Every light in the map lights the scene directly: surface lights, point
  lights and spotlights from the map's entities, the sky, and dynamic lights.
  Lights are importance sampled; indirect light comes from further bounces.
* Physically based materials: GGX specular with roughness and metallic, normal
  maps and smooth shading on models. Surface properties are guessed from the
  texture names and can be set per texture in `pt_materials.txt`, or given as
  hand-made maps beside a texture: `<name>_n.tga` (normals), `<name>_r.tga`
  (roughness) and `<name>_e.tga` (emission).
* Glass and liquids reflect and refract with a Fresnel term.
* Three ways to draw water: classic (the original swimming texture), realistic
  (rippled, reflecting and refracting) and simulated (a wave simulation per
  pool, with wakes from the player and whatever moves at the surface).
* Fog and light shafts from single scattering along the view ray.
* A denoiser (reprojected history and an edge-stopping spatial filter),
  temporal anti-aliasing that also upscales from a lower internal resolution,
  auto exposure, bloom and a choice of tone mapping.
* Quality presets and a menu page for the main settings; everything else is a
  console variable (`pt_*`).
* On-screen performance info: frame rate, frame times and where the time
  goes, on either path tracer.
* The 64-bit renderer holds two builds of the tracer and picks one when it
  starts: one for processors with AVX2 and FMA, which puts rays to a tree
  with eight children to a node and filters eight pixels at a time, and one
  for any processor. `pt_bench` says which one ran.

**Offline demo rendering**

Play and record a demo with any renderer, then render it frame by frame at
settings far too slow to play with: `pt_render <demo> [fps] [paths per pixel]`.
Each frame is built from nothing at full resolution and saved as a PNG, with
motion blur from the eye's movement (`pt_render_blur`, half the frame's time
unless set otherwise). The sound is mixed in step into a WAV, and a script is
written that turns both into a video with ffmpeg.

**Benchmark**

`pt_bench [demo] [seconds]` plays a demo as fast as the renderer goes and
says how fast that was: frames a second, how the frame times are spread
(average, median, the slowest one in a hundred, the worst) and the average
and worst time of each part of the work, for either path tracer. The demo is
stepped a sixtieth of a second per frame however long a frame takes, so every
machine and both renderers draw the same frames. The report names the
processor or the card and the settings that matter, and is added to
`baseq2\pt_bench.txt`. To compare two machines, give both the same video mode
and `pt_quality`.

**RTX renderer** (`ref_ptrtx.dll`)

The same path tracer on the GPU, for Nvidia RTX cards, written as Vulkan
compute shaders that trace with ray queries. The map and everything that moves
are held in acceleration structures, the moving part rebuilt every frame. The
shaders follow the CPU tracer function for function, and the lights and the
tables for finding them are built by the CPU tracer's own code, so the two
light a map the same way. It has the lighting, materials, glass and liquids,
fog, the three water modes, the denoiser, anti-aliasing, auto exposure, bloom,
tone mapping, screenshots and offline rendering.

Measured on an RTX 4090 beside a 16 core Ryzen 7950X, on the first map at
800x600 with one path per pixel and three bounces: about 190 frames a second,
where the CPU renderer manages 9. Offline frames at 64 paths per pixel take
about a quarter of a second each, some twenty times faster than on the CPU.

Not yet on the GPU: the separate history the CPU renderer keeps for mirror
reflections. Adaptive sampling there goes by how long a point has been in
view, not also by how noisy it is. Its denoiser decides how far to smooth
from how long a pixel has been in view rather than from measured noise.

Like the CPU renderer it can trace a smaller picture than the window and
build the full size one from it over a few frames (`pt_scale`). At 5120x1440
on the RTX 4090, medium preset: 20 frames a second traced at full size, 49 at
half the width and height, 100 at a quarter.

## Requirements

Your own copy of Quake 2 for the game data, on either system. None of it is
in this repository.

**Windows**

* Windows 10 or 11
* Visual Studio 2022 with the C++ desktop workload, including its CMake tools
  (they bring CMake and Ninja)
* Optional: the [Vulkan SDK](https://vulkan.lunarg.com/), to build the RTX
  renderer. Without it that renderer is left out and everything else builds.

**Linux** (64-bit, X11)

* CMake, a C and C++ compiler, SDL2, and the X11 and OpenGL headers
* For the RTX renderer: the Vulkan headers and loader, and a GLSL compiler
  (`glslc` or `glslangValidator`). Without them that renderer is left out.
* On Ubuntu: `sudo apt install cmake g++ libsdl2-dev libx11-dev libxext-dev
  libgl-dev libglu1-mesa-dev libvulkan-dev glslang-tools`

The game, the original OpenGL renderer and both path traced renderers are
built on Linux. The software renderer is not. The OpenGL renderer there uses
no extensions, so it draws walls and their lighting in two passes.

## Build

**Windows**

```
build.bat                 64-bit, with debug information
build.bat x64 Release
build.bat x86 Release     32-bit
```

The 64-bit programs go to `run\`, the 32-bit ones to `run\x86\`, and the game
library to `run\baseq2\`.

**Linux**

```
cmake -S . -B build/linux
cmake --build build/linux -j
```

`quake2`, `ref_ptcpu.so` and `ref_ptrtx.so` go to `run/`, and `gamex64.so` to
`run/baseq2/`.

The Linux build is compiled with `-Wall -Wextra` and every warning is an
error. Should a newer compiler find something new to say, `-DQ2_WERROR=OFF`
on the first `cmake` line lets it build while that is dealt with.

## Game data

Copy the contents of the `baseq2` folder of your Quake 2 installation
(`pak0.pak` and the rest) into `run\baseq2\`. The folder `run\` is ignored by
git.

## Run

```
cd run
quake2.exe          Windows
./quake2            Linux
```

The 32-bit build is started from `run\x86` with `quake2.exe +set basedir ..`.

W and S walk forward and back and A and D turn left and right, as the arrow
keys do; the mouse looks around. The keys are put on once, where nothing of
the player's own is on them, and can be changed in the options menu like any
other. The game's own setup had looking up on A and the silencer on S.

The video menu lists the original 4:3 modes and wide ones from 1280x720 up
to 3840x2160, with 21:9 and 32:9 modes up to 5120x1440. On a wide picture the
view keeps its height and shows more to the sides.

The last mode, *desktop*, is the size of the desktop, whatever that is: full
screen it fills the display exactly, a laptop's 16:10 one included, without
changing the display mode. Sizes are in real pixels: where Windows is set to
scale the display, as on most laptops, the game is not stretched by it.

**F8** steps through the renderers: OpenGL, CPU path tracer, RTX. They are
also in the video menu, which has a *path tracing options* page. Without an
Nvidia RTX card the RTX renderer is skipped. Setting `PT_VK_VALIDATE` in the
environment turns on the Vulkan validation layer for it.

Some console commands and variables:

| | |
| --- | --- |
| `pt_quality 0`-`3` | preset: low, medium, high, ultra |
| `pt_scale` | internal resolution as a fraction of the window |
| `pt_bounces`, `pt_samples`, `pt_light_samples` | path length, paths per pixel per frame, lights weighed per point |
| `pt_reflections 0`-`2` | none, glass and water, every shiny surface |
| `pt_water 0`-`2` | classic, realistic, simulated |
| `pt_fog`, `pt_bloom`, `pt_tonemap`, `pt_exposure` | the look of the picture |
| `pt_denoise`, `pt_taa`, `pt_history` | filtering over space and time |
| `pt_filter 0`-`2`, `pt_filter_cycle` (**F7**) | the picture as the paths alone make it, noise and all: `0` every frame on its own, `1` the same but frames add up while you stand still, `2` (the default) blended over time and filtered |
| `pt_switch 1`-`7` (number pad **1**-**7**) | switch off, or back on, one of the things in the picture that depend on earlier frames, to find which one a fault comes from: anti-aliasing and the upscaler, light history, the noise filter, adaptive sampling, auto exposure, upscaling, the history view. A list of them all comes up for a few seconds with what is on and off. Number pad **0** puts them all back; **.** keeps the list up (`pt_show_filter`) |
| `pt_stats 0` | hide the performance info, which is on by default (never shown in offline renders) |
| `pt_simd 0`-`1` | CPU renderer: the build for AVX2 where the processor has it, or the one for any processor, to compare the two |
| `pt_debug 1`-`11` | one part of the picture on its own |
| `screenshot`, `pt_screenshot [paths]` | the frame as shown, or rendered again at high quality |
| `record <name>`, `stop` | record a demo (the game's own commands) |
| `pt_render <demo> [fps] [paths] [start] [length]` | render a demo offline into `baseq2\render\<demo>\`; start and length, in seconds, pick a part of it |
| `pt_render_blur 0`-`1` | motion blur for offline rendering: the share of each frame's time the shutter is open; 0.5 unless set, film's 180 degree shutter |
| `pt_bench [demo] [seconds] [quit]` | time a demo: `demo1` and 20 seconds unless given, 0 for all of it; `quit` leaves the game afterwards, for scripts (`quake2 +pt_bench demo1 20 quit`) |

## How it is put together

```
pt/         the path tracing core (MIT): knows nothing about Quake 2
  include/pt.h    the C interface a host program uses
  cpu/            CPU backend: BVH, path tracer, denoiser, output
  rtx/            Vulkan backend: the same tracer as compute shaders
  water/          height field wave simulation
  png/            PNG writer
ref_pt/     the renderer DLLs (GPL): turns Quake 2's maps, models and
            per-frame scene into what pt.h asks for
client/, server/, game/, qcommon/, win32/, ref_gl/, ref_soft/
            the original engine, ported to 64 bits
linux/      the Linux build: the program's entry, video, input and sound
            on SDL2 (new), beside id's own Linux sources
```

Both path traced renderers are built from the same `ref_pt` sources and differ
only in the backend they link, so a setting or feature added to the interface
is available to both.

## Licensing

* The engine, the game and `ref_pt/` are under the GNU General Public License,
  version 2: see `LICENSE` (the same text as id Software's `gnu.txt`).
* Everything in `pt/` is under the MIT license: see `pt/LICENSE`. It includes
  no Quake 2 code and can be used on its own.
* The programs built from this repository link both and are, as a whole,
  covered by the GPL v2.

## Changes from the original

The starting point is id Software's release of the Quake 2 3.21 source of
December 2001, which is the first commit of this repository. These files of it
were changed in 2026:

| Files | What changed |
| --- | --- |
| `game/g_local.h`, `game/g_main.c`, `game/q_shared.c`, `game/q_shared.h` | 64-bit port: structure offsets, formatted printing into fixed buffers |
| `game/g_items.c` | three variables declared one way in the header and another here, which gcc refuses |
| `qcommon/common.c`, `qcommon/net_chan.c`, `qcommon/qcommon.h` | 64-bit port; leaving the game safely after an error |
| `server/sv_game.c`, `server/sv_send.c`, `server/sv_world.c` | 64-bit port |
| `ref_gl/gl_model.c`, `ref_gl/gl_rmain.c` | 64-bit port: memory for models, the renderer interface |
| `ref_soft/r_alias.c`, `r_edge.c`, `r_main.c`, `r_model.c`, `r_poly.c`, `r_rast.c`, `r_scan.c`, `r_surf.c` | 64-bit port of the software renderer, built without its x86 assembly |
| `win32/cd_win.c`, `conproc.c`, `net_wins.c`, `q_shwin.c`, `rw_imp.c`, `sys_win.c`, `q2.rc` | 64-bit port and building with current Windows headers |
| `win32/vid_dll.c`, `win32/vid_menu.c` | loading the path traced renderers, switching between them, more video modes, closing the window |
| `client/cl_scrn.c` | 64-bit port |
| `client/console.c` | the console on a picture more than 2048 pixels wide |
| `client/cl_main.c`, `client/client.h`, `client/keys.c`, `client/keys.h`, `client/vid.h` | mouse look by default; W, A, S and D do what the arrow keys do; hooks for offline demo rendering |
| `client/menu.c` | menu pages for the path tracing options and for rendering a demo; "reset defaults" keeps the WASD keys |
| `client/snd_dma.c`, `snd_loc.h`, `snd_mem.c`, `snd_mix.c`, `sound.h` | 64-bit port; mixing the sound to a file in step with offline rendering |
| `game/m_*.c`, `game/g_save.c` | braces round each row of the monster animation tables, the flash offsets and the save tables |
| `game/g_ai.c`, `g_chase.c`, `g_combat.c`, `g_monster.c`, `g_spawn.c`, `g_target.c`, `p_hud.c`; `qcommon/cmd.c`, `cmodel.c`, `files.c`; `server/sv_ccmds.c`, `sv_ents.c`, `sv_main.c`, `sv_world.c`; `client/cl_cin.c`, `cl_ents.c`, `cl_fx.c`, `cl_parse.c`, `cl_tent.c`, `qmenu.c`; `ref_gl/gl_image.c`, `gl_light.c`, `gl_local.h`, `gl_mesh.c`, `gl_model.h`, `gl_rsurf.c`; `linux/glob.c`, `net_udp.c`, `q_shlinux.c`, `qgl_linux.c` | what gcc's `-Wall -Wextra` points out, so that the Linux build can treat every warning as an error: casts between signednesses, dead variables, missing returns, defaults and braces, checked reads of save and pak files |

New beside them: `CMakeLists.txt` and `build.bat` (the build), `win32/quake2.manifest`
(what the program tells Windows about itself), `client/cl_render.c`,
`ref_pt/` and `pt/`, and for Linux `linux/linux.cmake`, `linux/spv_inc.cmake`,
`linux/sys_sdl.c`, `linux/vid_sdl.c`, `linux/snd_sdl.c` and `linux/gl_sdl.c`. The full list is `git diff --name-status` between the first
commit and `main`.

## Credits

Quake 2 was made by id Software, who released its source code under the GPL
in 2001. This is an unofficial project and is not affiliated with or endorsed
by id Software. Quake is a trademark of its owner.
