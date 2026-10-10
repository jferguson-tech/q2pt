# The final test, and how noisy the references are

## The final test

The tenth run's weights (results/stage10.md) and Open Image Denoise 2.3.3 on
the three maps kept aside since 2026-10-08, `xdm2`, `xmoon2` (The
Reckoning) and `rdm7` (Ground Zero): rendered on 2026-10-10 with
`render_dataset.py --split final`, each pack's pak0.pak beside the game's
own only while its maps were rendered, and measured once. Nothing was
changed after. The tables below are evaluate.py's, whole.

## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 25.47 | 0.4820 | 37.51 |
| noisy 16 | 29.76 | 0.6752 | 22.80 |
| ours 4 | 40.71 | 0.9677 | 4.22 |
| ours 8 | 41.48 | 0.9701 | 4.11 |
| ours 16 | 42.12 | 0.9718 | 4.04 |
| ours 16, past only | 42.01 | 0.9710 | 4.19 |
| ours 16, alone | 41.54 | 0.9687 | 4.91 |
| OIDN 4 | 40.36 | 0.9647 | 5.55 |
| OIDN 16 | 42.21 | 0.9699 | 4.76 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 25.28 | 0.4651 | 38.68 |
| noisy 16 | 29.47 | 0.6549 | 24.00 |
| ours 4 | 37.88 | 0.9275 | 7.22 |
| ours 8 | 38.85 | 0.9299 | 7.06 |
| ours 16 | 39.25 | 0.9314 | 6.95 |
| ours 16, past only | 39.21 | 0.9310 | 7.03 |
| ours 16, alone | 39.09 | 0.9297 | 7.42 |
| OIDN 4 | 37.66 | 0.9269 | 7.91 |
| OIDN 16 | 39.28 | 0.9318 | 7.18 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 39.25 | 0.9314 | 6.95 |
| sharp frames denoised, then blurred | 38.25 | 0.9318 | 7.25 |
| sharp reference, then blurred | 38.55 | 0.9348 | 7.09 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 25.54 | 29.80 | 40.60 | 41.40 | 42.10 | 41.77 | 41.77 | 40.67 | 42.60 |
| 2 | 25.50 | 29.74 | 41.01 | 41.77 | 42.38 | 42.18 | 41.73 | 40.54 | 42.32 |
| 3 | 25.45 | 29.66 | 41.02 | 41.76 | 42.40 | 42.24 | 41.74 | 40.29 | 42.14 |
| 4 | 25.37 | 29.63 | 40.94 | 41.68 | 42.25 | 42.11 | 41.58 | 40.05 | 41.88 |
| 5 | 25.35 | 29.64 | 40.69 | 41.46 | 42.05 | 41.95 | 41.45 | 40.00 | 41.87 |
| 6 | 25.28 | 29.53 | 40.62 | 41.35 | 41.94 | 41.84 | 41.34 | 40.08 | 41.85 |
| 7 | 25.24 | 29.49 | 40.51 | 41.24 | 41.85 | 41.75 | 41.23 | 40.02 | 41.80 |
| 8 | 25.26 | 29.53 | 40.40 | 41.17 | 41.80 | 41.73 | 41.21 | 39.99 | 41.89 |
| 9 | 25.30 | 29.60 | 40.20 | 40.97 | 41.62 | 41.56 | 41.05 | 40.11 | 42.00 |
| 10 | 25.36 | 29.65 | 40.13 | 40.92 | 41.58 | 41.55 | 41.05 | 40.21 | 42.08 |
| 11 | 25.44 | 29.76 | 40.31 | 41.09 | 41.75 | 41.72 | 41.24 | 40.41 | 42.17 |
| 12 | 25.55 | 29.90 | 40.50 | 41.29 | 41.93 | 41.91 | 41.44 | 40.67 | 42.48 |
| 13 | 27.51 | 32.22 | 44.42 | 45.63 | 46.56 | 46.62 | 45.66 | 43.29 | 45.62 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| rdm7 c1 sharp | noisy 4 | 23.71 | 0.3165 | 50.22 |
| rdm7 c1 sharp | noisy 16 | 27.83 | 0.5078 | 31.71 |
| rdm7 c1 sharp | ours 4 | 37.83 | 0.9399 | 7.00 |
| rdm7 c1 sharp | ours 8 | 38.42 | 0.9417 | 6.84 |
| rdm7 c1 sharp | ours 16 | 38.94 | 0.9431 | 6.72 |
| rdm7 c1 sharp | ours 16, past only | 38.93 | 0.9427 | 6.83 |
| rdm7 c1 sharp | ours 16, alone | 38.68 | 0.9413 | 7.31 |
| rdm7 c1 sharp | OIDN 4 | 38.60 | 0.9391 | 7.81 |
| rdm7 c1 sharp | OIDN 16 | 40.06 | 0.9431 | 7.08 |
| rdm7 c1 blurred | noisy 4 | 23.42 | 0.2975 | 52.07 |
| rdm7 c1 blurred | noisy 16 | 27.45 | 0.4826 | 33.53 |
| rdm7 c1 blurred | ours 4 | 34.57 | 0.8480 | 11.76 |
| rdm7 c1 blurred | ours 8 | 35.66 | 0.8514 | 11.53 |
| rdm7 c1 blurred | ours 16 | 36.05 | 0.8531 | 11.39 |
| rdm7 c1 blurred | ours 16, past only | 36.03 | 0.8529 | 11.47 |
| rdm7 c1 blurred | ours 16, alone | 35.98 | 0.8520 | 11.77 |
| rdm7 c1 blurred | OIDN 4 | 34.50 | 0.8469 | 12.41 |
| rdm7 c1 blurred | OIDN 16 | 36.21 | 0.8545 | 11.53 |
| xdm2 c1 sharp | noisy 4 | 26.48 | 0.5692 | 30.71 |
| xdm2 c1 sharp | noisy 16 | 31.02 | 0.7462 | 18.47 |
| xdm2 c1 sharp | ours 4 | 45.56 | 0.9853 | 2.87 |
| xdm2 c1 sharp | ours 8 | 46.37 | 0.9865 | 2.78 |
| xdm2 c1 sharp | ours 16 | 46.99 | 0.9872 | 2.73 |
| xdm2 c1 sharp | ours 16, past only | 46.69 | 0.9868 | 2.82 |
| xdm2 c1 sharp | ours 16, alone | 46.15 | 0.9854 | 3.30 |
| xdm2 c1 sharp | OIDN 4 | 43.54 | 0.9817 | 3.63 |
| xdm2 c1 sharp | OIDN 16 | 45.27 | 0.9845 | 3.15 |
| xdm2 c1 blurred | noisy 4 | 26.40 | 0.5592 | 31.34 |
| xdm2 c1 blurred | noisy 16 | 30.88 | 0.7331 | 19.20 |
| xdm2 c1 blurred | ours 4 | 42.00 | 0.9636 | 5.18 |
| xdm2 c1 blurred | ours 8 | 42.57 | 0.9647 | 5.05 |
| xdm2 c1 blurred | ours 16 | 42.89 | 0.9654 | 4.96 |
| xdm2 c1 blurred | ours 16, past only | 42.81 | 0.9651 | 5.01 |
| xdm2 c1 blurred | ours 16, alone | 42.69 | 0.9641 | 5.30 |
| xdm2 c1 blurred | OIDN 4 | 41.42 | 0.9628 | 5.55 |
| xdm2 c1 blurred | OIDN 16 | 42.59 | 0.9650 | 5.09 |
| xmoon2 c1 sharp | noisy 4 | 26.76 | 0.5477 | 32.65 |
| xmoon2 c1 sharp | noisy 16 | 31.10 | 0.7588 | 18.98 |
| xmoon2 c1 sharp | ours 4 | 41.65 | 0.9756 | 3.04 |
| xmoon2 c1 sharp | ours 8 | 42.82 | 0.9798 | 2.95 |
| xmoon2 c1 sharp | ours 16 | 43.80 | 0.9828 | 2.90 |
| xmoon2 c1 sharp | ours 16, past only | 43.50 | 0.9813 | 3.15 |
| xmoon2 c1 sharp | ours 16, alone | 42.56 | 0.9773 | 4.31 |
| xmoon2 c1 sharp | OIDN 4 | 40.13 | 0.9714 | 5.40 |
| xmoon2 c1 sharp | OIDN 16 | 42.60 | 0.9799 | 4.24 |
| xmoon2 c1 blurred | noisy 4 | 26.63 | 0.5256 | 33.75 |
| xmoon2 c1 blurred | noisy 16 | 30.83 | 0.7358 | 20.07 |
| xmoon2 c1 blurred | ours 4 | 40.39 | 0.9646 | 5.11 |
| xmoon2 c1 blurred | ours 8 | 41.26 | 0.9676 | 4.96 |
| xmoon2 c1 blurred | ours 16 | 41.77 | 0.9695 | 4.86 |
| xmoon2 c1 blurred | ours 16, past only | 41.67 | 0.9689 | 4.97 |
| xmoon2 c1 blurred | ours 16, alone | 41.33 | 0.9669 | 5.55 |
| xmoon2 c1 blurred | OIDN 4 | 39.92 | 0.9648 | 6.14 |
| xmoon2 c1 blurred | OIDN 16 | 41.60 | 0.9699 | 5.28 |

