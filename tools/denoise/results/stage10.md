## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.30 | 0.4364 | 42.56 |
| noisy 16 | 26.74 | 0.6052 | 29.36 |
| ours 4 | 39.47 | 0.9422 | 6.21 |
| ours 8 | 40.14 | 0.9452 | 5.92 |
| ours 16 | 40.69 | 0.9473 | 5.72 |
| ours 16, past only | 40.53 | 0.9466 | 5.82 |
| ours 16, alone | 39.38 | 0.9394 | 6.71 |
| OIDN 4 | 38.91 | 0.9433 | 6.77 |
| OIDN 16 | 40.14 | 0.9471 | 6.13 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.92 | 0.4247 | 45.80 |
| noisy 16 | 26.21 | 0.5873 | 31.89 |
| ours 4 | 35.23 | 0.8649 | 11.12 |
| ours 8 | 35.62 | 0.8672 | 10.71 |
| ours 16 | 35.84 | 0.8688 | 10.50 |
| ours 16, past only | 35.81 | 0.8685 | 10.54 |
| ours 16, alone | 35.39 | 0.8637 | 11.13 |
| OIDN 4 | 35.06 | 0.8672 | 11.34 |
| OIDN 16 | 35.75 | 0.8699 | 10.59 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 35.84 | 0.8688 | 10.50 |
| sharp frames denoised, then blurred | 27.80 | 0.8656 | 17.00 |
| sharp reference, then blurred | 27.87 | 0.8710 | 16.81 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.21 | 25.63 | 38.93 | 39.51 | 39.95 | 39.70 | 39.69 | 38.71 | 39.70 |
| 2 | 22.47 | 25.89 | 39.44 | 39.94 | 40.33 | 40.16 | 39.92 | 38.84 | 39.94 |
| 3 | 23.05 | 26.38 | 39.93 | 40.42 | 40.81 | 40.64 | 40.32 | 39.31 | 40.41 |
| 4 | 22.34 | 25.79 | 39.23 | 39.76 | 40.17 | 40.04 | 39.79 | 38.72 | 39.79 |
| 5 | 22.53 | 25.97 | 39.56 | 40.06 | 40.44 | 40.29 | 39.98 | 38.90 | 39.97 |
| 6 | 23.38 | 26.59 | 39.99 | 40.50 | 40.92 | 40.90 | 40.55 | 39.47 | 40.59 |
| 7 | 24.18 | 27.78 | 38.94 | 39.88 | 40.69 | 40.48 | 37.48 | 38.57 | 40.02 |
| 8 | 24.14 | 27.86 | 39.07 | 40.07 | 40.89 | 40.65 | 37.60 | 38.73 | 40.12 |
| 9 | 24.74 | 28.11 | 40.56 | 41.15 | 41.62 | 41.46 | 41.27 | 39.64 | 40.88 |
| 10 | 24.04 | 27.78 | 39.37 | 40.22 | 40.93 | 40.75 | 38.98 | 38.74 | 40.20 |
| 11 | 23.92 | 27.44 | 39.55 | 40.36 | 41.04 | 40.87 | 39.29 | 38.77 | 40.18 |
| 12 | 23.55 | 26.79 | 39.32 | 40.07 | 40.68 | 40.68 | 39.37 | 38.66 | 40.08 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 29.66 | 0.6882 | 22.01 |
| city2 c1 sharp | noisy 16 | 34.66 | 0.8514 | 12.25 |
| city2 c1 sharp | ours 4 | 46.96 | 0.9899 | 2.40 |
| city2 c1 sharp | ours 8 | 47.70 | 0.9904 | 2.30 |
| city2 c1 sharp | ours 16 | 48.21 | 0.9908 | 2.24 |
| city2 c1 sharp | ours 16, past only | 48.02 | 0.9906 | 2.29 |
| city2 c1 sharp | ours 16, alone | 47.55 | 0.9900 | 2.51 |
| city2 c1 sharp | OIDN 4 | 45.52 | 0.9883 | 2.76 |
| city2 c1 sharp | OIDN 16 | 47.20 | 0.9897 | 2.41 |
| city2 c1 blurred | noisy 4 | 29.56 | 0.6840 | 22.32 |
| city2 c1 blurred | noisy 16 | 34.33 | 0.8442 | 12.73 |
| city2 c1 blurred | ours 4 | 43.06 | 0.9730 | 4.13 |
| city2 c1 blurred | ours 8 | 43.33 | 0.9735 | 4.05 |
| city2 c1 blurred | ours 16 | 43.53 | 0.9739 | 4.00 |
| city2 c1 blurred | ours 16, past only | 43.48 | 0.9737 | 4.04 |
| city2 c1 blurred | ours 16, alone | 43.34 | 0.9732 | 4.19 |
| city2 c1 blurred | OIDN 4 | 42.53 | 0.9723 | 4.34 |
| city2 c1 blurred | OIDN 16 | 43.23 | 0.9735 | 4.09 |
| jail3 c1 sharp | noisy 4 | 29.23 | 0.5900 | 27.08 |
| jail3 c1 sharp | noisy 16 | 34.43 | 0.8089 | 14.63 |
| jail3 c1 sharp | ours 4 | 46.95 | 0.9888 | 2.87 |
| jail3 c1 sharp | ours 8 | 47.69 | 0.9895 | 2.73 |
| jail3 c1 sharp | ours 16 | 48.18 | 0.9899 | 2.64 |
| jail3 c1 sharp | ours 16, past only | 47.98 | 0.9897 | 2.67 |
| jail3 c1 sharp | ours 16, alone | 47.83 | 0.9892 | 2.87 |
| jail3 c1 sharp | OIDN 4 | 43.83 | 0.9827 | 3.97 |
| jail3 c1 sharp | OIDN 16 | 45.63 | 0.9852 | 3.45 |
| jail3 c1 blurred | noisy 4 | 28.63 | 0.5712 | 28.80 |
| jail3 c1 blurred | noisy 16 | 33.43 | 0.7886 | 15.97 |
| jail3 c1 blurred | ours 4 | 41.87 | 0.9644 | 5.60 |
| jail3 c1 blurred | ours 8 | 42.39 | 0.9653 | 5.29 |
| jail3 c1 blurred | ours 16 | 42.59 | 0.9658 | 5.20 |
| jail3 c1 blurred | ours 16, past only | 42.54 | 0.9656 | 5.22 |
| jail3 c1 blurred | ours 16, alone | 42.55 | 0.9652 | 5.32 |
| jail3 c1 blurred | OIDN 4 | 40.91 | 0.9610 | 6.25 |
| jail3 c1 blurred | OIDN 16 | 42.01 | 0.9633 | 5.63 |
| mine3 c1 sharp | noisy 4 | 27.10 | 0.5580 | 28.61 |
| mine3 c1 sharp | noisy 16 | 31.15 | 0.7409 | 17.99 |
| mine3 c1 sharp | ours 4 | 44.29 | 0.9873 | 2.70 |
| mine3 c1 sharp | ours 8 | 45.16 | 0.9882 | 2.58 |
| mine3 c1 sharp | ours 16 | 45.78 | 0.9888 | 2.49 |
| mine3 c1 sharp | ours 16, past only | 45.54 | 0.9884 | 2.60 |
| mine3 c1 sharp | ours 16, alone | 45.07 | 0.9867 | 3.16 |
| mine3 c1 sharp | OIDN 4 | 43.43 | 0.9864 | 3.15 |
| mine3 c1 sharp | OIDN 16 | 45.56 | 0.9885 | 2.65 |
| mine3 c1 blurred | noisy 4 | 27.08 | 0.5537 | 28.96 |
| mine3 c1 blurred | noisy 16 | 30.99 | 0.7353 | 18.41 |
| mine3 c1 blurred | ours 4 | 40.95 | 0.9707 | 4.44 |
| mine3 c1 blurred | ours 8 | 41.54 | 0.9716 | 4.32 |
| mine3 c1 blurred | ours 16 | 41.84 | 0.9722 | 4.24 |
| mine3 c1 blurred | ours 16, past only | 41.79 | 0.9719 | 4.30 |
| mine3 c1 blurred | ours 16, alone | 41.62 | 0.9704 | 4.69 |
| mine3 c1 blurred | OIDN 4 | 40.59 | 0.9706 | 4.70 |
| mine3 c1 blurred | OIDN 16 | 41.77 | 0.9724 | 4.30 |
| power2 c1 sharp | noisy 4 | 22.27 | 0.3351 | 53.12 |
| power2 c1 sharp | noisy 16 | 25.79 | 0.5102 | 36.00 |
| power2 c1 sharp | ours 4 | 38.80 | 0.9464 | 6.40 |
| power2 c1 sharp | ours 8 | 39.64 | 0.9503 | 6.16 |
| power2 c1 sharp | ours 16 | 40.49 | 0.9539 | 5.99 |
| power2 c1 sharp | ours 16, past only | 40.06 | 0.9518 | 6.22 |
| power2 c1 sharp | ours 16, alone | 39.59 | 0.9487 | 7.18 |
| power2 c1 sharp | OIDN 4 | 37.76 | 0.9434 | 7.69 |
| power2 c1 sharp | OIDN 16 | 39.83 | 0.9519 | 6.79 |
| power2 c1 blurred | noisy 4 | 22.25 | 0.3218 | 54.22 |
| power2 c1 blurred | noisy 16 | 25.69 | 0.4859 | 37.45 |
| power2 c1 blurred | ours 4 | 35.95 | 0.8752 | 11.48 |
| power2 c1 blurred | ours 8 | 36.45 | 0.8781 | 11.16 |
| power2 c1 blurred | ours 16 | 36.78 | 0.8804 | 10.95 |
| power2 c1 blurred | ours 16, past only | 36.74 | 0.8797 | 10.99 |
| power2 c1 blurred | ours 16, alone | 36.66 | 0.8786 | 11.43 |
| power2 c1 blurred | OIDN 4 | 35.83 | 0.8774 | 11.92 |
| power2 c1 blurred | OIDN 16 | 36.85 | 0.8827 | 11.03 |
| q2dm4 c1 sharp | noisy 4 | 21.47 | 0.2282 | 66.06 |
| q2dm4 c1 sharp | noisy 16 | 25.35 | 0.4022 | 42.01 |
| q2dm4 c1 sharp | ours 4 | 37.15 | 0.8835 | 10.38 |
| q2dm4 c1 sharp | ours 8 | 37.47 | 0.8854 | 10.04 |
| q2dm4 c1 sharp | ours 16 | 37.71 | 0.8867 | 9.82 |
| q2dm4 c1 sharp | ours 16, past only | 37.68 | 0.8865 | 9.89 |
| q2dm4 c1 sharp | ours 16, alone | 37.66 | 0.8862 | 10.06 |
| q2dm4 c1 sharp | OIDN 4 | 37.10 | 0.8847 | 10.52 |
| q2dm4 c1 sharp | OIDN 16 | 37.67 | 0.8876 | 9.93 |
| q2dm4 c1 blurred | noisy 4 | 20.94 | 0.2079 | 72.65 |
| q2dm4 c1 blurred | noisy 16 | 24.70 | 0.3671 | 46.45 |
| q2dm4 c1 blurred | ours 4 | 32.54 | 0.7346 | 17.82 |
| q2dm4 c1 blurred | ours 8 | 32.69 | 0.7363 | 17.51 |
| q2dm4 c1 blurred | ours 16 | 32.79 | 0.7374 | 17.28 |
| q2dm4 c1 blurred | ours 16, past only | 32.78 | 0.7372 | 17.33 |
| q2dm4 c1 blurred | ours 16, alone | 32.78 | 0.7371 | 17.40 |
| q2dm4 c1 blurred | OIDN 4 | 32.46 | 0.7357 | 17.94 |
| q2dm4 c1 blurred | OIDN 16 | 32.74 | 0.7379 | 17.36 |
| ware2 c1 sharp | noisy 4 | 19.68 | 0.2186 | 58.50 |
| ware2 c1 sharp | noisy 16 | 22.46 | 0.3176 | 53.27 |
| ware2 c1 sharp | ours 4 | 35.58 | 0.8574 | 12.51 |
| ware2 c1 sharp | ours 8 | 36.41 | 0.8671 | 11.71 |
| ware2 c1 sharp | ours 16 | 37.06 | 0.8738 | 11.13 |
| ware2 c1 sharp | ours 16, past only | 36.94 | 0.8727 | 11.25 |
| ware2 c1 sharp | ours 16, alone | 34.62 | 0.8359 | 14.49 |
| ware2 c1 sharp | OIDN 4 | 35.31 | 0.8745 | 12.55 |
| ware2 c1 sharp | OIDN 16 | 36.36 | 0.8795 | 11.56 |
| ware2 c1 blurred | noisy 4 | 19.15 | 0.2096 | 67.87 |
| ware2 c1 blurred | noisy 16 | 21.77 | 0.3024 | 60.34 |
| ware2 c1 blurred | ours 4 | 31.00 | 0.6717 | 23.26 |
| ware2 c1 blurred | ours 8 | 31.51 | 0.6787 | 21.95 |
| ware2 c1 blurred | ours 16 | 31.78 | 0.6835 | 21.31 |
| ware2 c1 blurred | ours 16, past only | 31.74 | 0.6829 | 21.38 |
| ware2 c1 blurred | ours 16, alone | 30.86 | 0.6578 | 23.76 |
| ware2 c1 blurred | OIDN 4 | 30.88 | 0.6859 | 22.89 |
| ware2 c1 blurred | OIDN 16 | 31.68 | 0.6896 | 21.12 |
