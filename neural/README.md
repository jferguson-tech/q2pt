# neural: a renderer that infers the light

Training code for a third way of making the picture: the card produces cheap
per-pixel planes of the first surface and of what the lights would put on it,
and a small network turns them into the lit picture. This folder is the
training side, in Python with PyTorch, and is MIT licensed like `pt/`. It is
never linked into the game: the game's own inference, when there is one, is
Vulkan compute shaders in `pt/rtx/`.

The dataset and the weights live outside the repository, by default in
`~/q2pt-neural/`.

## The split

By map, in `split.py`: whole maps are held out and nothing of them is used
for training. Validation: jail3, mine2, waste1, q2dm3. Test: fact2, ware2,
city1, q2dm1. The other 39 maps train, and so do the shipped demos, which
play on base2 (demo1) and a training map.

## Making the dataset

```
python render_dataset.py <game folder> ~/q2pt-neural [--frames 200] [--demo-frames 300] [--paths 64]
```

walks every map with `pt_walk` (see the main README under *Offline demo
rendering*) and renders the shipped demos, each as a run of the game in
turn with `pt_render_export`, then packs each run's `.planes` files into
`~/q2pt-neural/packed/<map>/frameNNNNN.q2n` with `repack.py` and removes
them. A run already packed is skipped, so the script can be run again after
a stop. The game is at exposure 1 here, which is dim: it plays at exposure 2
with auto exposure on top.

## The planes files

With `pt_render_export 1` the RTX renderer writes every frame of a `pt_render`
run as `frameNNNNN.planes` beside its PNG (see the main README under *Offline
demo rendering*). Each frame is traced twice from the same camera: once with
one path a pixel and no bounces, for the inputs, and once with the paths asked
for added up, for the target. There is no offset within the pixel, no
filtering over space, no motion blur, exposure 1 and no tone curve, so the
planes line up to the pixel and are plain linear light.

`planes.py` reads a file (`read`), checks one (`verify`) and makes a contact
sheet of one (`sheet`):

```
python planes.py verify frame00000.planes
python planes.py sheet frame00000.planes sheet.png
```

### Format

All numbers are little endian. The file is:

| at | what |
| --- | --- |
| 0 | a 64 byte header: `Q2PTPLNS`, then as unsigned 32 bit: version (1), width, height, number of planes, offset of the table, offset of the text, length of the text, offset of the data |
| the table | one 48 byte entry per plane: the name (24 bytes, zero padded), type (unsigned 32 bit: 0 half float, 1 float, 2 byte, 3 unsigned 16 bit, 4 unsigned 32 bit, 5 signed 32 bit), channels, then as unsigned 64 bit the plane's offset and its length in bytes |
| the text | lines of `key=value` about the frame: the map, the frame number and time, the camera (`origin`, `forward`, `right`, `up`, `fov_x`, `fov_y` in degrees), the paths, bounces and light samples of the target, fog, the 64 light styles, how many of the frame's lights and triangles there were, and `clamped`, how many values were too large for a half float and were held to its largest |
| the data | each plane, top row first, a pixel's channels together, starting at a multiple of 64 bytes |

The planes, in order:

| name | type | channels | what |
| --- | --- | --- | --- |
| position | float | 3 | where in the world the pixel's surface is. For what moves, where it was last frame; seen through water, where it appears to be; the sky, far along the ray |
| normal | half | 3 | the shading normal, in the world |
| depth | half | 1 | distance along the eye ray to the first solid surface; -1 where there is none |
| albedo | byte | 3 | diffuse reflectance, as the light is multiplied by it: with what the air and any liquid on the way let through, times 255 |
| specular | byte | 3 | specular reflectance, the same way |
| roughness | byte | 1 | times 255 |
| emission | half | 3 | radiance the surface gives off towards the eye, before fog |
| metallic | byte | 1 | times 255 |
| material | signed | 1 | material index; -1 where there is no surface |
| triangle | signed | 2 | triangle index, and 1 if it is of the frame (what moves) rather than the map |
| direct_diffuse | half | 3 | what every light, the map's and the frame's, would put on the surface with nothing in the way: irradiance, so the diffuse light it would give is this times albedo over pi. The map's triangle lights are taken as points at their middle; the sky is not counted |
| direct_specular | half | 3 | the same through the specular lobe, as radiance |
| ray_diffuse | half | 3 | the direct light one path found, with its one shadow ray, over albedo and pi |
| ray_specular | half | 3 | the same through the specular lobe, over the specular reflectance |
| light_diffuse | half | 3 | the target: the diffuse light gathered over all the paths, over albedo and pi |
| light_specular | half | 3 | the specular light gathered, over the specular reflectance |
| light_layers | half | 3 | light of what is in front of the surface: glass, water, the air's glow |
| light_extra | half | 3 | exact light that is not gathered: emission and the frame's point lights, after fog |
| picture | half | 3 | the picture as the renderer puts it together, linear: albedo × light_diffuse + specular × light_specular + light_layers + light_extra |

The eye ray through the centre of pixel (x, y) is `forward + right * (2 (x +
0.5) / width - 1) * tan(fov_x / 2) + up * (1 - 2 (y + 0.5) / height) *
tan(fov_y / 2)`, normalised; `planes.py` has it as `eye_dirs`. Motion
between two frames follows from `position` and the two cameras.

A frame at 1280x720 is 92 MB as written.

### The packed form

`planes.pack` writes a `.q2n`: `Q2PTPACK`, the length of a JSON header as
unsigned 64 bit, the header (the planes' names, types, shapes, offsets and
lengths, the frame's text, the size), then each plane compressed with zstd on
its own. Two planes are left out, since they follow from the others and
`planes.read` gives them back on reading a `.q2n`: `position` from depth and
the camera (`rebuild_position`; what moves or is seen through water is then
where it is, not where it was) and `picture` from the light planes
(`rebuild_picture`). One is added when the frame follows another of the same
burst: `motion`, two half floats, where each pixel's surface was on the
previous frame's picture less where it is now, in pixels, NaN where it was
off it or behind the eye; it comes from the position plane and the previous
frame's camera, so for what moves it is where the thing was. The first frame
of a burst has no motion plane. A packed frame is 15 to 50 MB: the light
planes are noise and compress little, the surface planes compress well.

## Setting up

```
uv venv ~/q2pt-neural/venv
uv pip install --python ~/q2pt-neural/venv/bin/python -r requirements.txt
```