## How far two references are from each other

The sharp clips of `q2dm4` and `ware2` of the test set were rendered a
second time at 16,384 paths with other random numbers. The tracer has no
seed to set: its random numbers come from the pixel and a count of the
passes made since it started. So the third of the six lines before the
clip, which is otherwise not filmed, was filmed too, at 5,464 paths (3
frames, 4,098 passes; a frame of the clip is 4,096), by giving
`tour_clip_begin` a file to run for each clip. The game ran as before: eye,
surface colour and depth of every frame differ by nothing in both renders, to five
places.

One render against the other, measured as a denoiser is against a
reference (exposure of the first frame, the picture as shown):

| | PSNR | SSIM | flicker |
|---|---|---|---|
| q2dm4, the second render against the first | 35.56 | 0.8233 | 12.89 |
| q2dm4, the first with only its layers from the second | 35.57 | 0.8237 | 12.81 |
| q2dm4, tenth run at 4 / 16 paths against the first | 37.15 / 37.71 | 0.8835 / 0.8867 | 10.38 / 9.82 |
| q2dm4, Open Image Denoise at 4 / 16 | 37.10 / 37.67 | 0.8847 / 0.8876 | 10.52 / 9.93 |
| ware2, the second render against the first | 36.05 | 0.8204 | 13.14 |
| ware2, the first with only its layers from the second | 36.51 | 0.8368 | 12.26 |
| ware2, tenth run at 4 / 16 paths against the first | 35.58 / 37.06 | 0.8574 / 0.8738 | 12.51 / 11.13 |
| ware2, Open Image Denoise at 4 / 16 | 35.31 / 36.36 | 0.8745 / 0.8795 | 12.55 / 11.56 |

