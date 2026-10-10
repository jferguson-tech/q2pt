## Multi-scale SSIM in the loss: four probes

Each 3,000 steps from the tenth run's weights at `--lr 1e-4`, otherwise as
the tenth run, with `--ssim W` (1 - MS-SSIM of the picture as shown, added
to the loss with weight W). Six held-out clips, sharp frames, references of
16,384 paths.

| | PSNR 4 / 8 / 16 | SSIM 4 / 16 | flicker 4 / 16 | ware2 4: PSNR, SSIM | q2dm4 16: PSNR, SSIM |
|---|---|---|---|---|---|
| tenth run | 39.47 / 40.14 / 40.69 | 0.9422 / 0.9473 | 6.21 / 5.72 | 35.58, 0.8574 | 37.71, 0.8867 |
| `--ssim 0.1` | 39.50 / 40.15 / 40.68 | 0.9425 / 0.9473 | 6.20 / 5.72 | 35.65, 0.8584 | 37.70, 0.8868 |
| `--ssim 0.5` | 39.45 / 40.12 / 40.65 | 0.9426 / 0.9473 | 6.17 / 5.72 | 35.67, 0.8588 | 37.65, 0.8865 |
| `--ssim 3` | 39.40 / 40.04 / 40.55 | 0.9426 / 0.9465 | 6.19 / 5.78 | 35.68, 0.8591 | 37.51, 0.8843 |
| `--ssim 1 --ssim-even` | 39.43 / 40.07 / 40.57 | 0.9426 / 0.9467 | 6.18 / 5.76 | 35.68, 0.8594 | 37.54, 0.8843 |
| Open Image Denoise | 38.91 / - / 40.14 | 0.9433 / 0.9471 | 6.77 / 6.13 | 35.31, 0.8745 | 37.67, 0.8876 |

None closes the gap in SSIM at 4 paths; the two heavier ones lose PSNR and
lose SSIM at 16 paths.

## Where the gap in SSIM is (tenth run, ware2 and q2dm4)

SSIM at 4 paths with one of the three denoised lights replaced by the
reference's:

| | ware2 | q2dm4 |
|---|---|---|
| as denoised | 0.8574 | 0.8835 |
| true diffuse | 0.8625 | 0.8840 |
| true mirrored | 0.8774 | 0.8863 |
| true layers | 0.9748 | 0.9963 |

Surface colour does not pass through the network, and there is no filter
after it. Nearly all that is missing is in the layers (fog, glows, beams).

The reference's layers are themselves still noisy. The finest detail
(the light less itself blurred by 1 px) of two halves of 8 paths of one
frame agrees with 94% of that of the reference in the diffuse light of
q2dm4, and with none of it in the layers; nor does it carry from a frame to
the next. The reference of q2dm4 with only its layers blurred by 2 px scores
SSIM 0.8950 and 38.24 dB against itself, which is about where both
denoisers are.

In ware2 the gap is in the six frames lit by the BFG (0.84-0.88 against
0.89-0.90; the six dark frames are within 0.008). Blurring the denoised
layers and mirrored light afterwards, at 4 paths:

| | ware2 SSIM, PSNR | q2dm4 SSIM, PSNR |
|---|---|---|
| as denoised | 0.8574, 35.58 | 0.8835, 37.15 |
| layers by 0.7 px | 0.8660, 35.64 | 0.8850, 37.19 |
| layers and mirrored by 0.7 px | 0.8687, 35.73 | 0.8848, 37.17 |
| layers and mirrored by 1.5 px | 0.8736, 35.35 | 0.8832, 36.96 |
| Open Image Denoise | 0.8745, 35.31 | 0.8847, 37.10 |

So the network leaves fine grain in those lights that Open Image Denoise
smooths away: it keeps 43% of the reference's finest detail in ware2 where
Open Image Denoise keeps 24%, but what it keeps follows the reference less
well (structure term 0.915 against 0.951).
