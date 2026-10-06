# An offline denoiser for `pt_render`

A small neural network that turns frames rendered with a few paths a pixel
into something close to frames rendered with thousands, for films made with
`pt_render`. It is run after the game, on the buffers the game saves with
`pt_render_export 1`, and it works the same on frames from either renderer.

Everything here is MIT licensed, like `pt/`. It needs Python 3.10 or later
with PyTorch, NumPy and Pillow (`pip install -r requirements.txt`).

## Using it

In the game:

```
pt_render_export 1
pt_render mydemo 60 16
```

That leaves `baseq2/render/mydemo/frameNNNNN.ptx`, one a frame (about 80 MB
each at 1280x720: they can be deleted once the pictures are made). Then:

```
python pt_denoise.py baseq2/render/mydemo --weights denoiser.pt
```

writes `frameNNNNN.png` beside them, exposed and graded as the game does it
(automatic exposure, the filmic curve). `--blur 0.5` adds motion blur to
sharp frames; frames rendered with `pt_render_blur` are denoised as they
are. The status bar and the glow around bright things (`pt_bloom`) are not
in these frames.

The weights are not in this repository: they are a download beside a
release.

## How it works

The network (model.py) sees, for each frame, the noisy light with the
surfaces' own colour divided out, that colour, the normals, the distance,
and its own answer for the frame before, moved along the motion the game
exported to where things are now. It returns a fresh estimate and, per
pixel, how much of the last answer to keep. Textures never pass through it,
so they stay as sharp as they were rendered; keeping part of the last
answer is what stops a film from flickering.

## Training it again

1. `render_dataset.py --game ../../run --out DATA --split train` and again
   with `--split test`. It writes camera tours through every map
   (make_tours.py reads the maps from your own .pak files), runs the game on
   them and saves the buffers. Six maps are held out as the test set and
   never trained on. This takes hours and a few hundred GB.
2. `train.py --data DATA/train --out RUN`
3. `evaluate.py --data DATA/test --weights RUN/denoiser.pt --out REPORT`
   measures it against the references, the noisy frames and, with `--oidn`,
   Intel Open Image Denoise.

No game data is kept anywhere in this: the tours are lists of camera
positions, and the rendered frames stay on your machine.