Two references differ by more than a denoised frame differs from one of
them. An answer with no error at all would score 3 dB over the first row
of each clip against either render (38.6 and 39.1 dB), since the two
renders' noise adds up in that row. The differences in SSIM between the two
denoisers on these clips, 0.001 to 0.017, are small beside the 0.18 that
two references differ by.

Nearly all of it is in the layers (fog, glows, beams). Their noise does not
fall with the paths as the other lights' does. Variance of the brightness
of one render's light, from the difference of two, at 4 paths (the
exported sets) and at 16,384:

| | diffuse | mirrored | layers |
|---|---|---|---|
| q2dm4 frame 1 | falls 4,105 times | 4,078 | 126 |
| q2dm4 frame 4 | 4,052 | 4,097 | 128 |
| q2dm4 frame 7 | 4,097 | 4,120 | 131 |
| q2dm4 frame 10 | 4,073 | 4,071 | 132 |
| ware2 frame 1 | 4,093 | 4,030 | 128 |
| ware2 frame 4 | 4,229 | 4,050 | 128 |
| ware2 frame 7 | 4,402 | 4,137 | 137 |
| ware2 frame 10 | 3,948 | 3,930 | 128 |

Independent paths would make it fall 4,096 times. The layers' falls 128
times: as if 128 of a frame's 4,096 passes were independent, so that the
layers of a reference of 16,384 paths are as noisy as 512 paths would make
them. Why has not been looked into, and nothing in the tracer was changed.

## Frames to reproduce it with

