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

That leaves `baseq2/render/mydemo/frameNNNNN.ptx`, one a frame (about 140 MB
each at 1280x720: they can be deleted once the pictures are made). Then:

```
python pt_denoise.py baseq2/render/mydemo --weights denoiser.pt
```

writes `frameNNNNN.png` beside them, exposed and graded as the game does it
(automatic exposure, the filmic curve). `--blur 0.5` adds motion blur to
sharp frames; frames rendered with `pt_render_blur` are denoised as they
are. The status bar and the glow around bright things (`pt_bloom`) are not
in these frames.

The film is gone over twice, from its end to its start and then from its
start to its end, so that each frame can draw on the frames on both sides
of it. `--past-only` makes one pass, in which a frame draws only on those
before it: about twice as fast and, on the test clips, under 0.1 dB worse.
`--paths 4` (or 8) uses only that many of the paths a frame was rendered
with.

The weights are not in this repository: they are a download beside a
release.

## How it works

The game exports the light of each frame in three layers (what surfaces
scatter, what they mirror, and what lies in front of them such as beams and
particles), each with the surfaces' own colour divided out, and beside them
that colour, the normals, the distance, how noisy each pixel is, and how
each pixel moved since the frame before.

The network (model.py, 4.0 million weights) sees those for one frame,
together with its own answers for the frame before and, on the second pass,
the frame after, each moved along the exported motion to where things are
now. It returns a fresh estimate of each layer and, per pixel, how much of
the neighbouring frames' answers to mix in. Textures never pass through it,
so they stay as sharp as they were rendered; mixing in the neighbouring
frames is what stops a film from flickering.

Light is scaled before the network sees it by the exposure the game would
choose for that stretch of the film, steadied over neighbouring frames.

## How good it is

`results/` has the measurements of each training run on the six held-out
maps, against references of 16,384 paths a pixel and against Intel Open
Image Denoise 2.3.3 given the same frames with their colour and normal
buffers. As of the fourth run (`results/stage4.md`) it flickers less than
Open Image Denoise at 4 and 16 paths but is still slightly behind it in
PSNR and SSIM: 38.79 dB against 38.95 dB at 16 paths, 37.64 against 38.10
at 4. It is ahead on four of the six clips at 4 paths and three at 16; the
largest loss is where a BFG is fired in a dark room.

A fifth run (`results/stage5.md`) scaled each frame by the exposure the
game would have reached by then and drew clips with flashes more often. It
flickered less but was worse overall (38.48 dB at 16 paths), and was taken
out again; the fourth run's weights are the best so far.

Frames rendered with motion blur denoise far better than sharp frames
blurred afterwards (33.6 dB against 27.7 dB at 16 paths).

## Training it again

1. `render_dataset.py --game ../../run --out DATA --split train` and again
   with `--split test`. It writes camera tours through every map
   (make_tours.py reads the maps from your own .pak files), runs the game on
   them and saves the buffers. Six maps are held out as the test set and
   never trained on. This takes hours and a few hundred GB.
2. `train.py --data DATA/train --out RUN`, and `--start OTHER/denoiser.pt`
   to carry on from weights already trained. It needs a GPU with about
   10 GB.
3. `evaluate.py --data DATA/test --weights RUN/denoiser.pt --out REPORT`
   measures it against the references, the noisy frames and, with `--oidn`,
   Intel Open Image Denoise.

No game data is kept anywhere in this: the tours are lists of camera
positions, and the rendered frames stay on your machine.
