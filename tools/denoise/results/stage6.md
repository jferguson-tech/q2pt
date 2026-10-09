## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.07 | 0.4185 | 43.07 |
| noisy 16 | 26.29 | 0.5904 | 30.03 |
| ours 4 | 37.75 | 0.9242 | 7.36 |
| ours 8 | 38.35 | 0.9265 | 7.08 |
| ours 16 | 38.83 | 0.9284 | 6.89 |
| ours 16, past only | 38.75 | 0.9278 | 6.96 |
| ours 16, alone | 36.89 | 0.9192 | 8.00 |
| OIDN 4 | 38.10 | 0.9271 | 7.62 |
| OIDN 16 | 38.95 | 0.9304 | 7.04 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.55 | 0.4048 | 46.98 |
| noisy 16 | 25.35 | 0.5704 | 33.78 |
| ours 4 | 33.10 | 0.8372 | 13.31 |
| ours 8 | 33.39 | 0.8389 | 12.96 |
| ours 16 | 33.56 | 0.8402 | 12.75 |
| ours 16, past only | 33.55 | 0.8399 | 12.78 |
| ours 16, alone | 32.87 | 0.8359 | 13.54 |
| OIDN 4 | 33.28 | 0.8395 | 13.29 |
| OIDN 16 | 33.67 | 0.8419 | 12.65 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 33.56 | 0.8402 | 12.75 |
| sharp frames denoised, then blurred | 27.63 | 0.8405 | 18.39 |
| sharp reference, then blurred | 27.74 | 0.8464 | 18.18 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.43 | 25.16 | 37.56 | 37.95 | 38.27 | 38.19 | 37.40 | 37.66 | 38.27 |
| 2 | 22.71 | 25.42 | 37.99 | 38.28 | 38.53 | 38.47 | 37.52 | 37.84 | 38.50 |
| 3 | 23.09 | 25.75 | 38.38 | 38.63 | 38.83 | 38.76 | 37.79 | 38.08 | 38.78 |
| 4 | 22.64 | 25.49 | 38.01 | 38.33 | 38.54 | 38.48 | 37.59 | 37.78 | 38.49 |
| 5 | 22.84 | 25.71 | 38.30 | 38.57 | 38.78 | 38.71 | 37.77 | 38.02 | 38.69 |
| 6 | 23.50 | 26.19 | 38.50 | 38.82 | 39.03 | 39.11 | 38.09 | 38.35 | 39.12 |
| 7 | 23.41 | 27.30 | 36.24 | 37.34 | 38.40 | 37.99 | 34.23 | 38.05 | 38.97 |
| 8 | 23.27 | 27.32 | 35.76 | 37.05 | 38.27 | 38.33 | 34.02 | 38.31 | 39.25 |
| 9 | 24.76 | 27.83 | 39.14 | 39.69 | 39.95 | 39.83 | 39.87 | 38.86 | 39.84 |
| 10 | 22.98 | 27.06 | 37.89 | 38.57 | 39.14 | 39.05 | 37.08 | 38.11 | 39.28 |
| 11 | 22.89 | 26.89 | 38.32 | 38.92 | 39.40 | 39.31 | 37.32 | 38.15 | 39.23 |
| 12 | 22.75 | 26.40 | 38.16 | 38.69 | 39.16 | 39.16 | 37.52 | 38.08 | 39.19 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 27.57 | 0.5997 | 29.02 |
| city2 c1 sharp | noisy 16 | 32.37 | 0.7835 | 16.28 |
| city2 c1 sharp | ours 4 | 44.70 | 0.9816 | 3.40 |
| city2 c1 sharp | ours 8 | 45.35 | 0.9823 | 3.28 |
| city2 c1 sharp | ours 16 | 45.76 | 0.9829 | 3.19 |
| city2 c1 sharp | ours 16, past only | 45.76 | 0.9828 | 3.21 |
| city2 c1 sharp | ours 16, alone | 45.52 | 0.9821 | 3.45 |
| city2 c1 sharp | OIDN 4 | 43.85 | 0.9800 | 3.69 |
| city2 c1 sharp | OIDN 16 | 45.12 | 0.9818 | 3.33 |
| city2 c1 blurred | noisy 4 | 27.45 | 0.5949 | 29.49 |
| city2 c1 blurred | noisy 16 | 31.99 | 0.7730 | 17.01 |
| city2 c1 blurred | ours 4 | 40.36 | 0.9502 | 5.93 |
| city2 c1 blurred | ours 8 | 40.57 | 0.9509 | 5.84 |
| city2 c1 blurred | ours 16 | 40.69 | 0.9514 | 5.78 |
| city2 c1 blurred | ours 16, past only | 40.69 | 0.9513 | 5.79 |
| city2 c1 blurred | ours 16, alone | 40.63 | 0.9508 | 5.94 |
| city2 c1 blurred | OIDN 4 | 40.11 | 0.9500 | 6.08 |
| city2 c1 blurred | OIDN 16 | 40.55 | 0.9512 | 5.83 |
| jail3 c1 sharp | noisy 4 | 27.26 | 0.4858 | 34.08 |
| jail3 c1 sharp | noisy 16 | 32.26 | 0.7236 | 19.20 |
| jail3 c1 sharp | ours 4 | 44.89 | 0.9811 | 3.86 |
| jail3 c1 sharp | ours 8 | 45.53 | 0.9818 | 3.70 |
| jail3 c1 sharp | ours 16 | 45.88 | 0.9823 | 3.60 |
| jail3 c1 sharp | ours 16, past only | 45.86 | 0.9822 | 3.61 |
| jail3 c1 sharp | ours 16, alone | 45.69 | 0.9818 | 3.79 |
| jail3 c1 sharp | OIDN 4 | 42.43 | 0.9742 | 4.99 |
| jail3 c1 sharp | OIDN 16 | 43.95 | 0.9774 | 4.39 |
| jail3 c1 blurred | noisy 4 | 26.58 | 0.4610 | 36.93 |
| jail3 c1 blurred | noisy 16 | 31.16 | 0.6933 | 21.33 |
| jail3 c1 blurred | ours 4 | 39.32 | 0.9364 | 7.64 |
| jail3 c1 blurred | ours 8 | 39.68 | 0.9373 | 7.38 |
| jail3 c1 blurred | ours 16 | 39.82 | 0.9379 | 7.29 |
| jail3 c1 blurred | ours 16, past only | 39.83 | 0.9379 | 7.30 |
| jail3 c1 blurred | ours 16, alone | 39.82 | 0.9376 | 7.38 |
| jail3 c1 blurred | OIDN 4 | 38.68 | 0.9324 | 8.33 |
| jail3 c1 blurred | OIDN 16 | 39.41 | 0.9353 | 7.72 |
| mine3 c1 sharp | noisy 4 | 26.46 | 0.5360 | 30.89 |
| mine3 c1 sharp | noisy 16 | 30.41 | 0.7240 | 19.36 |
| mine3 c1 sharp | ours 4 | 42.57 | 0.9810 | 3.36 |
| mine3 c1 sharp | ours 8 | 43.09 | 0.9818 | 3.24 |
| mine3 c1 sharp | ours 16 | 43.60 | 0.9824 | 3.14 |
| mine3 c1 sharp | ours 16, past only | 43.51 | 0.9821 | 3.22 |
| mine3 c1 sharp | ours 16, alone | 43.32 | 0.9807 | 3.63 |
| mine3 c1 sharp | OIDN 4 | 42.11 | 0.9801 | 3.77 |
| mine3 c1 sharp | OIDN 16 | 43.76 | 0.9823 | 3.29 |
| mine3 c1 blurred | noisy 4 | 26.39 | 0.5286 | 31.53 |
| mine3 c1 blurred | noisy 16 | 30.15 | 0.7147 | 20.03 |
| mine3 c1 blurred | ours 4 | 38.99 | 0.9547 | 5.73 |
| mine3 c1 blurred | ours 8 | 39.28 | 0.9555 | 5.62 |
| mine3 c1 blurred | ours 16 | 39.46 | 0.9561 | 5.54 |
| mine3 c1 blurred | ours 16, past only | 39.44 | 0.9559 | 5.58 |
| mine3 c1 blurred | ours 16, alone | 39.36 | 0.9548 | 5.84 |
| mine3 c1 blurred | OIDN 4 | 38.79 | 0.9549 | 5.95 |
| mine3 c1 blurred | OIDN 16 | 39.45 | 0.9565 | 5.60 |
| power2 c1 sharp | noisy 4 | 21.59 | 0.2892 | 59.31 |
| power2 c1 sharp | noisy 16 | 25.11 | 0.4439 | 40.73 |
| power2 c1 sharp | ours 4 | 37.88 | 0.9191 | 8.32 |
| power2 c1 sharp | ours 8 | 38.40 | 0.9219 | 8.12 |
| power2 c1 sharp | ours 16 | 38.89 | 0.9246 | 7.95 |
| power2 c1 sharp | ours 16, past only | 38.71 | 0.9233 | 8.12 |
| power2 c1 sharp | ours 16, alone | 38.49 | 0.9208 | 8.78 |
| power2 c1 sharp | OIDN 4 | 37.19 | 0.9186 | 9.21 |
| power2 c1 sharp | OIDN 16 | 38.61 | 0.9251 | 8.43 |
| power2 c1 blurred | noisy 4 | 21.46 | 0.2742 | 61.31 |
| power2 c1 blurred | noisy 16 | 24.82 | 0.4168 | 43.14 |
| power2 c1 blurred | ours 4 | 33.99 | 0.8074 | 14.83 |
| power2 c1 blurred | ours 8 | 34.23 | 0.8094 | 14.60 |
| power2 c1 blurred | ours 16 | 34.40 | 0.8113 | 14.42 |
| power2 c1 blurred | ours 16, past only | 34.40 | 0.8109 | 14.44 |
| power2 c1 blurred | ours 16, alone | 34.36 | 0.8099 | 14.76 |
| power2 c1 blurred | OIDN 4 | 33.92 | 0.8099 | 15.16 |
| power2 c1 blurred | OIDN 16 | 34.42 | 0.8135 | 14.44 |
| q2dm4 c1 sharp | noisy 4 | 25.10 | 0.4118 | 40.96 |
| q2dm4 c1 sharp | noisy 16 | 29.05 | 0.6094 | 25.09 |
| q2dm4 c1 sharp | ours 4 | 39.95 | 0.9432 | 6.41 |
| q2dm4 c1 sharp | ours 8 | 40.31 | 0.9446 | 6.18 |
| q2dm4 c1 sharp | ours 16 | 40.64 | 0.9457 | 6.01 |
| q2dm4 c1 sharp | ours 16, past only | 40.61 | 0.9455 | 6.07 |
| q2dm4 c1 sharp | ours 16, alone | 40.57 | 0.9450 | 6.25 |
| q2dm4 c1 sharp | OIDN 4 | 40.17 | 0.9452 | 6.47 |
| q2dm4 c1 sharp | OIDN 16 | 40.89 | 0.9477 | 5.98 |
| q2dm4 c1 blurred | noisy 4 | 24.79 | 0.3947 | 43.13 |
| q2dm4 c1 blurred | noisy 16 | 28.52 | 0.5842 | 26.96 |
| q2dm4 c1 blurred | ours 4 | 36.07 | 0.8745 | 10.15 |
| q2dm4 c1 blurred | ours 8 | 36.27 | 0.8757 | 9.91 |
| q2dm4 c1 blurred | ours 16 | 36.41 | 0.8766 | 9.74 |
| q2dm4 c1 blurred | ours 16, past only | 36.41 | 0.8765 | 9.77 |
| q2dm4 c1 blurred | ours 16, alone | 36.40 | 0.8764 | 9.85 |
| q2dm4 c1 blurred | OIDN 4 | 36.06 | 0.8756 | 10.23 |
| q2dm4 c1 blurred | OIDN 16 | 36.44 | 0.8775 | 9.72 |
| ware2 c1 sharp | noisy 4 | 18.64 | 0.1888 | 64.19 |
| ware2 c1 sharp | noisy 16 | 21.15 | 0.2578 | 59.50 |
| ware2 c1 sharp | ours 4 | 32.06 | 0.7390 | 18.79 |
| ware2 c1 sharp | ours 8 | 32.73 | 0.7468 | 17.95 |
| ware2 c1 sharp | ours 16 | 33.25 | 0.7527 | 17.42 |
| ware2 c1 sharp | ours 16, past only | 33.18 | 0.7508 | 17.56 |
| ware2 c1 sharp | ours 16, alone | 30.47 | 0.7045 | 22.13 |
| ware2 c1 sharp | OIDN 4 | 33.16 | 0.7641 | 17.57 |
| ware2 c1 sharp | OIDN 16 | 33.64 | 0.7679 | 16.84 |
| ware2 c1 blurred | noisy 4 | 17.81 | 0.1751 | 79.51 |
| ware2 c1 blurred | noisy 16 | 19.82 | 0.2405 | 74.18 |
| ware2 c1 blurred | ours 4 | 27.08 | 0.5000 | 35.59 |
| ware2 c1 blurred | ours 8 | 27.39 | 0.5044 | 34.40 |
| ware2 c1 blurred | ours 16 | 27.58 | 0.5080 | 33.71 |
| ware2 c1 blurred | ours 16, past only | 27.56 | 0.5071 | 33.83 |
| ware2 c1 blurred | ours 16, alone | 26.59 | 0.4859 | 37.48 |
| ware2 c1 blurred | OIDN 4 | 27.45 | 0.5143 | 33.99 |
| ware2 c1 blurred | OIDN 16 | 27.76 | 0.5171 | 32.56 |