All from `render_dataset.py --split test` (tour seed 1, tag `test`, RTX
renderer, 1280x720, `pt_render_fog 1`, 30 frames a second, 16,384 paths),
on the build of denoise-exports at 334c452 (main at #60). Eye and angles
are the frame's own. "One only" is the share of pixels whose layers are
over twice the median of the 7x7 around them in the first render and not
in the second, which is noise; "both" is the share where both are, which is
something small and bright that is really there.

| map | frame | time | eye | pitch, yaw | layers / picture | the two renders: PSNR, SSIM | one only | both | most |
|---|---|---|---|---|---|---|---|---|---|
| q2dm4 | 0 | 3.63 | 218.7 -1225.9 -313.4 | -39.3, 77.3 | 0.88 | 34.13, 0.7645 | 0.001% | 0.007% | 25x |
| q2dm4 | 1 | 3.66 | 221.6 -1219.5 -306.4 | -39.6, 73.3 | 0.87 | 34.60, 0.7787 | 0.001% | 0.008% | 20x |
| q2dm4 | 2 | 3.70 | 224.4 -1213.0 -299.4 | -40.0, 69.4 | 0.84 | 35.43, 0.8227 | 0.014% | 0.008% | 14x |
| q2dm4 | 3 | 3.73 | 227.3 -1206.6 -292.5 | -40.0, 68.1 | 0.88 | 34.20, 0.7676 | 0.002% | 0.014% | 45x |
| q2dm4 | 4 | 3.76 | 230.1 -1200.2 -285.5 | -40.0, 67.1 | 0.87 | 34.63, 0.7823 | 0.004% | 0.014% | 32x |
| q2dm4 | 5 | 3.80 | 233.0 -1193.7 -278.5 | -40.0, 66.2 | 0.81 | 36.05, 0.8363 | 0.066% | 0.016% | 20x |
| q2dm4 | 6 | 3.83 | 235.8 -1187.3 -271.6 | -40.0, 66.0 | 0.79 | 36.38, 0.8467 | 0.077% | 0.019% | 10x |
| q2dm4 | 7 | 3.86 | 238.7 -1180.8 -264.6 | -40.0, 66.0 | 0.77 | 36.61, 0.8545 | 0.109% | 0.026% | 30x |
| q2dm4 | 8 | 3.89 | 241.5 -1174.4 -257.6 | -40.0, 66.0 | 0.75 | 36.76, 0.8592 | 0.145% | 0.027% | 54x |
| q2dm4 | 9 | 3.93 | 243.7 -1169.7 -252.5 | -37.4, 69.4 | 0.73 | 36.67, 0.8593 | 0.170% | 0.026% | 19x |
| q2dm4 | 10 | 3.96 | 245.7 -1165.2 -247.6 | -34.4, 73.4 | 0.73 | 36.46, 0.8563 | 0.175% | 0.025% | 47x |
| q2dm4 | 11 | 3.99 | 247.7 -1160.7 -242.8 | -31.3, 77.3 | 0.73 | 36.20, 0.8517 | 0.208% | 0.023% | 59x |
| ware2 | 0 | 3.83 | 133.1 -33.2 58.6 | -23.2, 343.3 | 0.55 | 34.44, 0.7470 | 1.329% | 0.076% | 14x |
| ware2 | 1 | 3.86 | 138.8 -34.6 61.4 | -24.4, 344.6 | 0.54 | 34.68, 0.7566 | 1.484% | 0.098% | 13x |
| ware2 | 2 | 3.89 | 144.6 -36.0 64.3 | -25.7, 345.8 | 0.53 | 34.85, 0.7634 | 1.726% | 0.128% | 12x |
| ware2 | 3 | 3.93 | 150.3 -37.4 67.1 | -25.9, 346.0 | 0.52 | 34.96, 0.7677 | 1.994% | 0.174% | 11x |
| ware2 | 4 | 3.96 | 156.1 -38.9 70.0 | -25.9, 346.0 | 0.51 | 35.06, 0.7715 | 2.311% | 0.231% | 10x |
| ware2 | 5 | 3.99 | 161.9 -40.3 72.9 | -25.9, 346.0 | 0.49 | 35.12, 0.7737 | 2.656% | 0.286% | 17x |
| ware2 | 6 | 4.03 | 167.6 -41.7 75.8 | -25.9, 346.0 | 0.81 | 37.39, 0.8707 | 0.000% | 0.012% | 353x |
| ware2 | 7 | 4.06 | 173.4 -43.2 78.7 | -25.9, 346.0 | 0.78 | 37.72, 0.8806 | 0.000% | 0.013% | 743x |
| ware2 | 8 | 4.09 | 179.1 -44.6 81.6 | -25.9, 346.0 | 0.02 | 37.77, 0.8808 | 0.085% | 0.013% | 11x |
| ware2 | 9 | 4.13 | 184.9 -46.1 84.5 | -25.9, 346.0 | 0.28 | 37.62, 0.8756 | 0.001% | 0.016% | 62x |
| ware2 | 10 | 4.16 | 190.6 -47.5 87.3 | -25.9, 346.0 | 0.41 | 37.87, 0.8778 | 0.000% | 0.006% | 191x |
| ware2 | 11 | 4.19 | 196.4 -49.0 90.2 | -25.9, 346.0 | 0.50 | 38.02, 0.8798 | 0.000% | 0.010% | 35x |

The dark frames of `ware2` (0 to 5, before the BFG goes off) have the most
pixels that stand out; in `q2dm4` the noise is an even grain over the fog
and few pixels stand out, though the renders differ as much.

The other four test clips were rendered once only. Their layers by the
same count in that one render (which cannot tell noise from what is there):

| map | frame | eye | pitch, yaw | layers / picture | pixels over 2x / 4x their surroundings | most | brightest 0.1% hold |
|---|---|---|---|---|---|---|---|
| power2 | c1 00000 | 732.1 1884.1 388.1 | 10.8, 121.4 | 0.63 | 0.06% / 0.024% | 59x | 1.5% |
| power2 | c1 00001 | 721.4 1891.9 388.1 | 7.2, 125.3 | 0.63 | 0.06% / 0.021% | 47x | 1.6% |
| power2 | c1 00002 | 710.8 1899.7 388.1 | 3.7, 129.3 | 0.63 | 0.05% / 0.027% | 42x | 1.1% |
| power2 | c1 00003 | 699.8 1907.8 388.1 | 0.0, 133.4 | 0.61 | 0.05% / 0.023% | 48x | 1.3% |
| power2 | c1 00004 | 689.2 1915.7 388.1 | 0.0, 136.7 | 0.61 | 0.06% / 0.017% | 46x | 1.3% |
| power2 | c1 00005 | 678.6 1923.5 388.1 | 0.0, 140.1 | 0.60 | 0.06% / 0.011% | 46x | 1.3% |
| power2 | c1 00006 | 667.7 1931.6 388.1 | 0.0, 143.5 | 0.60 | 0.05% / 0.007% | 37x | 1.4% |
| power2 | c1 00007 | 657.1 1939.4 388.1 | 0.0, 143.5 | 0.59 | 0.04% / 0.007% | 37x | 1.4% |
| power2 | c1 00008 | 646.5 1947.2 388.1 | 0.0, 143.5 | 0.53 | 0.03% / 0.007% | 32x | 1.4% |
| power2 | c1 00009 | 635.6 1955.3 388.1 | 0.0, 143.5 | 0.53 | 0.02% / 0.006% | 26x | 1.3% |
| power2 | c1 00010 | 624.9 1963.2 388.1 | 0.0, 143.5 | 0.51 | 0.02% / 0.006% | 25x | 1.1% |
| power2 | c1 00011 | 614.3 1971.0 388.1 | 0.0, 143.5 | 0.46 | 0.02% / 0.006% | 23x | 1.1% |
| city2 | c1 00000 | -895.9 224.1 -419.9 | 14.4, 57.0 | 0.73 | 0.00% / 0.000% | 3x | 0.8% |
| city2 | c1 00001 | -895.9 224.1 -419.9 | 14.4, 57.9 | 0.72 | 0.00% / 0.000% | 4x | 0.8% |
| city2 | c1 00002 | -895.9 224.1 -419.9 | 14.4, 58.9 | 0.72 | 0.00% / 0.000% | 4x | 0.8% |
| city2 | c1 00003 | -895.9 224.1 -419.9 | 14.4, 59.8 | 0.72 | 0.00% / 0.000% | 4x | 0.8% |
| city2 | c1 00004 | -895.9 224.1 -419.9 | 14.4, 60.8 | 0.72 | 0.00% / 0.000% | 4x | 0.8% |
| city2 | c1 00005 | -895.9 224.1 -419.9 | 14.4, 61.7 | 0.71 | 0.00% / 0.000% | 3x | 0.8% |
| city2 | c1 00006 | -895.9 224.1 -419.9 | 14.4, 62.6 | 0.68 | 0.01% / 0.004% | 20x | 0.6% |
| city2 | c1 00007 | -895.9 224.1 -419.9 | 14.4, 63.6 | 0.68 | 0.01% / 0.004% | 18x | 0.6% |
| city2 | c1 00008 | -895.9 224.1 -419.9 | 14.4, 64.5 | 0.67 | 0.00% / 0.000% | 3x | 0.8% |
| city2 | c1 00009 | -895.9 224.1 -419.9 | 14.4, 65.5 | 0.69 | 0.00% / 0.000% | 4x | 0.8% |
| city2 | c1 00010 | -895.9 224.1 -419.9 | 14.4, 66.4 | 0.69 | 0.00% / 0.000% | 3x | 0.8% |
| city2 | c1 00011 | -895.9 224.1 -419.9 | 14.4, 67.4 | 0.69 | 0.01% / 0.000% | 4x | 0.8% |
| jail3 | c1 00000 | -764.6 -1561.3 969.0 | -16.0, 55.2 | 0.48 | 0.01% / 0.001% | 6x | 0.6% |
| jail3 | c1 00001 | -760.2 -1552.5 970.2 | -14.0, 57.2 | 0.47 | 0.01% / 0.002% | 8x | 0.6% |
| jail3 | c1 00002 | -755.8 -1543.7 971.5 | -12.1, 59.2 | 0.47 | 0.01% / 0.004% | 9x | 0.6% |
| jail3 | c1 00003 | -751.4 -1534.9 972.7 | -10.5, 60.6 | 0.64 | 0.01% / 0.007% | 105x | 1.2% |
| jail3 | c1 00004 | -747.0 -1526.1 974.0 | -9.1, 61.8 | 0.65 | 0.01% / 0.008% | 77x | 0.5% |
| jail3 | c1 00005 | -742.6 -1517.3 975.3 | -7.8, 63.1 | 0.51 | 0.01% / 0.005% | 14x | 0.7% |
| jail3 | c1 00006 | -738.2 -1508.6 976.6 | -7.3, 63.4 | 0.51 | 0.01% / 0.006% | 16x | 0.7% |
| jail3 | c1 00007 | -733.8 -1499.8 977.9 | -7.3, 63.4 | 0.51 | 0.01% / 0.006% | 16x | 0.7% |
| jail3 | c1 00008 | -729.4 -1491.0 979.1 | -7.3, 63.4 | 0.51 | 0.01% / 0.006% | 16x | 0.7% |
| jail3 | c1 00009 | -725.0 -1482.2 980.4 | -7.3, 63.4 | 0.50 | 0.01% / 0.007% | 17x | 0.7% |
| jail3 | c1 00010 | -720.7 -1473.4 981.6 | -7.3, 63.4 | 0.50 | 0.02% / 0.007% | 17x | 0.7% |
| jail3 | c1 00011 | -716.3 -1464.6 982.9 | -7.3, 63.4 | 0.50 | 0.02% / 0.007% | 17x | 0.8% |
| mine3 | c1 00000 | 148.7 -2743.3 -438.5 | 40.0, 351.8 | 0.69 | 0.01% / 0.000% | 6x | 0.8% |
| mine3 | c1 00001 | 154.3 -2742.4 -446.6 | 40.0, 352.4 | 0.69 | 0.01% / 0.001% | 5x | 0.8% |
| mine3 | c1 00002 | 160.0 -2741.6 -454.6 | 40.0, 353.1 | 0.68 | 0.01% / 0.001% | 5x | 0.9% |
| mine3 | c1 00003 | 165.6 -2740.8 -462.7 | 40.0, 353.8 | 0.67 | 0.01% / 0.000% | 4x | 0.9% |
| mine3 | c1 00004 | 171.3 -2740.0 -470.8 | 40.0, 354.4 | 0.65 | 0.01% / 0.000% | 4x | 1.0% |
| mine3 | c1 00005 | 176.9 -2739.1 -478.9 | 40.0, 355.1 | 0.62 | 0.00% / 0.000% | 3x | 1.1% |
| mine3 | c1 00006 | 182.6 -2738.3 -486.9 | 40.0, 355.7 | 0.58 | 0.00% / 0.000% | 3x | 1.3% |
| mine3 | c1 00007 | 188.2 -2737.6 -495.0 | 40.0, 356.4 | 0.54 | 0.00% / 0.000% | 3x | 1.5% |
| mine3 | c1 00008 | 193.9 -2736.8 -503.1 | 40.0, 357.1 | 0.49 | 0.00% / 0.000% | 3x | 1.7% |
| mine3 | c1 00009 | 196.5 -2736.4 -506.9 | 39.5, 357.7 | 0.47 | 0.01% / 0.000% | 3x | 1.9% |
| mine3 | c1 00010 | 198.1 -2736.2 -509.1 | 38.9, 358.4 | 0.47 | 0.00% / 0.000% | 4x | 1.9% |
| mine3 | c1 00011 | 199.6 -2736.0 -511.3 | 38.2, 359.0 | 0.46 | 0.01% / 0.000% | 4x | 1.9% |
