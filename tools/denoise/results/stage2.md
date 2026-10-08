## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.07 | 0.4185 | 43.07 |
| noisy 16 | 26.29 | 0.5904 | 30.03 |
| ours 4 | 36.46 | 0.9170 | 8.00 |
| ours 8 | 37.48 | 0.9212 | 7.50 |
| ours 16 | 38.23 | 0.9244 | 7.19 |
| ours 16, past only | 38.14 | 0.9229 | 7.31 |
| ours 16, alone | 35.76 | 0.9115 | 8.73 |
| OIDN 4 | 38.10 | 0.9271 | 7.62 |
| OIDN 16 | 38.95 | 0.9304 | 7.04 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.55 | 0.4048 | 46.98 |
| noisy 16 | 25.35 | 0.5704 | 33.78 |
| ours 4 | 32.56 | 0.8332 | 13.75 |
| ours 8 | 33.09 | 0.8361 | 13.25 |
| ours 16 | 33.38 | 0.8382 | 12.96 |
| ours 16, past only | 33.34 | 0.8374 | 13.04 |
| ours 16, alone | 32.29 | 0.8315 | 14.12 |
| OIDN 4 | 33.28 | 0.8395 | 13.29 |
| OIDN 16 | 33.67 | 0.8419 | 12.65 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 33.38 | 0.8382 | 12.96 |
| sharp frames denoised, then blurred | 27.63 | 0.8396 | 18.49 |
| sharp reference, then blurred | 27.74 | 0.8464 | 18.18 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.43 | 25.16 | 36.43 | 37.07 | 37.75 | 37.71 | 36.64 | 37.66 | 38.27 |
| 2 | 22.71 | 25.42 | 36.99 | 37.53 | 38.15 | 38.13 | 36.78 | 37.84 | 38.50 |
| 3 | 23.09 | 25.75 | 37.52 | 38.01 | 38.51 | 38.48 | 37.08 | 38.08 | 38.78 |
| 4 | 22.64 | 25.49 | 37.28 | 37.78 | 38.21 | 38.16 | 36.92 | 37.78 | 38.49 |
| 5 | 22.84 | 25.71 | 37.68 | 38.06 | 38.45 | 38.39 | 37.13 | 38.02 | 38.69 |
| 6 | 23.50 | 26.19 | 37.50 | 37.91 | 38.31 | 38.72 | 37.43 | 38.35 | 39.12 |
| 7 | 23.41 | 27.30 | 34.44 | 36.02 | 37.18 | 36.64 | 32.73 | 38.05 | 38.97 |
| 8 | 23.27 | 27.32 | 34.45 | 36.13 | 37.58 | 37.49 | 32.55 | 38.31 | 39.25 |
| 9 | 24.76 | 27.83 | 38.49 | 38.85 | 39.13 | 39.01 | 38.53 | 38.86 | 39.84 |
| 10 | 22.98 | 27.06 | 35.73 | 37.37 | 38.39 | 38.21 | 35.73 | 38.11 | 39.28 |
| 11 | 22.89 | 26.89 | 36.24 | 37.92 | 38.84 | 38.68 | 35.95 | 38.15 | 39.23 |
| 12 | 22.75 | 26.40 | 36.83 | 37.99 | 38.60 | 38.58 | 36.27 | 38.08 | 39.19 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 27.57 | 0.5997 | 29.02 |
| city2 c1 sharp | noisy 16 | 32.37 | 0.7835 | 16.28 |
| city2 c1 sharp | ours 4 | 44.22 | 0.9809 | 3.49 |
| city2 c1 sharp | ours 8 | 44.99 | 0.9819 | 3.35 |
| city2 c1 sharp | ours 16 | 45.52 | 0.9825 | 3.25 |
| city2 c1 sharp | ours 16, past only | 45.48 | 0.9823 | 3.28 |
| city2 c1 sharp | ours 16, alone | 45.20 | 0.9815 | 3.57 |
| city2 c1 sharp | OIDN 4 | 43.85 | 0.9800 | 3.69 |
| city2 c1 sharp | OIDN 16 | 45.12 | 0.9818 | 3.33 |
| city2 c1 blurred | noisy 4 | 27.45 | 0.5949 | 29.49 |
| city2 c1 blurred | noisy 16 | 31.99 | 0.7730 | 17.01 |
| city2 c1 blurred | ours 4 | 40.18 | 0.9496 | 6.00 |
| city2 c1 blurred | ours 8 | 40.45 | 0.9505 | 5.90 |
| city2 c1 blurred | ours 16 | 40.62 | 0.9511 | 5.83 |
| city2 c1 blurred | ours 16, past only | 40.61 | 0.9509 | 5.85 |
| city2 c1 blurred | ours 16, alone | 40.53 | 0.9502 | 6.02 |
| city2 c1 blurred | OIDN 4 | 40.11 | 0.9500 | 6.08 |
| city2 c1 blurred | OIDN 16 | 40.55 | 0.9512 | 5.83 |
| jail3 c1 sharp | noisy 4 | 27.26 | 0.4858 | 34.08 |
| jail3 c1 sharp | noisy 16 | 32.26 | 0.7236 | 19.20 |
| jail3 c1 sharp | ours 4 | 44.14 | 0.9800 | 3.98 |
| jail3 c1 sharp | ours 8 | 45.02 | 0.9811 | 3.79 |
| jail3 c1 sharp | ours 16 | 45.59 | 0.9818 | 3.65 |
| jail3 c1 sharp | ours 16, past only | 45.52 | 0.9817 | 3.68 |
| jail3 c1 sharp | ours 16, alone | 45.37 | 0.9811 | 3.94 |
| jail3 c1 sharp | OIDN 4 | 42.43 | 0.9742 | 4.99 |
| jail3 c1 sharp | OIDN 16 | 43.95 | 0.9774 | 4.39 |
| jail3 c1 blurred | noisy 4 | 26.58 | 0.4610 | 36.93 |
| jail3 c1 blurred | noisy 16 | 31.16 | 0.6933 | 21.33 |
| jail3 c1 blurred | ours 4 | 39.06 | 0.9353 | 7.71 |
| jail3 c1 blurred | ours 8 | 39.51 | 0.9366 | 7.46 |
| jail3 c1 blurred | ours 16 | 39.74 | 0.9375 | 7.34 |
| jail3 c1 blurred | ours 16, past only | 39.73 | 0.9374 | 7.35 |
| jail3 c1 blurred | ours 16, alone | 39.74 | 0.9370 | 7.47 |
| jail3 c1 blurred | OIDN 4 | 38.68 | 0.9324 | 8.33 |
| jail3 c1 blurred | OIDN 16 | 39.41 | 0.9353 | 7.72 |
| mine3 c1 sharp | noisy 4 | 26.46 | 0.5360 | 30.89 |
| mine3 c1 sharp | noisy 16 | 30.41 | 0.7240 | 19.36 |
| mine3 c1 sharp | ours 4 | 42.11 | 0.9800 | 3.45 |
| mine3 c1 sharp | ours 8 | 42.92 | 0.9811 | 3.31 |
| mine3 c1 sharp | ours 16 | 43.63 | 0.9819 | 3.19 |
| mine3 c1 sharp | ours 16, past only | 43.53 | 0.9816 | 3.26 |
| mine3 c1 sharp | ours 16, alone | 43.16 | 0.9795 | 3.82 |
| mine3 c1 sharp | OIDN 4 | 42.11 | 0.9801 | 3.77 |
| mine3 c1 sharp | OIDN 16 | 43.76 | 0.9823 | 3.29 |
| mine3 c1 blurred | noisy 4 | 26.39 | 0.5286 | 31.53 |
| mine3 c1 blurred | noisy 16 | 30.15 | 0.7147 | 20.03 |
| mine3 c1 blurred | ours 4 | 38.82 | 0.9539 | 5.79 |
| mine3 c1 blurred | ours 8 | 39.19 | 0.9549 | 5.68 |
| mine3 c1 blurred | ours 16 | 39.42 | 0.9557 | 5.59 |
| mine3 c1 blurred | ours 16, past only | 39.39 | 0.9554 | 5.62 |
| mine3 c1 blurred | ours 16, alone | 39.28 | 0.9537 | 5.99 |
| mine3 c1 blurred | OIDN 4 | 38.79 | 0.9549 | 5.95 |
| mine3 c1 blurred | OIDN 16 | 39.45 | 0.9565 | 5.60 |
| power2 c1 sharp | noisy 4 | 21.59 | 0.2892 | 59.31 |
| power2 c1 sharp | noisy 16 | 25.11 | 0.4439 | 40.73 |
| power2 c1 sharp | ours 4 | 37.52 | 0.9165 | 8.50 |
| power2 c1 sharp | ours 8 | 38.13 | 0.9201 | 8.24 |
| power2 c1 sharp | ours 16 | 38.70 | 0.9233 | 8.06 |
| power2 c1 sharp | ours 16, past only | 38.54 | 0.9220 | 8.21 |
| power2 c1 sharp | ours 16, alone | 38.26 | 0.9185 | 9.06 |
| power2 c1 sharp | OIDN 4 | 37.19 | 0.9186 | 9.21 |
| power2 c1 sharp | OIDN 16 | 38.61 | 0.9251 | 8.43 |
| power2 c1 blurred | noisy 4 | 21.46 | 0.2742 | 61.31 |
| power2 c1 blurred | noisy 16 | 24.82 | 0.4168 | 43.14 |
| power2 c1 blurred | ours 4 | 33.86 | 0.8056 | 14.93 |
| power2 c1 blurred | ours 8 | 34.15 | 0.8082 | 14.67 |
| power2 c1 blurred | ours 16 | 34.35 | 0.8103 | 14.49 |
| power2 c1 blurred | ours 16, past only | 34.34 | 0.8097 | 14.52 |
| power2 c1 blurred | ours 16, alone | 34.29 | 0.8082 | 14.94 |
| power2 c1 blurred | OIDN 4 | 33.92 | 0.8099 | 15.16 |
| power2 c1 blurred | OIDN 16 | 34.42 | 0.8135 | 14.44 |
| q2dm4 c1 sharp | noisy 4 | 25.10 | 0.4118 | 40.96 |
| q2dm4 c1 sharp | noisy 16 | 29.05 | 0.6094 | 25.09 |
| q2dm4 c1 sharp | ours 4 | 39.70 | 0.9413 | 6.59 |
| q2dm4 c1 sharp | ours 8 | 40.12 | 0.9432 | 6.31 |
| q2dm4 c1 sharp | ours 16 | 40.50 | 0.9447 | 6.11 |
| q2dm4 c1 sharp | ours 16, past only | 40.48 | 0.9444 | 6.16 |
| q2dm4 c1 sharp | ours 16, alone | 40.41 | 0.9436 | 6.41 |
| q2dm4 c1 sharp | OIDN 4 | 40.17 | 0.9452 | 6.47 |
| q2dm4 c1 sharp | OIDN 16 | 40.89 | 0.9477 | 5.98 |
| q2dm4 c1 blurred | noisy 4 | 24.79 | 0.3947 | 43.13 |
| q2dm4 c1 blurred | noisy 16 | 28.52 | 0.5842 | 26.96 |
| q2dm4 c1 blurred | ours 4 | 35.97 | 0.8730 | 10.31 |
| q2dm4 c1 blurred | ours 8 | 36.20 | 0.8748 | 10.03 |
| q2dm4 c1 blurred | ours 16 | 36.37 | 0.8759 | 9.82 |
| q2dm4 c1 blurred | ours 16, past only | 36.36 | 0.8757 | 9.86 |
| q2dm4 c1 blurred | ours 16, alone | 36.35 | 0.8754 | 9.97 |
| q2dm4 c1 blurred | OIDN 4 | 36.06 | 0.8756 | 10.23 |
| q2dm4 c1 blurred | OIDN 16 | 36.44 | 0.8775 | 9.72 |
| ware2 c1 sharp | noisy 4 | 18.64 | 0.1888 | 64.19 |
| ware2 c1 sharp | noisy 16 | 21.15 | 0.2578 | 59.50 |
| ware2 c1 sharp | ours 4 | 30.29 | 0.7031 | 21.95 |
| ware2 c1 sharp | ours 8 | 31.50 | 0.7196 | 20.00 |
| ware2 c1 sharp | ours 16 | 32.37 | 0.7322 | 18.87 |
| ware2 c1 sharp | ours 16, past only | 32.28 | 0.7253 | 19.28 |
| ware2 c1 sharp | ours 16, alone | 29.04 | 0.6650 | 25.61 |
| ware2 c1 sharp | OIDN 4 | 33.16 | 0.7641 | 17.57 |
| ware2 c1 sharp | OIDN 16 | 33.64 | 0.7679 | 16.84 |
| ware2 c1 blurred | noisy 4 | 17.81 | 0.1751 | 79.51 |
| ware2 c1 blurred | noisy 16 | 19.82 | 0.2405 | 74.18 |
| ware2 c1 blurred | ours 4 | 26.37 | 0.4821 | 37.77 |
| ware2 c1 blurred | ours 8 | 26.99 | 0.4919 | 35.80 |
| ware2 c1 blurred | ours 16 | 27.34 | 0.4988 | 34.70 |
| ware2 c1 blurred | ours 16, past only | 27.27 | 0.4953 | 35.06 |
| ware2 c1 blurred | ours 16, alone | 25.80 | 0.4645 | 40.36 |
| ware2 c1 blurred | OIDN 4 | 27.45 | 0.5143 | 33.99 |
| ware2 c1 blurred | OIDN 16 | 27.76 | 0.5171 | 32.56 |
