## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.07 | 0.4185 | 43.07 |
| noisy 16 | 26.29 | 0.5904 | 30.03 |
| ours 4 | 36.43 | 0.9206 | 7.26 |
| ours 8 | 36.64 | 0.9219 | 7.06 |
| ours 16 | 36.85 | 0.9225 | 6.92 |
| ours 16, past only | 36.78 | 0.9216 | 7.00 |
| ours 16, alone | 36.20 | 0.9166 | 7.67 |
| OIDN 4 | 38.10 | 0.9271 | 7.62 |
| OIDN 16 | 38.95 | 0.9304 | 7.04 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.55 | 0.4048 | 46.98 |
| noisy 16 | 25.35 | 0.5704 | 33.78 |
| ours 4 | 32.75 | 0.8369 | 13.17 |
| ours 8 | 32.91 | 0.8382 | 12.86 |
| ours 16 | 33.00 | 0.8388 | 12.69 |
| ours 16, past only | 32.98 | 0.8385 | 12.72 |
| ours 16, alone | 32.68 | 0.8357 | 13.19 |
| OIDN 4 | 33.28 | 0.8395 | 13.29 |
| OIDN 16 | 33.67 | 0.8419 | 12.65 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 33.00 | 0.8388 | 12.69 |
| sharp frames denoised, then blurred | 27.41 | 0.8383 | 18.43 |
| sharp reference, then blurred | 27.74 | 0.8464 | 18.18 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.43 | 25.16 | 35.25 | 35.36 | 35.55 | 35.54 | 35.23 | 37.66 | 38.27 |
| 2 | 22.71 | 25.42 | 35.59 | 35.52 | 35.60 | 35.57 | 35.46 | 37.84 | 38.50 |
| 3 | 23.09 | 25.75 | 36.04 | 35.94 | 35.90 | 35.88 | 35.79 | 38.08 | 38.78 |
| 4 | 22.64 | 25.49 | 35.74 | 35.74 | 35.67 | 35.67 | 35.59 | 37.78 | 38.49 |
| 5 | 22.84 | 25.71 | 36.24 | 36.15 | 36.03 | 36.04 | 35.93 | 38.02 | 38.69 |
| 6 | 23.50 | 26.19 | 36.77 | 36.74 | 36.62 | 36.45 | 36.35 | 38.35 | 39.12 |
| 7 | 23.41 | 27.30 | 36.18 | 36.97 | 37.75 | 37.31 | 35.40 | 38.05 | 38.97 |
| 8 | 23.27 | 27.32 | 35.54 | 36.51 | 37.52 | 37.63 | 35.25 | 38.31 | 39.25 |
| 9 | 24.76 | 27.83 | 38.16 | 38.38 | 38.62 | 38.37 | 38.30 | 38.86 | 39.84 |
| 10 | 22.98 | 27.06 | 37.38 | 37.65 | 38.09 | 37.96 | 37.35 | 38.11 | 39.28 |
| 11 | 22.89 | 26.89 | 37.74 | 38.07 | 38.45 | 38.35 | 37.50 | 38.15 | 39.23 |
| 12 | 22.75 | 26.40 | 37.71 | 37.97 | 38.26 | 38.27 | 37.58 | 38.08 | 39.19 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 27.57 | 0.5997 | 29.02 |
| city2 c1 sharp | noisy 16 | 32.37 | 0.7835 | 16.28 |
| city2 c1 sharp | ours 4 | 43.17 | 0.9817 | 3.37 |
| city2 c1 sharp | ours 8 | 43.55 | 0.9823 | 3.26 |
| city2 c1 sharp | ours 16 | 44.04 | 0.9827 | 3.20 |
| city2 c1 sharp | ours 16, past only | 44.02 | 0.9826 | 3.22 |
| city2 c1 sharp | ours 16, alone | 43.85 | 0.9820 | 3.42 |
| city2 c1 sharp | OIDN 4 | 43.85 | 0.9800 | 3.69 |
| city2 c1 sharp | OIDN 16 | 45.12 | 0.9818 | 3.33 |
| city2 c1 blurred | noisy 4 | 27.45 | 0.5949 | 29.49 |
| city2 c1 blurred | noisy 16 | 31.99 | 0.7730 | 17.01 |
| city2 c1 blurred | ours 4 | 39.80 | 0.9504 | 5.90 |
| city2 c1 blurred | ours 8 | 39.98 | 0.9509 | 5.83 |
| city2 c1 blurred | ours 16 | 40.17 | 0.9513 | 5.78 |
| city2 c1 blurred | ours 16, past only | 40.16 | 0.9512 | 5.79 |
| city2 c1 blurred | ours 16, alone | 40.10 | 0.9508 | 5.91 |
| city2 c1 blurred | OIDN 4 | 40.11 | 0.9500 | 6.08 |
| city2 c1 blurred | OIDN 16 | 40.55 | 0.9512 | 5.83 |
| jail3 c1 sharp | noisy 4 | 27.26 | 0.4858 | 34.08 |
| jail3 c1 sharp | noisy 16 | 32.26 | 0.7236 | 19.20 |
| jail3 c1 sharp | ours 4 | 43.13 | 0.9810 | 3.96 |
| jail3 c1 sharp | ours 8 | 43.56 | 0.9816 | 3.81 |
| jail3 c1 sharp | ours 16 | 44.04 | 0.9821 | 3.70 |
| jail3 c1 sharp | ours 16, past only | 44.00 | 0.9820 | 3.72 |
| jail3 c1 sharp | ours 16, alone | 43.81 | 0.9815 | 3.89 |
| jail3 c1 sharp | OIDN 4 | 42.43 | 0.9742 | 4.99 |
| jail3 c1 sharp | OIDN 16 | 43.95 | 0.9774 | 4.39 |
| jail3 c1 blurred | noisy 4 | 26.58 | 0.4610 | 36.93 |
| jail3 c1 blurred | noisy 16 | 31.16 | 0.6933 | 21.33 |
| jail3 c1 blurred | ours 4 | 38.83 | 0.9365 | 7.64 |
| jail3 c1 blurred | ours 8 | 38.98 | 0.9373 | 7.45 |
| jail3 c1 blurred | ours 16 | 39.22 | 0.9378 | 7.35 |
| jail3 c1 blurred | ours 16, past only | 39.21 | 0.9378 | 7.36 |
| jail3 c1 blurred | ours 16, alone | 39.18 | 0.9374 | 7.44 |
| jail3 c1 blurred | OIDN 4 | 38.68 | 0.9324 | 8.33 |
| jail3 c1 blurred | OIDN 16 | 39.41 | 0.9353 | 7.72 |
| mine3 c1 sharp | noisy 4 | 26.46 | 0.5360 | 30.89 |
| mine3 c1 sharp | noisy 16 | 30.41 | 0.7240 | 19.36 |
| mine3 c1 sharp | ours 4 | 40.98 | 0.9809 | 3.35 |
| mine3 c1 sharp | ours 8 | 40.86 | 0.9815 | 3.25 |
| mine3 c1 sharp | ours 16 | 41.27 | 0.9819 | 3.16 |
| mine3 c1 sharp | ours 16, past only | 41.19 | 0.9816 | 3.24 |
| mine3 c1 sharp | ours 16, alone | 41.12 | 0.9804 | 3.62 |
| mine3 c1 sharp | OIDN 4 | 42.11 | 0.9801 | 3.77 |
| mine3 c1 sharp | OIDN 16 | 43.76 | 0.9823 | 3.29 |
| mine3 c1 blurred | noisy 4 | 26.39 | 0.5286 | 31.53 |
| mine3 c1 blurred | noisy 16 | 30.15 | 0.7147 | 20.03 |
| mine3 c1 blurred | ours 4 | 38.42 | 0.9547 | 5.70 |
| mine3 c1 blurred | ours 8 | 38.44 | 0.9554 | 5.61 |
| mine3 c1 blurred | ours 16 | 38.62 | 0.9558 | 5.54 |
| mine3 c1 blurred | ours 16, past only | 38.61 | 0.9556 | 5.58 |
| mine3 c1 blurred | ours 16, alone | 38.58 | 0.9546 | 5.81 |
| mine3 c1 blurred | OIDN 4 | 38.79 | 0.9549 | 5.95 |
| mine3 c1 blurred | OIDN 16 | 39.45 | 0.9565 | 5.60 |
| power2 c1 sharp | noisy 4 | 21.59 | 0.2892 | 59.31 |
| power2 c1 sharp | noisy 16 | 25.11 | 0.4439 | 40.73 |
| power2 c1 sharp | ours 4 | 36.55 | 0.9177 | 8.33 |
| power2 c1 sharp | ours 8 | 36.79 | 0.9202 | 8.14 |
| power2 c1 sharp | ours 16 | 37.18 | 0.9225 | 8.00 |
| power2 c1 sharp | ours 16, past only | 37.08 | 0.9213 | 8.15 |
| power2 c1 sharp | ours 16, alone | 36.91 | 0.9188 | 8.82 |
| power2 c1 sharp | OIDN 4 | 37.19 | 0.9186 | 9.21 |
| power2 c1 sharp | OIDN 16 | 38.61 | 0.9251 | 8.43 |
| power2 c1 blurred | noisy 4 | 21.46 | 0.2742 | 61.31 |
| power2 c1 blurred | noisy 16 | 24.82 | 0.4168 | 43.14 |
| power2 c1 blurred | ours 4 | 33.46 | 0.8065 | 14.81 |
| power2 c1 blurred | ours 8 | 33.64 | 0.8083 | 14.58 |
| power2 c1 blurred | ours 16 | 33.81 | 0.8100 | 14.43 |
| power2 c1 blurred | ours 16, past only | 33.81 | 0.8097 | 14.45 |
| power2 c1 blurred | ours 16, alone | 33.78 | 0.8087 | 14.76 |
| power2 c1 blurred | OIDN 4 | 33.92 | 0.8099 | 15.16 |
| power2 c1 blurred | OIDN 16 | 34.42 | 0.8135 | 14.44 |
| q2dm4 c1 sharp | noisy 4 | 25.10 | 0.4118 | 40.96 |
| q2dm4 c1 sharp | noisy 16 | 29.05 | 0.6094 | 25.09 |
| q2dm4 c1 sharp | ours 4 | 39.18 | 0.9434 | 6.37 |
| q2dm4 c1 sharp | ours 8 | 39.36 | 0.9444 | 6.15 |
| q2dm4 c1 sharp | ours 16 | 39.71 | 0.9454 | 6.00 |
| q2dm4 c1 sharp | ours 16, past only | 39.67 | 0.9452 | 6.05 |
| q2dm4 c1 sharp | ours 16, alone | 39.67 | 0.9447 | 6.24 |
| q2dm4 c1 sharp | OIDN 4 | 40.17 | 0.9452 | 6.47 |
| q2dm4 c1 sharp | OIDN 16 | 40.89 | 0.9477 | 5.98 |
| q2dm4 c1 blurred | noisy 4 | 24.79 | 0.3947 | 43.13 |
| q2dm4 c1 blurred | noisy 16 | 28.52 | 0.5842 | 26.96 |
| q2dm4 c1 blurred | ours 4 | 35.85 | 0.8747 | 10.15 |
| q2dm4 c1 blurred | ours 8 | 35.98 | 0.8756 | 9.92 |
| q2dm4 c1 blurred | ours 16 | 36.11 | 0.8764 | 9.74 |
| q2dm4 c1 blurred | ours 16, past only | 36.09 | 0.8763 | 9.77 |
| q2dm4 c1 blurred | ours 16, alone | 36.11 | 0.8761 | 9.85 |
| q2dm4 c1 blurred | OIDN 4 | 36.06 | 0.8756 | 10.23 |
| q2dm4 c1 blurred | OIDN 16 | 36.44 | 0.8775 | 9.72 |
| ware2 c1 sharp | noisy 4 | 18.64 | 0.1888 | 64.19 |
| ware2 c1 sharp | noisy 16 | 21.15 | 0.2578 | 59.50 |
| ware2 c1 sharp | ours 4 | 30.72 | 0.7190 | 18.22 |
| ware2 c1 sharp | ours 8 | 30.93 | 0.7212 | 17.77 |
| ware2 c1 sharp | ours 16 | 31.04 | 0.7202 | 17.48 |
| ware2 c1 sharp | ours 16, past only | 30.97 | 0.7169 | 17.64 |
| ware2 c1 sharp | ours 16, alone | 30.14 | 0.6920 | 20.05 |
| ware2 c1 sharp | OIDN 4 | 33.16 | 0.7641 | 17.57 |
| ware2 c1 sharp | OIDN 16 | 33.64 | 0.7679 | 16.84 |
| ware2 c1 blurred | noisy 4 | 17.81 | 0.1751 | 79.51 |
| ware2 c1 blurred | noisy 16 | 19.82 | 0.2405 | 74.18 |
| ware2 c1 blurred | ours 4 | 26.79 | 0.4989 | 34.80 |
| ware2 c1 blurred | ours 8 | 26.96 | 0.5015 | 33.80 |
| ware2 c1 blurred | ours 16 | 27.00 | 0.5017 | 33.33 |
| ware2 c1 blurred | ours 16, past only | 26.97 | 0.5003 | 33.39 |
| ware2 c1 blurred | ours 16, alone | 26.54 | 0.4864 | 35.39 |
| ware2 c1 blurred | OIDN 4 | 27.45 | 0.5143 | 33.99 |
| ware2 c1 blurred | OIDN 16 | 27.76 | 0.5171 | 32.56 |
