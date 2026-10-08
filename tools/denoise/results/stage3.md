## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.07 | 0.4185 | 43.07 |
| noisy 16 | 26.29 | 0.5904 | 30.03 |
| ours 4 | 37.26 | 0.9219 | 7.52 |
| ours 8 | 37.96 | 0.9248 | 7.21 |
| ours 16 | 38.61 | 0.9274 | 6.98 |
| ours 16, past only | 38.57 | 0.9266 | 7.06 |
| ours 16, alone | 36.51 | 0.9156 | 8.12 |
| OIDN 4 | 38.10 | 0.9271 | 7.62 |
| OIDN 16 | 38.95 | 0.9304 | 7.04 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.55 | 0.4048 | 46.98 |
| noisy 16 | 25.35 | 0.5704 | 33.78 |
| ours 4 | 32.97 | 0.8364 | 13.40 |
| ours 8 | 33.31 | 0.8383 | 13.03 |
| ours 16 | 33.53 | 0.8399 | 12.81 |
| ours 16, past only | 33.51 | 0.8394 | 12.85 |
| ours 16, alone | 32.74 | 0.8345 | 13.58 |
| OIDN 4 | 33.28 | 0.8395 | 13.29 |
| OIDN 16 | 33.67 | 0.8419 | 12.65 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 33.53 | 0.8399 | 12.81 |
| sharp frames denoised, then blurred | 27.62 | 0.8403 | 18.43 |
| sharp reference, then blurred | 27.74 | 0.8464 | 18.18 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.43 | 25.16 | 36.54 | 37.10 | 37.88 | 37.89 | 36.47 | 37.66 | 38.27 |
| 2 | 22.71 | 25.42 | 37.20 | 37.68 | 38.31 | 38.31 | 36.60 | 37.84 | 38.50 |
| 3 | 23.09 | 25.75 | 37.77 | 38.23 | 38.70 | 38.67 | 36.88 | 38.08 | 38.78 |
| 4 | 22.64 | 25.49 | 37.57 | 38.06 | 38.45 | 38.40 | 36.74 | 37.78 | 38.49 |
| 5 | 22.84 | 25.71 | 37.95 | 38.32 | 38.68 | 38.63 | 36.92 | 38.02 | 38.69 |
| 6 | 23.50 | 26.19 | 38.03 | 38.36 | 38.64 | 39.00 | 37.19 | 38.35 | 39.12 |
| 7 | 23.41 | 27.30 | 35.94 | 37.06 | 38.12 | 37.65 | 34.43 | 38.05 | 38.97 |
| 8 | 23.27 | 27.32 | 35.36 | 36.68 | 37.98 | 38.06 | 34.18 | 38.31 | 39.25 |
| 9 | 24.76 | 27.83 | 38.96 | 39.31 | 39.60 | 39.56 | 39.07 | 38.86 | 39.84 |
| 10 | 22.98 | 27.06 | 37.39 | 38.33 | 38.99 | 38.88 | 36.99 | 38.11 | 39.28 |
| 11 | 22.89 | 26.89 | 37.87 | 38.67 | 39.26 | 39.15 | 37.32 | 38.15 | 39.23 |
| 12 | 22.75 | 26.40 | 37.88 | 38.49 | 39.03 | 39.02 | 37.53 | 38.08 | 39.19 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 27.57 | 0.5997 | 29.02 |
| city2 c1 sharp | noisy 16 | 32.37 | 0.7835 | 16.28 |
| city2 c1 sharp | ours 4 | 44.44 | 0.9814 | 3.41 |
| city2 c1 sharp | ours 8 | 45.18 | 0.9822 | 3.29 |
| city2 c1 sharp | ours 16 | 45.69 | 0.9828 | 3.20 |
| city2 c1 sharp | ours 16, past only | 45.66 | 0.9827 | 3.23 |
| city2 c1 sharp | ours 16, alone | 45.42 | 0.9820 | 3.47 |
| city2 c1 sharp | OIDN 4 | 43.85 | 0.9800 | 3.69 |
| city2 c1 sharp | OIDN 16 | 45.12 | 0.9818 | 3.33 |
| city2 c1 blurred | noisy 4 | 27.45 | 0.5949 | 29.49 |
| city2 c1 blurred | noisy 16 | 31.99 | 0.7730 | 17.01 |
| city2 c1 blurred | ours 4 | 40.27 | 0.9500 | 5.94 |
| city2 c1 blurred | ours 8 | 40.52 | 0.9508 | 5.86 |
| city2 c1 blurred | ours 16 | 40.67 | 0.9513 | 5.79 |
| city2 c1 blurred | ours 16, past only | 40.67 | 0.9512 | 5.81 |
| city2 c1 blurred | ours 16, alone | 40.60 | 0.9506 | 5.96 |
| city2 c1 blurred | OIDN 4 | 40.11 | 0.9500 | 6.08 |
| city2 c1 blurred | OIDN 16 | 40.55 | 0.9512 | 5.83 |
| jail3 c1 sharp | noisy 4 | 27.26 | 0.4858 | 34.08 |
| jail3 c1 sharp | noisy 16 | 32.26 | 0.7236 | 19.20 |
| jail3 c1 sharp | ours 4 | 44.62 | 0.9808 | 3.90 |
| jail3 c1 sharp | ours 8 | 45.37 | 0.9817 | 3.72 |
| jail3 c1 sharp | ours 16 | 45.80 | 0.9822 | 3.61 |
| jail3 c1 sharp | ours 16, past only | 45.77 | 0.9821 | 3.63 |
| jail3 c1 sharp | ours 16, alone | 45.59 | 0.9816 | 3.84 |
| jail3 c1 sharp | OIDN 4 | 42.43 | 0.9742 | 4.99 |
| jail3 c1 sharp | OIDN 16 | 43.95 | 0.9774 | 4.39 |
| jail3 c1 blurred | noisy 4 | 26.58 | 0.4610 | 36.93 |
| jail3 c1 blurred | noisy 16 | 31.16 | 0.6933 | 21.33 |
| jail3 c1 blurred | ours 4 | 39.23 | 0.9361 | 7.66 |
| jail3 c1 blurred | ours 8 | 39.63 | 0.9372 | 7.40 |
| jail3 c1 blurred | ours 16 | 39.81 | 0.9379 | 7.30 |
| jail3 c1 blurred | ours 16, past only | 39.81 | 0.9378 | 7.31 |
| jail3 c1 blurred | ours 16, alone | 39.80 | 0.9375 | 7.41 |
| jail3 c1 blurred | OIDN 4 | 38.68 | 0.9324 | 8.33 |
| jail3 c1 blurred | OIDN 16 | 39.41 | 0.9353 | 7.72 |
| mine3 c1 sharp | noisy 4 | 26.46 | 0.5360 | 30.89 |
| mine3 c1 sharp | noisy 16 | 30.41 | 0.7240 | 19.36 |
| mine3 c1 sharp | ours 4 | 42.32 | 0.9806 | 3.40 |
| mine3 c1 sharp | ours 8 | 42.91 | 0.9815 | 3.26 |
| mine3 c1 sharp | ours 16 | 43.57 | 0.9823 | 3.15 |
| mine3 c1 sharp | ours 16, past only | 43.48 | 0.9819 | 3.23 |
| mine3 c1 sharp | ours 16, alone | 43.27 | 0.9803 | 3.72 |
| mine3 c1 sharp | OIDN 4 | 42.11 | 0.9801 | 3.77 |
| mine3 c1 sharp | OIDN 16 | 43.76 | 0.9823 | 3.29 |
| mine3 c1 blurred | noisy 4 | 26.39 | 0.5286 | 31.53 |
| mine3 c1 blurred | noisy 16 | 30.15 | 0.7147 | 20.03 |
| mine3 c1 blurred | ours 4 | 38.91 | 0.9543 | 5.76 |
| mine3 c1 blurred | ours 8 | 39.23 | 0.9553 | 5.64 |
| mine3 c1 blurred | ours 16 | 39.45 | 0.9559 | 5.56 |
| mine3 c1 blurred | ours 16, past only | 39.42 | 0.9557 | 5.59 |
| mine3 c1 blurred | ours 16, alone | 39.33 | 0.9543 | 5.91 |
| mine3 c1 blurred | OIDN 4 | 38.79 | 0.9549 | 5.95 |
| mine3 c1 blurred | OIDN 16 | 39.45 | 0.9565 | 5.60 |
| power2 c1 sharp | noisy 4 | 21.59 | 0.2892 | 59.31 |
| power2 c1 sharp | noisy 16 | 25.11 | 0.4439 | 40.73 |
| power2 c1 sharp | ours 4 | 37.70 | 0.9179 | 8.42 |
| power2 c1 sharp | ours 8 | 38.27 | 0.9212 | 8.18 |
| power2 c1 sharp | ours 16 | 38.82 | 0.9243 | 7.99 |
| power2 c1 sharp | ours 16, past only | 38.66 | 0.9231 | 8.16 |
| power2 c1 sharp | ours 16, alone | 38.41 | 0.9202 | 8.89 |
| power2 c1 sharp | OIDN 4 | 37.19 | 0.9186 | 9.21 |
| power2 c1 sharp | OIDN 16 | 38.61 | 0.9251 | 8.43 |
| power2 c1 blurred | noisy 4 | 21.46 | 0.2742 | 61.31 |
| power2 c1 blurred | noisy 16 | 24.82 | 0.4168 | 43.14 |
| power2 c1 blurred | ours 4 | 33.92 | 0.8065 | 14.88 |
| power2 c1 blurred | ours 8 | 34.20 | 0.8089 | 14.62 |
| power2 c1 blurred | ours 16 | 34.38 | 0.8110 | 14.43 |
| power2 c1 blurred | ours 16, past only | 34.37 | 0.8106 | 14.46 |
| power2 c1 blurred | ours 16, alone | 34.33 | 0.8094 | 14.84 |
| power2 c1 blurred | OIDN 4 | 33.92 | 0.8099 | 15.16 |
| power2 c1 blurred | OIDN 16 | 34.42 | 0.8135 | 14.44 |
| q2dm4 c1 sharp | noisy 4 | 25.10 | 0.4118 | 40.96 |
| q2dm4 c1 sharp | noisy 16 | 29.05 | 0.6094 | 25.09 |
| q2dm4 c1 sharp | ours 4 | 39.87 | 0.9425 | 6.47 |
| q2dm4 c1 sharp | ours 8 | 40.25 | 0.9441 | 6.22 |
| q2dm4 c1 sharp | ours 16 | 40.59 | 0.9453 | 6.03 |
| q2dm4 c1 sharp | ours 16, past only | 40.56 | 0.9451 | 6.09 |
| q2dm4 c1 sharp | ours 16, alone | 40.50 | 0.9445 | 6.29 |
| q2dm4 c1 sharp | OIDN 4 | 40.17 | 0.9452 | 6.47 |
| q2dm4 c1 sharp | OIDN 16 | 40.89 | 0.9477 | 5.98 |
| q2dm4 c1 blurred | noisy 4 | 24.79 | 0.3947 | 43.13 |
| q2dm4 c1 blurred | noisy 16 | 28.52 | 0.5842 | 26.96 |
| q2dm4 c1 blurred | ours 4 | 36.04 | 0.8740 | 10.21 |
| q2dm4 c1 blurred | ours 8 | 36.25 | 0.8754 | 9.95 |
| q2dm4 c1 blurred | ours 16 | 36.40 | 0.8764 | 9.76 |
| q2dm4 c1 blurred | ours 16, past only | 36.39 | 0.8762 | 9.79 |
| q2dm4 c1 blurred | ours 16, alone | 36.38 | 0.8760 | 9.89 |
| q2dm4 c1 blurred | OIDN 4 | 36.06 | 0.8756 | 10.23 |
| q2dm4 c1 blurred | OIDN 16 | 36.44 | 0.8775 | 9.72 |
| ware2 c1 sharp | noisy 4 | 18.64 | 0.1888 | 64.19 |
| ware2 c1 sharp | noisy 16 | 21.15 | 0.2578 | 59.50 |
| ware2 c1 sharp | ours 4 | 31.40 | 0.7282 | 19.52 |
| ware2 c1 sharp | ours 8 | 32.18 | 0.7380 | 18.57 |
| ware2 c1 sharp | ours 16 | 32.92 | 0.7474 | 17.87 |
| ware2 c1 sharp | ours 16, past only | 32.91 | 0.7445 | 18.03 |
| ware2 c1 sharp | ours 16, alone | 29.98 | 0.6851 | 22.48 |
| ware2 c1 sharp | OIDN 4 | 33.16 | 0.7641 | 17.57 |
| ware2 c1 sharp | OIDN 16 | 33.64 | 0.7679 | 16.84 |
| ware2 c1 blurred | noisy 4 | 17.81 | 0.1751 | 79.51 |
| ware2 c1 blurred | noisy 16 | 19.82 | 0.2405 | 74.18 |
| ware2 c1 blurred | ours 4 | 26.93 | 0.4974 | 35.96 |
| ware2 c1 blurred | ours 8 | 27.30 | 0.5024 | 34.71 |
| ware2 c1 blurred | ours 16 | 27.53 | 0.5067 | 34.00 |
| ware2 c1 blurred | ours 16, past only | 27.50 | 0.5050 | 34.16 |
| ware2 c1 blurred | ours 16, alone | 26.41 | 0.4791 | 37.47 |
| ware2 c1 blurred | OIDN 4 | 27.45 | 0.5143 | 33.99 |
| ware2 c1 blurred | OIDN 16 | 27.76 | 0.5171 | 32.56 |
