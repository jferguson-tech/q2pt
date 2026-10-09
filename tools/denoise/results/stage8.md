## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.07 | 0.4185 | 43.07 |
| noisy 16 | 26.29 | 0.5904 | 30.03 |
| ours 4 | 37.75 | 0.9239 | 7.30 |
| ours 8 | 38.38 | 0.9267 | 7.03 |
| ours 16 | 38.91 | 0.9290 | 6.83 |
| ours 16, past only | 38.84 | 0.9284 | 6.89 |
| ours 16, alone | 37.09 | 0.9193 | 7.80 |
| OIDN 4 | 38.10 | 0.9271 | 7.62 |
| OIDN 16 | 38.95 | 0.9304 | 7.04 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.55 | 0.4048 | 46.98 |
| noisy 16 | 25.35 | 0.5704 | 33.78 |
| ours 4 | 33.17 | 0.8376 | 13.26 |
| ours 8 | 33.46 | 0.8394 | 12.91 |
| ours 16 | 33.63 | 0.8406 | 12.71 |
| ours 16, past only | 33.62 | 0.8404 | 12.73 |
| ours 16, alone | 32.99 | 0.8364 | 13.35 |
| OIDN 4 | 33.28 | 0.8395 | 13.29 |
| OIDN 16 | 33.67 | 0.8419 | 12.65 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 33.63 | 0.8406 | 12.71 |
| sharp frames denoised, then blurred | 27.66 | 0.8408 | 18.34 |
| sharp reference, then blurred | 27.74 | 0.8464 | 18.18 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.43 | 25.16 | 37.09 | 37.50 | 37.95 | 37.98 | 36.77 | 37.66 | 38.27 |
| 2 | 22.71 | 25.42 | 37.49 | 37.86 | 38.31 | 38.28 | 36.91 | 37.84 | 38.50 |
| 3 | 23.09 | 25.75 | 37.98 | 38.35 | 38.72 | 38.67 | 37.19 | 38.08 | 38.78 |
| 4 | 22.64 | 25.49 | 37.73 | 38.17 | 38.51 | 38.45 | 37.05 | 37.78 | 38.49 |
| 5 | 22.84 | 25.71 | 38.10 | 38.46 | 38.76 | 38.71 | 37.25 | 38.02 | 38.69 |
| 6 | 23.50 | 26.19 | 38.39 | 38.72 | 39.03 | 39.11 | 37.53 | 38.35 | 39.12 |
| 7 | 23.41 | 27.30 | 36.88 | 37.93 | 38.84 | 38.43 | 35.32 | 38.05 | 38.97 |
| 8 | 23.27 | 27.32 | 36.45 | 37.74 | 38.87 | 38.78 | 35.17 | 38.31 | 39.25 |
| 9 | 24.76 | 27.83 | 39.27 | 39.78 | 40.14 | 40.06 | 40.03 | 38.86 | 39.84 |
| 10 | 22.98 | 27.06 | 37.99 | 38.72 | 39.33 | 39.22 | 37.74 | 38.11 | 39.28 |
| 11 | 22.89 | 26.89 | 38.29 | 39.02 | 39.56 | 39.45 | 37.98 | 38.15 | 39.23 |
| 12 | 22.75 | 26.40 | 38.08 | 38.78 | 39.33 | 39.32 | 38.15 | 38.08 | 39.19 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 27.57 | 0.5997 | 29.02 |
| city2 c1 sharp | noisy 16 | 32.37 | 0.7835 | 16.28 |
| city2 c1 sharp | ours 4 | 44.59 | 0.9817 | 3.40 |
| city2 c1 sharp | ours 8 | 45.28 | 0.9825 | 3.28 |
| city2 c1 sharp | ours 16 | 45.75 | 0.9830 | 3.20 |
| city2 c1 sharp | ours 16, past only | 45.72 | 0.9829 | 3.22 |
| city2 c1 sharp | ours 16, alone | 45.55 | 0.9823 | 3.41 |
| city2 c1 sharp | OIDN 4 | 43.85 | 0.9800 | 3.69 |
| city2 c1 sharp | OIDN 16 | 45.12 | 0.9818 | 3.33 |
| city2 c1 blurred | noisy 4 | 27.45 | 0.5949 | 29.49 |
| city2 c1 blurred | noisy 16 | 31.99 | 0.7730 | 17.01 |
| city2 c1 blurred | ours 4 | 40.33 | 0.9504 | 5.93 |
| city2 c1 blurred | ours 8 | 40.57 | 0.9510 | 5.84 |
| city2 c1 blurred | ours 16 | 40.70 | 0.9515 | 5.78 |
| city2 c1 blurred | ours 16, past only | 40.69 | 0.9514 | 5.80 |
| city2 c1 blurred | ours 16, alone | 40.65 | 0.9510 | 5.91 |
| city2 c1 blurred | OIDN 4 | 40.11 | 0.9500 | 6.08 |
| city2 c1 blurred | OIDN 16 | 40.55 | 0.9512 | 5.83 |
| jail3 c1 sharp | noisy 4 | 27.26 | 0.4858 | 34.08 |
| jail3 c1 sharp | noisy 16 | 32.26 | 0.7236 | 19.20 |
| jail3 c1 sharp | ours 4 | 44.72 | 0.9812 | 3.87 |
| jail3 c1 sharp | ours 8 | 45.44 | 0.9820 | 3.70 |
| jail3 c1 sharp | ours 16 | 45.86 | 0.9825 | 3.59 |
| jail3 c1 sharp | ours 16, past only | 45.82 | 0.9824 | 3.61 |
| jail3 c1 sharp | ours 16, alone | 45.66 | 0.9820 | 3.77 |
| jail3 c1 sharp | OIDN 4 | 42.43 | 0.9742 | 4.99 |
| jail3 c1 sharp | OIDN 16 | 43.95 | 0.9774 | 4.39 |
| jail3 c1 blurred | noisy 4 | 26.58 | 0.4610 | 36.93 |
| jail3 c1 blurred | noisy 16 | 31.16 | 0.6933 | 21.33 |
| jail3 c1 blurred | ours 4 | 39.29 | 0.9365 | 7.63 |
| jail3 c1 blurred | ours 8 | 39.66 | 0.9375 | 7.38 |
| jail3 c1 blurred | ours 16 | 39.84 | 0.9381 | 7.28 |
| jail3 c1 blurred | ours 16, past only | 39.84 | 0.9380 | 7.29 |
| jail3 c1 blurred | ours 16, alone | 39.84 | 0.9378 | 7.36 |
| jail3 c1 blurred | OIDN 4 | 38.68 | 0.9324 | 8.33 |
| jail3 c1 blurred | OIDN 16 | 39.41 | 0.9353 | 7.72 |
| mine3 c1 sharp | noisy 4 | 26.46 | 0.5360 | 30.89 |
| mine3 c1 sharp | noisy 16 | 30.41 | 0.7240 | 19.36 |
| mine3 c1 sharp | ours 4 | 42.50 | 0.9811 | 3.35 |
| mine3 c1 sharp | ours 8 | 42.96 | 0.9819 | 3.22 |
| mine3 c1 sharp | ours 16 | 43.50 | 0.9825 | 3.12 |
| mine3 c1 sharp | ours 16, past only | 43.39 | 0.9822 | 3.20 |
| mine3 c1 sharp | ours 16, alone | 43.24 | 0.9810 | 3.58 |
| mine3 c1 sharp | OIDN 4 | 42.11 | 0.9801 | 3.77 |
| mine3 c1 sharp | OIDN 16 | 43.76 | 0.9823 | 3.29 |
| mine3 c1 blurred | noisy 4 | 26.39 | 0.5286 | 31.53 |
| mine3 c1 blurred | noisy 16 | 30.15 | 0.7147 | 20.03 |
| mine3 c1 blurred | ours 4 | 38.98 | 0.9548 | 5.72 |
| mine3 c1 blurred | ours 8 | 39.25 | 0.9556 | 5.62 |
| mine3 c1 blurred | ours 16 | 39.44 | 0.9562 | 5.54 |
| mine3 c1 blurred | ours 16, past only | 39.42 | 0.9560 | 5.57 |
| mine3 c1 blurred | ours 16, alone | 39.37 | 0.9550 | 5.81 |
| mine3 c1 blurred | OIDN 4 | 38.79 | 0.9549 | 5.95 |
| mine3 c1 blurred | OIDN 16 | 39.45 | 0.9565 | 5.60 |
| power2 c1 sharp | noisy 4 | 21.59 | 0.2892 | 59.31 |
| power2 c1 sharp | noisy 16 | 25.11 | 0.4439 | 40.73 |
| power2 c1 sharp | ours 4 | 37.85 | 0.9194 | 8.29 |
| power2 c1 sharp | ours 8 | 38.37 | 0.9223 | 8.08 |
| power2 c1 sharp | ours 16 | 38.85 | 0.9249 | 7.93 |
| power2 c1 sharp | ours 16, past only | 38.68 | 0.9237 | 8.08 |
| power2 c1 sharp | ours 16, alone | 38.45 | 0.9213 | 8.77 |
| power2 c1 sharp | OIDN 4 | 37.19 | 0.9186 | 9.21 |
| power2 c1 sharp | OIDN 16 | 38.61 | 0.9251 | 8.43 |
| power2 c1 blurred | noisy 4 | 21.46 | 0.2742 | 61.31 |
| power2 c1 blurred | noisy 16 | 24.82 | 0.4168 | 43.14 |
| power2 c1 blurred | ours 4 | 33.96 | 0.8074 | 14.83 |
| power2 c1 blurred | ours 8 | 34.22 | 0.8095 | 14.59 |
| power2 c1 blurred | ours 16 | 34.38 | 0.8113 | 14.42 |
| power2 c1 blurred | ours 16, past only | 34.38 | 0.8111 | 14.44 |
| power2 c1 blurred | ours 16, alone | 34.35 | 0.8102 | 14.76 |
| power2 c1 blurred | OIDN 4 | 33.92 | 0.8099 | 15.16 |
| power2 c1 blurred | OIDN 16 | 34.42 | 0.8135 | 14.44 |
| q2dm4 c1 sharp | noisy 4 | 25.10 | 0.4118 | 40.96 |
| q2dm4 c1 sharp | noisy 16 | 29.05 | 0.6094 | 25.09 |
| q2dm4 c1 sharp | ours 4 | 39.95 | 0.9435 | 6.39 |
| q2dm4 c1 sharp | ours 8 | 40.26 | 0.9448 | 6.15 |
| q2dm4 c1 sharp | ours 16 | 40.59 | 0.9459 | 5.97 |
| q2dm4 c1 sharp | ours 16, past only | 40.56 | 0.9457 | 6.03 |
| q2dm4 c1 sharp | ours 16, alone | 40.53 | 0.9452 | 6.20 |
| q2dm4 c1 sharp | OIDN 4 | 40.17 | 0.9452 | 6.47 |
| q2dm4 c1 sharp | OIDN 16 | 40.89 | 0.9477 | 5.98 |
| q2dm4 c1 blurred | noisy 4 | 24.79 | 0.3947 | 43.13 |
| q2dm4 c1 blurred | noisy 16 | 28.52 | 0.5842 | 26.96 |
| q2dm4 c1 blurred | ours 4 | 36.08 | 0.8747 | 10.14 |
| q2dm4 c1 blurred | ours 8 | 36.26 | 0.8758 | 9.90 |
| q2dm4 c1 blurred | ours 16 | 36.40 | 0.8767 | 9.72 |
| q2dm4 c1 blurred | ours 16, past only | 36.39 | 0.8766 | 9.75 |
| q2dm4 c1 blurred | ours 16, alone | 36.39 | 0.8764 | 9.82 |
| q2dm4 c1 blurred | OIDN 4 | 36.06 | 0.8756 | 10.23 |
| q2dm4 c1 blurred | OIDN 16 | 36.44 | 0.8775 | 9.72 |
| ware2 c1 sharp | noisy 4 | 18.64 | 0.1888 | 64.19 |
| ware2 c1 sharp | noisy 16 | 21.15 | 0.2578 | 59.50 |
| ware2 c1 sharp | ours 4 | 32.10 | 0.7367 | 18.52 |
| ware2 c1 sharp | ours 8 | 32.81 | 0.7469 | 17.75 |
| ware2 c1 sharp | ours 16 | 33.40 | 0.7550 | 17.15 |
| ware2 c1 sharp | ours 16, past only | 33.35 | 0.7536 | 17.23 |
| ware2 c1 sharp | ours 16, alone | 30.77 | 0.7038 | 21.07 |
| ware2 c1 sharp | OIDN 4 | 33.16 | 0.7641 | 17.57 |
| ware2 c1 sharp | OIDN 16 | 33.64 | 0.7679 | 16.84 |
| ware2 c1 blurred | noisy 4 | 17.81 | 0.1751 | 79.51 |
| ware2 c1 blurred | noisy 16 | 19.82 | 0.2405 | 74.18 |
| ware2 c1 blurred | ours 4 | 27.20 | 0.5019 | 35.33 |
| ware2 c1 blurred | ours 8 | 27.51 | 0.5067 | 34.16 |
| ware2 c1 blurred | ours 16 | 27.68 | 0.5101 | 33.50 |
| ware2 c1 blurred | ours 16, past only | 27.66 | 0.5095 | 33.53 |
| ware2 c1 blurred | ours 16, alone | 26.76 | 0.4880 | 36.48 |
| ware2 c1 blurred | OIDN 4 | 27.45 | 0.5143 | 33.99 |
| ware2 c1 blurred | OIDN 16 | 27.76 | 0.5171 | 32.56 |
