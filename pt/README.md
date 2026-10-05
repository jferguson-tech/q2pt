# pt

Engine-independent path tracing core, MIT licensed (see `LICENSE`).

Nothing in this directory includes or is derived from the Quake 2 sources.
The engine talks to it only through `include/pt.h`; the GPL glue that adapts
Quake 2's renderer interface to that header lives in `../ref_pt`.

- `include/pt.h` - the C API
- `cpu/` - CPU backend
- `rtx/` - Vulkan backend (Nvidia only for now): the same tracer as compute
  shaders using ray queries; it builds its light lists with `cpu/pt_world.cpp`
- `water/` - height field wave simulation for bodies of liquid
- `png/` - PNG writer
