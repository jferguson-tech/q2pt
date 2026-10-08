## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.07 | 0.4185 | 43.07 |
| noisy 16 | 26.29 | 0.5904 | 30.03 |
| ours 4 | 37.28 | 0.9231 | 7.18 |
| ours 8 | 37.93 | 0.9262 | 6.94 |
| ours 16 | 38.48 | 0.9285 | 6.76 |
| ours 16, past only | 38.43 | 0.9280 | 6.83 |
| ours 16, alone | 37.07 | 0.9197 | 7.90 |
| OIDN 4 | 38.10 | 0.9271 | 7.62 |
| OIDN 16 | 38.95 | 0.9304 | 7.04 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.55 | 0.4048 | 46.98 |
| noisy 16 | 25.35 | 0.5704 | 33.78 |
| ours 4 | 33.06 | 0.8380 | 13.06 |
| ours 8 | 33.36 | 0.8398 | 12.76 |
| ours 16 | 33.56 | 0.8412 | 12.59 |
| ours 16, past only | 33.56 | 0.8410 | 12.62 |
| ours 16, alone | 32.97 | 0.8362 | 13.45 |
| OIDN 4 | 33.28 | 0.8395 | 13.29 |
| OIDN 16 | 33.67 | 0.8419 | 12.65 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 33.56 | 0.8412 | 12.59 |
| sharp frames denoised, then blurred | 27.64 | 0.8408 | 18.34 |
| sharp reference, then blurred | 27.74 | 0.8464 | 18.18 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.43 | 25.16 | 36.75 | 37.18 | 37.41 | 37.47 | 37.47 | 37.66 | 38.27 |
| 2 | 22.71 | 25.42 | 36.91 | 37.27 | 37.56 | 37.59 | 37.59 | 37.84 | 38.50 |
| 3 | 23.09 | 25.75 | 37.23 | 37.60 | 37.87 | 37.90 | 37.88 | 38.08 | 38.78 |
| 4 | 22.64 | 25.49 | 37.01 | 37.42 | 37.71 | 37.72 | 37.68 | 37.78 | 38.49 |
| 5 | 22.84 | 25.71 | 37.24 | 37.63 | 37.94 | 37.94 | 37.86 | 38.02 | 38.69 |
| 6 | 23.50 | 26.19 | 37.67 | 38.03 | 38.41 | 38.26 | 38.19 | 38.35 | 39.12 |
| 7 | 23.41 | 27.30 | 36.42 | 37.69 | 38.84 | 38.24 | 34.56 | 38.05 | 38.97 |
| 8 | 23.27 | 27.32 | 35.52 | 36.83 | 38.21 | 38.44 | 34.31 | 38.31 | 39.25 |
| 9 | 24.76 | 27.83 | 38.91 | 39.30 | 39.76 | 39.75 | 39.74 | 38.86 | 39.84 |
| 10 | 22.98 | 27.06 | 37.97 | 38.78 | 39.51 | 39.38 | 37.30 | 38.11 | 39.28 |
| 11 | 22.89 | 26.89 | 38.47 | 39.22 | 39.86 | 39.74 | 37.61 | 38.15 | 39.23 |
| 12 | 22.75 | 26.40 | 38.48 | 39.16 | 39.72 | 39.74 | 37.79 | 38.08 | 39.19 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 27.57 | 0.5997 | 29.02 |
| city2 c1 sharp | noisy 16 | 32.37 | 0.7835 | 16.28 |
| city2 c1 sharp | ours 4 | 44.70 | 0.9817 | 3.38 |
| city2 c1 sharp | ours 8 | 45.35 | 0.9824 | 3.26 |
| city2 c1 sharp | ours 16 | 45.79 | 0.9830 | 3.17 |
| city2 c1 sharp | ours 16, past only | 45.78 | 0.9829 | 3.19 |
| city2 c1 sharp | ours 16, alone | 45.54 | 0.9822 | 3.43 |
| city2 c1 sharp | OIDN 4 | 43.85 | 0.9800 | 3.69 |
| city2 c1 sharp | OIDN 16 | 45.12 | 0.9818 | 3.33 |
| city2 c1 blurred | noisy 4 | 27.45 | 0.5949 | 29.49 |
| city2 c1 blurred | noisy 16 | 31.99 | 0.7730 | 17.01 |
| city2 c1 blurred | ours 4 | 40.36 | 0.9503 | 5.92 |
| city2 c1 blurred | ours 8 | 40.58 | 0.9510 | 5.83 |
| city2 c1 blurred | ours 16 | 40.70 | 0.9515 | 5.77 |
| city2 c1 blurred | ours 16, past only | 40.70 | 0.9514 | 5.78 |
| city2 c1 blurred | ours 16, alone | 40.63 | 0.9508 | 5.93 |
| city2 c1 blurred | OIDN 4 | 40.11 | 0.9500 | 6.08 |
| city2 c1 blurred | OIDN 16 | 40.55 | 0.9512 | 5.83 |
| jail3 c1 sharp | noisy 4 | 27.26 | 0.4858 | 34.08 |
| jail3 c1 sharp | noisy 16 | 32.26 | 0.7236 | 19.20 |
| jail3 c1 sharp | ours 4 | 44.98 | 0.9812 | 3.83 |
| jail3 c1 sharp | ours 8 | 45.59 | 0.9819 | 3.68 |
| jail3 c1 sharp | ours 16 | 45.91 | 0.9824 | 3.59 |
| jail3 c1 sharp | ours 16, past only | 45.88 | 0.9823 | 3.61 |
| jail3 c1 sharp | ours 16, alone | 45.73 | 0.9818 | 3.79 |
| jail3 c1 sharp | OIDN 4 | 42.43 | 0.9742 | 4.99 |
| jail3 c1 sharp | OIDN 16 | 43.95 | 0.9774 | 4.39 |
| jail3 c1 blurred | noisy 4 | 26.58 | 0.4610 | 36.93 |
| jail3 c1 blurred | noisy 16 | 31.16 | 0.6933 | 21.33 |
| jail3 c1 blurred | ours 4 | 39.35 | 0.9365 | 7.62 |
| jail3 c1 blurred | ours 8 | 39.70 | 0.9374 | 7.37 |
| jail3 c1 blurred | ours 16 | 39.84 | 0.9380 | 7.28 |
| jail3 c1 blurred | ours 16, past only | 39.84 | 0.9379 | 7.29 |
| jail3 c1 blurred | ours 16, alone | 39.83 | 0.9377 | 7.37 |
| jail3 c1 blurred | OIDN 4 | 38.68 | 0.9324 | 8.33 |
| jail3 c1 blurred | OIDN 16 | 39.41 | 0.9353 | 7.72 |
| mine3 c1 sharp | noisy 4 | 26.46 | 0.5360 | 30.89 |
| mine3 c1 sharp | noisy 16 | 30.41 | 0.7240 | 19.36 |
| mine3 c1 sharp | ours 4 | 42.44 | 0.9810 | 3.36 |
| mine3 c1 sharp | ours 8 | 43.05 | 0.9818 | 3.23 |
| mine3 c1 sharp | ours 16 | 43.63 | 0.9824 | 3.13 |
| mine3 c1 sharp | ours 16, past only | 43.52 | 0.9821 | 3.21 |
| mine3 c1 sharp | ours 16, alone | 43.34 | 0.9808 | 3.62 |
| mine3 c1 sharp | OIDN 4 | 42.11 | 0.9801 | 3.77 |
| mine3 c1 sharp | OIDN 16 | 43.76 | 0.9823 | 3.29 |
| mine3 c1 blurred | noisy 4 | 26.39 | 0.5286 | 31.53 |
| mine3 c1 blurred | noisy 16 | 30.15 | 0.7147 | 20.03 |
| mine3 c1 blurred | ours 4 | 38.96 | 0.9547 | 5.72 |
| mine3 c1 blurred | ours 8 | 39.27 | 0.9556 | 5.61 |
| mine3 c1 blurred | ours 16 | 39.47 | 0.9561 | 5.54 |
| mine3 c1 blurred | ours 16, past only | 39.44 | 0.9559 | 5.58 |
| mine3 c1 blurred | ours 16, alone | 39.38 | 0.9548 | 5.84 |
| mine3 c1 blurred | OIDN 4 | 38.79 | 0.9549 | 5.95 |
| mine3 c1 blurred | OIDN 16 | 39.45 | 0.9565 | 5.60 |
| power2 c1 sharp | noisy 4 | 21.59 | 0.2892 | 59.31 |
| power2 c1 sharp | noisy 16 | 25.11 | 0.4439 | 40.73 |
| power2 c1 sharp | ours 4 | 37.90 | 0.9193 | 8.33 |
| power2 c1 sharp | ours 8 | 38.41 | 0.9221 | 8.11 |
| power2 c1 sharp | ours 16 | 38.90 | 0.9247 | 7.94 |
| power2 c1 sharp | ours 16, past only | 38.72 | 0.9234 | 8.11 |
| power2 c1 sharp | ours 16, alone | 38.51 | 0.9210 | 8.78 |
| power2 c1 sharp | OIDN 4 | 37.19 | 0.9186 | 9.21 |
| power2 c1 sharp | OIDN 16 | 38.61 | 0.9251 | 8.43 |
| power2 c1 blurred | noisy 4 | 21.46 | 0.2742 | 61.31 |
| power2 c1 blurred | noisy 16 | 24.82 | 0.4168 | 43.14 |
| power2 c1 blurred | ours 4 | 33.98 | 0.8074 | 14.84 |
| power2 c1 blurred | ours 8 | 34.23 | 0.8095 | 14.60 |
| power2 c1 blurred | ours 16 | 34.39 | 0.8113 | 14.42 |
| power2 c1 blurred | ours 16, past only | 34.39 | 0.8110 | 14.46 |
| power2 c1 blurred | ours 16, alone | 34.36 | 0.8100 | 14.78 |
| power2 c1 blurred | OIDN 4 | 33.92 | 0.8099 | 15.16 |
| power2 c1 blurred | OIDN 16 | 34.42 | 0.8135 | 14.44 |
| q2dm4 c1 sharp | noisy 4 | 25.10 | 0.4118 | 40.96 |
| q2dm4 c1 sharp | noisy 16 | 29.05 | 0.6094 | 25.09 |
| q2dm4 c1 sharp | ours 4 | 39.97 | 0.9432 | 6.40 |
| q2dm4 c1 sharp | ours 8 | 40.31 | 0.9446 | 6.17 |
| q2dm4 c1 sharp | ours 16 | 40.65 | 0.9458 | 6.00 |
| q2dm4 c1 sharp | ours 16, past only | 40.62 | 0.9455 | 6.05 |
| q2dm4 c1 sharp | ours 16, alone | 40.58 | 0.9451 | 6.24 |
| q2dm4 c1 sharp | OIDN 4 | 40.17 | 0.9452 | 6.47 |
| q2dm4 c1 sharp | OIDN 16 | 40.89 | 0.9477 | 5.98 |
| q2dm4 c1 blurred | noisy 4 | 24.79 | 0.3947 | 43.13 |
| q2dm4 c1 blurred | noisy 16 | 28.52 | 0.5842 | 26.96 |
| q2dm4 c1 blurred | ours 4 | 36.08 | 0.8746 | 10.16 |
| q2dm4 c1 blurred | ours 8 | 36.27 | 0.8758 | 9.91 |
| q2dm4 c1 blurred | ours 16 | 36.42 | 0.8767 | 9.72 |
| q2dm4 c1 blurred | ours 16, past only | 36.41 | 0.8766 | 9.76 |
| q2dm4 c1 blurred | ours 16, alone | 36.40 | 0.8764 | 9.84 |
| q2dm4 c1 blurred | OIDN 4 | 36.06 | 0.8756 | 10.23 |
| q2dm4 c1 blurred | OIDN 16 | 36.44 | 0.8775 | 9.72 |
| ware2 c1 sharp | noisy 4 | 18.64 | 0.1888 | 64.19 |
| ware2 c1 sharp | noisy 16 | 21.15 | 0.2578 | 59.50 |
| ware2 c1 sharp | ours 4 | 31.33 | 0.7324 | 17.82 |
| ware2 c1 sharp | ours 8 | 32.06 | 0.7445 | 17.17 |
| ware2 c1 sharp | ours 16 | 32.66 | 0.7529 | 16.71 |
| ware2 c1 sharp | ours 16, past only | 32.65 | 0.7517 | 16.83 |
| ware2 c1 sharp | ours 16, alone | 30.72 | 0.7075 | 21.57 |
| ware2 c1 sharp | OIDN 4 | 33.16 | 0.7641 | 17.57 |
| ware2 c1 sharp | OIDN 16 | 33.64 | 0.7679 | 16.84 |
| ware2 c1 blurred | noisy 4 | 17.81 | 0.1751 | 79.51 |
| ware2 c1 blurred | noisy 16 | 19.82 | 0.2405 | 74.18 |
| ware2 c1 blurred | ours 4 | 27.03 | 0.5044 | 34.12 |
| ware2 c1 blurred | ours 8 | 27.35 | 0.5098 | 33.22 |
| ware2 c1 blurred | ours 16 | 27.57 | 0.5138 | 32.82 |
| ware2 c1 blurred | ours 16, past only | 27.57 | 0.5133 | 32.86 |
| ware2 c1 blurred | ours 16, alone | 26.72 | 0.4874 | 36.96 |
| ware2 c1 blurred | OIDN 4 | 27.45 | 0.5143 | 33.99 |
| ware2 c1 blurred | OIDN 16 | 27.76 | 0.5171 | 32.56 |
