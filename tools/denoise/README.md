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
choose for that stretch of the film, steadied over neighbouring frames. The
network is given it twice, once as a logarithm that is nearly linear in the
dark and once as one that spans six decades, so that a dark frame reads
much the same however it was scaled.

## How good it is

`results/` has the measurements of each training run on the six held-out
maps, against references of 16,384 paths a pixel and against Intel Open
Image Denoise 2.3.3 given the same frames with their colour and normal
buffers. From the ninth run on the test set is one rendered again with the
renderer as it then was (`results/stage8-new-test.md` has the eighth run on
it); earlier figures are on an older one and do not compare with these.

As of the ninth run (`results/stage9.md`), on sharp frames:

| | PSNR at 4 / 16 paths | SSIM | flicker |
|---|---|---|---|
| this denoiser | 39.48 / 40.63 dB | 0.9424 / 0.9471 | 6.20 / 5.75 |
| Open Image Denoise | 38.91 / 40.14 dB | 0.9433 / 0.9471 | 6.77 / 6.13 |

It is level or ahead in PSNR on all six clips at both path counts, and
slightly behind in SSIM at 4 paths, on the two clips with the most noise
left in them.

What made the difference in the dark: where little light reaches, the
references of 512 paths were themselves spiky in the light of fog and glows,
and an error capped for each pixel does not pull towards a spike, so the
network left that light out. The cap was raised (`--spike`), and the dark
clips were rendered again with 4,096 paths and a BFG fired in each
(`--dark N --flash`).

A tenth run (`results/stage10.md`), 8,000 more steps with all 92 of those
clips where the ninth had 31, changed nothing that matters: 39.47 / 40.69 dB
and SSIM 0.9422 / 0.9473.

Runs that did not help and were taken out again: the fifth
(`results/stage5.md`) scaled each frame by the exposure the game would have
reached by then, and was worse overall (38.48 dB at 16 paths). The seventh
(`results/stage7.md`) judged the light by the squared difference of its
logarithms, and its pictures came out too dark, by 16% in the darkest clip
(36.85 dB). The eighth went back to errors relative to the answer and added
the squared error of the picture as shown.

Frames rendered with motion blur denoise far better than sharp frames
blurred afterwards (33.6 dB against 27.7 dB at 16 paths).

## Training it again

1. `render_dataset.py --game ../../run --out DATA --split train` and again
   with `--split test`. It writes camera tours through every map
   (make_tours.py reads the maps from your own .pak files), runs the game on
   them and saves the buffers. Six maps are held out as the test set and
   never trained on. This takes hours and a few hundred GB. With `--dark N`
   it instead tries N clips a map and renders the dark ones; with `--dim`
   it renders every map with its lights turned down. Three maps of the
   mission packs are kept for `--split final`, a last test that nothing
   has been tuned against.
2. `train.py --data DATA/train --out RUN`, and `--start OTHER/denoiser.pt`
   to carry on from weights already trained. It needs a GPU with about
   10 GB.
3. `evaluate.py --data DATA/test --weights RUN/denoiser.pt --out REPORT`
   measures it against the references, the noisy frames and, with `--oidn`,
   Intel Open Image Denoise.

No game data is kept anywhere in this: the tours are lists of camera
positions, and the rendered frames stay on your machine.
