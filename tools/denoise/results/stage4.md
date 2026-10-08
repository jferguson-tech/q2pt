## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.07 | 0.4185 | 43.07 |
| noisy 16 | 26.29 | 0.5904 | 30.03 |
| ours 4 | 37.64 | 0.9241 | 7.32 |
| ours 8 | 38.23 | 0.9263 | 7.08 |
| ours 16 | 38.79 | 0.9284 | 6.88 |
| ours 16, past only | 38.72 | 0.9277 | 6.96 |
| ours 16, alone | 36.62 | 0.9169 | 7.98 |
| OIDN 4 | 38.10 | 0.9271 | 7.62 |
| OIDN 16 | 38.95 | 0.9304 | 7.04 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.55 | 0.4048 | 46.98 |
| noisy 16 | 25.35 | 0.5704 | 33.78 |
| ours 4 | 33.12 | 0.8375 | 13.28 |
| ours 8 | 33.39 | 0.8390 | 12.95 |
| ours 16 | 33.57 | 0.8403 | 12.74 |
| ours 16, past only | 33.55 | 0.8400 | 12.78 |
| ours 16, alone | 32.79 | 0.8352 | 13.50 |
| OIDN 4 | 33.28 | 0.8395 | 13.29 |
| OIDN 16 | 33.67 | 0.8419 | 12.65 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 33.57 | 0.8403 | 12.74 |
| sharp frames denoised, then blurred | 27.66 | 0.8406 | 18.37 |
| sharp reference, then blurred | 27.74 | 0.8464 | 18.18 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.43 | 25.16 | 36.99 | 37.46 | 38.07 | 38.04 | 36.43 | 37.66 | 38.27 |
| 2 | 22.71 | 25.42 | 37.59 | 37.97 | 38.44 | 38.40 | 36.56 | 37.84 | 38.50 |
| 3 | 23.09 | 25.75 | 38.10 | 38.45 | 38.79 | 38.73 | 36.83 | 38.08 | 38.78 |
| 4 | 22.64 | 25.49 | 37.84 | 38.24 | 38.53 | 38.47 | 36.71 | 37.78 | 38.49 |
| 5 | 22.84 | 25.71 | 38.16 | 38.49 | 38.77 | 38.70 | 36.89 | 38.02 | 38.69 |
| 6 | 23.50 | 26.19 | 38.31 | 38.64 | 38.88 | 39.08 | 37.17 | 38.35 | 39.12 |
| 7 | 23.41 | 27.30 | 36.26 | 37.28 | 38.35 | 37.90 | 34.60 | 38.05 | 38.97 |
| 8 | 23.27 | 27.32 | 35.76 | 36.96 | 38.19 | 38.24 | 34.34 | 38.31 | 39.25 |
| 9 | 24.76 | 27.83 | 39.14 | 39.62 | 39.94 | 39.90 | 39.69 | 38.86 | 39.84 |
| 10 | 22.98 | 27.06 | 38.09 | 38.66 | 39.20 | 39.08 | 37.31 | 38.11 | 39.28 |
| 11 | 22.89 | 26.89 | 38.42 | 38.97 | 39.45 | 39.33 | 37.59 | 38.15 | 39.23 |
| 12 | 22.75 | 26.40 | 38.26 | 38.73 | 39.21 | 39.21 | 37.79 | 38.08 | 39.19 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 27.57 | 0.5997 | 29.02 |
| city2 c1 sharp | noisy 16 | 32.37 | 0.7835 | 16.28 |
| city2 c1 sharp | ours 4 | 44.68 | 0.9817 | 3.37 |
| city2 c1 sharp | ours 8 | 45.34 | 0.9825 | 3.25 |
| city2 c1 sharp | ours 16 | 45.79 | 0.9830 | 3.17 |
| city2 c1 sharp | ours 16, past only | 45.77 | 0.9829 | 3.19 |
| city2 c1 sharp | ours 16, alone | 45.53 | 0.9822 | 3.43 |
| city2 c1 sharp | OIDN 4 | 43.85 | 0.9800 | 3.69 |
| city2 c1 sharp | OIDN 16 | 45.12 | 0.9818 | 3.33 |
| city2 c1 blurred | noisy 4 | 27.45 | 0.5949 | 29.49 |
| city2 c1 blurred | noisy 16 | 31.99 | 0.7730 | 17.01 |
| city2 c1 blurred | ours 4 | 40.36 | 0.9503 | 5.91 |
| city2 c1 blurred | ours 8 | 40.57 | 0.9510 | 5.83 |
| city2 c1 blurred | ours 16 | 40.70 | 0.9515 | 5.77 |
| city2 c1 blurred | ours 16, past only | 40.70 | 0.9514 | 5.78 |
| city2 c1 blurred | ours 16, alone | 40.63 | 0.9508 | 5.93 |
| city2 c1 blurred | OIDN 4 | 40.11 | 0.9500 | 6.08 |
| city2 c1 blurred | OIDN 16 | 40.55 | 0.9512 | 5.83 |
| jail3 c1 sharp | noisy 4 | 27.26 | 0.4858 | 34.08 |
| jail3 c1 sharp | noisy 16 | 32.26 | 0.7236 | 19.20 |
| jail3 c1 sharp | ours 4 | 44.88 | 0.9812 | 3.84 |
| jail3 c1 sharp | ours 8 | 45.56 | 0.9819 | 3.68 |
| jail3 c1 sharp | ours 16 | 45.90 | 0.9824 | 3.58 |
| jail3 c1 sharp | ours 16, past only | 45.87 | 0.9823 | 3.60 |
| jail3 c1 sharp | ours 16, alone | 45.71 | 0.9818 | 3.79 |
| jail3 c1 sharp | OIDN 4 | 42.43 | 0.9742 | 4.99 |
| jail3 c1 sharp | OIDN 16 | 43.95 | 0.9774 | 4.39 |
| jail3 c1 blurred | noisy 4 | 26.58 | 0.4610 | 36.93 |
| jail3 c1 blurred | noisy 16 | 31.16 | 0.6933 | 21.33 |
| jail3 c1 blurred | ours 4 | 39.34 | 0.9365 | 7.63 |
| jail3 c1 blurred | ours 8 | 39.69 | 0.9375 | 7.37 |
| jail3 c1 blurred | ours 16 | 39.84 | 0.9380 | 7.28 |
| jail3 c1 blurred | ours 16, past only | 39.84 | 0.9379 | 7.29 |
| jail3 c1 blurred | ours 16, alone | 39.83 | 0.9377 | 7.37 |
| jail3 c1 blurred | OIDN 4 | 38.68 | 0.9324 | 8.33 |
| jail3 c1 blurred | OIDN 16 | 39.41 | 0.9353 | 7.72 |
| mine3 c1 sharp | noisy 4 | 26.46 | 0.5360 | 30.89 |
| mine3 c1 sharp | noisy 16 | 30.41 | 0.7240 | 19.36 |
| mine3 c1 sharp | ours 4 | 42.60 | 0.9810 | 3.35 |
| mine3 c1 sharp | ours 8 | 43.16 | 0.9818 | 3.22 |
| mine3 c1 sharp | ours 16 | 43.67 | 0.9824 | 3.13 |
| mine3 c1 sharp | ours 16, past only | 43.57 | 0.9821 | 3.21 |
| mine3 c1 sharp | ours 16, alone | 43.31 | 0.9806 | 3.64 |
| mine3 c1 sharp | OIDN 4 | 42.11 | 0.9801 | 3.77 |
| mine3 c1 sharp | OIDN 16 | 43.76 | 0.9823 | 3.29 |
| mine3 c1 blurred | noisy 4 | 26.39 | 0.5286 | 31.53 |
| mine3 c1 blurred | noisy 16 | 30.15 | 0.7147 | 20.03 |
| mine3 c1 blurred | ours 4 | 39.00 | 0.9547 | 5.72 |
| mine3 c1 blurred | ours 8 | 39.30 | 0.9556 | 5.61 |
| mine3 c1 blurred | ours 16 | 39.48 | 0.9561 | 5.53 |
| mine3 c1 blurred | ours 16, past only | 39.46 | 0.9559 | 5.57 |
| mine3 c1 blurred | ours 16, alone | 39.36 | 0.9546 | 5.85 |
| mine3 c1 blurred | OIDN 4 | 38.79 | 0.9549 | 5.95 |
| mine3 c1 blurred | OIDN 16 | 39.45 | 0.9565 | 5.60 |
| power2 c1 sharp | noisy 4 | 21.59 | 0.2892 | 59.31 |
| power2 c1 sharp | noisy 16 | 25.11 | 0.4439 | 40.73 |
| power2 c1 sharp | ours 4 | 37.87 | 0.9191 | 8.34 |
| power2 c1 sharp | ours 8 | 38.39 | 0.9220 | 8.12 |
| power2 c1 sharp | ours 16 | 38.90 | 0.9247 | 7.95 |
| power2 c1 sharp | ours 16, past only | 38.72 | 0.9234 | 8.12 |
| power2 c1 sharp | ours 16, alone | 38.49 | 0.9209 | 8.78 |
| power2 c1 sharp | OIDN 4 | 37.19 | 0.9186 | 9.21 |
| power2 c1 sharp | OIDN 16 | 38.61 | 0.9251 | 8.43 |
| power2 c1 blurred | noisy 4 | 21.46 | 0.2742 | 61.31 |
| power2 c1 blurred | noisy 16 | 24.82 | 0.4168 | 43.14 |
| power2 c1 blurred | ours 4 | 33.97 | 0.8073 | 14.83 |
| power2 c1 blurred | ours 8 | 34.23 | 0.8094 | 14.58 |
| power2 c1 blurred | ours 16 | 34.40 | 0.8113 | 14.40 |
| power2 c1 blurred | ours 16, past only | 34.39 | 0.8110 | 14.43 |
| power2 c1 blurred | ours 16, alone | 34.36 | 0.8099 | 14.77 |
| power2 c1 blurred | OIDN 4 | 33.92 | 0.8099 | 15.16 |
| power2 c1 blurred | OIDN 16 | 34.42 | 0.8135 | 14.44 |
| q2dm4 c1 sharp | noisy 4 | 25.10 | 0.4118 | 40.96 |
| q2dm4 c1 sharp | noisy 16 | 29.05 | 0.6094 | 25.09 |
| q2dm4 c1 sharp | ours 4 | 39.98 | 0.9432 | 6.39 |
| q2dm4 c1 sharp | ours 8 | 40.33 | 0.9446 | 6.17 |
| q2dm4 c1 sharp | ours 16 | 40.65 | 0.9457 | 5.99 |
| q2dm4 c1 sharp | ours 16, past only | 40.61 | 0.9455 | 6.05 |
| q2dm4 c1 sharp | ours 16, alone | 40.57 | 0.9450 | 6.24 |
| q2dm4 c1 sharp | OIDN 4 | 40.17 | 0.9452 | 6.47 |
| q2dm4 c1 sharp | OIDN 16 | 40.89 | 0.9477 | 5.98 |
| q2dm4 c1 blurred | noisy 4 | 24.79 | 0.3947 | 43.13 |
| q2dm4 c1 blurred | noisy 16 | 28.52 | 0.5842 | 26.96 |
| q2dm4 c1 blurred | ours 4 | 36.08 | 0.8746 | 10.15 |
| q2dm4 c1 blurred | ours 8 | 36.28 | 0.8758 | 9.90 |
| q2dm4 c1 blurred | ours 16 | 36.42 | 0.8767 | 9.72 |
| q2dm4 c1 blurred | ours 16, past only | 36.41 | 0.8765 | 9.76 |
| q2dm4 c1 blurred | ours 16, alone | 36.40 | 0.8764 | 9.84 |
| q2dm4 c1 blurred | OIDN 4 | 36.06 | 0.8756 | 10.23 |
| q2dm4 c1 blurred | OIDN 16 | 36.44 | 0.8775 | 9.72 |
| ware2 c1 sharp | noisy 4 | 18.64 | 0.1888 | 64.19 |
| ware2 c1 sharp | noisy 16 | 21.15 | 0.2578 | 59.50 |
| ware2 c1 sharp | ours 4 | 31.88 | 0.7382 | 18.63 |
| ware2 c1 sharp | ours 8 | 32.52 | 0.7451 | 18.01 |
| ware2 c1 sharp | ours 16 | 33.16 | 0.7524 | 17.46 |
| ware2 c1 sharp | ours 16, past only | 33.12 | 0.7499 | 17.60 |
| ware2 c1 sharp | ours 16, alone | 30.10 | 0.6907 | 22.00 |
| ware2 c1 sharp | OIDN 4 | 33.16 | 0.7641 | 17.57 |
| ware2 c1 sharp | OIDN 16 | 33.64 | 0.7679 | 16.84 |
| ware2 c1 blurred | noisy 4 | 17.81 | 0.1751 | 79.51 |
| ware2 c1 blurred | noisy 16 | 19.82 | 0.2405 | 74.18 |
| ware2 c1 blurred | ours 4 | 27.11 | 0.5013 | 35.43 |
| ware2 c1 blurred | ours 8 | 27.39 | 0.5050 | 34.41 |
| ware2 c1 blurred | ours 16 | 27.59 | 0.5085 | 33.75 |
| ware2 c1 blurred | ours 16, past only | 27.56 | 0.5072 | 33.87 |
| ware2 c1 blurred | ours 16, alone | 26.47 | 0.4818 | 37.24 |
| ware2 c1 blurred | OIDN 4 | 27.45 | 0.5143 | 33.99 |
| ware2 c1 blurred | OIDN 16 | 27.76 | 0.5171 | 32.56 |
