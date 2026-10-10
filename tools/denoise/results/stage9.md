## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.30 | 0.4364 | 42.56 |
| noisy 16 | 26.74 | 0.6052 | 29.36 |
| ours 4 | 39.48 | 0.9424 | 6.20 |
| ours 8 | 40.12 | 0.9451 | 5.94 |
| ours 16 | 40.63 | 0.9471 | 5.75 |
| ours 16, past only | 40.48 | 0.9463 | 5.85 |
| ours 16, alone | 39.31 | 0.9389 | 6.76 |
| OIDN 4 | 38.91 | 0.9433 | 6.77 |
| OIDN 16 | 40.14 | 0.9471 | 6.13 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.92 | 0.4247 | 45.80 |
| noisy 16 | 26.21 | 0.5873 | 31.89 |
| ours 4 | 35.23 | 0.8650 | 11.13 |
| ours 8 | 35.61 | 0.8672 | 10.73 |
| ours 16 | 35.82 | 0.8687 | 10.52 |
| ours 16, past only | 35.79 | 0.8683 | 10.57 |
| ours 16, alone | 35.36 | 0.8634 | 11.16 |
| OIDN 4 | 35.06 | 0.8672 | 11.34 |
| OIDN 16 | 35.75 | 0.8699 | 10.59 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 35.82 | 0.8687 | 10.52 |
| sharp frames denoised, then blurred | 27.79 | 0.8656 | 17.01 |
| sharp reference, then blurred | 27.87 | 0.8710 | 16.81 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.21 | 25.63 | 38.90 | 39.47 | 39.90 | 39.65 | 39.64 | 38.71 | 39.70 |
| 2 | 22.47 | 25.89 | 39.43 | 39.93 | 40.30 | 40.14 | 39.88 | 38.84 | 39.94 |
| 3 | 23.05 | 26.38 | 39.92 | 40.41 | 40.78 | 40.62 | 40.28 | 39.31 | 40.41 |
| 4 | 22.34 | 25.79 | 39.23 | 39.74 | 40.14 | 40.02 | 39.75 | 38.72 | 39.79 |
| 5 | 22.53 | 25.97 | 39.55 | 40.04 | 40.41 | 40.26 | 39.94 | 38.90 | 39.97 |
| 6 | 23.38 | 26.59 | 39.92 | 40.45 | 40.89 | 40.87 | 40.51 | 39.47 | 40.59 |
| 7 | 24.18 | 27.78 | 39.11 | 39.90 | 40.59 | 40.38 | 37.40 | 38.57 | 40.02 |
| 8 | 24.14 | 27.86 | 39.15 | 40.07 | 40.80 | 40.56 | 37.48 | 38.73 | 40.12 |
| 9 | 24.74 | 28.11 | 40.49 | 41.09 | 41.55 | 41.40 | 41.21 | 39.64 | 40.88 |
| 10 | 24.04 | 27.78 | 39.43 | 40.20 | 40.86 | 40.70 | 38.87 | 38.74 | 40.20 |
| 11 | 23.92 | 27.44 | 39.57 | 40.33 | 40.99 | 40.81 | 39.17 | 38.77 | 40.18 |
| 12 | 23.55 | 26.79 | 39.25 | 39.97 | 40.58 | 40.57 | 39.25 | 38.66 | 40.08 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 29.66 | 0.6882 | 22.01 |
| city2 c1 sharp | noisy 16 | 34.66 | 0.8514 | 12.25 |
| city2 c1 sharp | ours 4 | 46.98 | 0.9898 | 2.40 |
| city2 c1 sharp | ours 8 | 47.69 | 0.9904 | 2.31 |
| city2 c1 sharp | ours 16 | 48.19 | 0.9907 | 2.25 |
| city2 c1 sharp | ours 16, past only | 47.99 | 0.9905 | 2.30 |
| city2 c1 sharp | ours 16, alone | 47.53 | 0.9899 | 2.53 |
| city2 c1 sharp | OIDN 4 | 45.52 | 0.9883 | 2.76 |
| city2 c1 sharp | OIDN 16 | 47.20 | 0.9897 | 2.41 |
| city2 c1 blurred | noisy 4 | 29.56 | 0.6840 | 22.32 |
| city2 c1 blurred | noisy 16 | 34.33 | 0.8442 | 12.73 |
| city2 c1 blurred | ours 4 | 43.07 | 0.9729 | 4.13 |
| city2 c1 blurred | ours 8 | 43.34 | 0.9735 | 4.06 |
| city2 c1 blurred | ours 16 | 43.53 | 0.9739 | 4.01 |
| city2 c1 blurred | ours 16, past only | 43.47 | 0.9737 | 4.05 |
| city2 c1 blurred | ours 16, alone | 43.33 | 0.9731 | 4.20 |
| city2 c1 blurred | OIDN 4 | 42.53 | 0.9723 | 4.34 |
| city2 c1 blurred | OIDN 16 | 43.23 | 0.9735 | 4.09 |
| jail3 c1 sharp | noisy 4 | 29.23 | 0.5900 | 27.08 |
| jail3 c1 sharp | noisy 16 | 34.43 | 0.8089 | 14.63 |
| jail3 c1 sharp | ours 4 | 46.94 | 0.9888 | 2.88 |
| jail3 c1 sharp | ours 8 | 47.67 | 0.9895 | 2.74 |
| jail3 c1 sharp | ours 16 | 48.16 | 0.9899 | 2.64 |
| jail3 c1 sharp | ours 16, past only | 47.95 | 0.9897 | 2.67 |
| jail3 c1 sharp | ours 16, alone | 47.81 | 0.9892 | 2.87 |
| jail3 c1 sharp | OIDN 4 | 43.83 | 0.9827 | 3.97 |
| jail3 c1 sharp | OIDN 16 | 45.63 | 0.9852 | 3.45 |
| jail3 c1 blurred | noisy 4 | 28.63 | 0.5712 | 28.80 |
| jail3 c1 blurred | noisy 16 | 33.43 | 0.7886 | 15.97 |
| jail3 c1 blurred | ours 4 | 41.87 | 0.9643 | 5.61 |
| jail3 c1 blurred | ours 8 | 42.39 | 0.9652 | 5.29 |
| jail3 c1 blurred | ours 16 | 42.58 | 0.9657 | 5.20 |
| jail3 c1 blurred | ours 16, past only | 42.54 | 0.9656 | 5.22 |
| jail3 c1 blurred | ours 16, alone | 42.55 | 0.9652 | 5.32 |
| jail3 c1 blurred | OIDN 4 | 40.91 | 0.9610 | 6.25 |
| jail3 c1 blurred | OIDN 16 | 42.01 | 0.9633 | 5.63 |
| mine3 c1 sharp | noisy 4 | 27.10 | 0.5580 | 28.61 |
| mine3 c1 sharp | noisy 16 | 31.15 | 0.7409 | 17.99 |
| mine3 c1 sharp | ours 4 | 44.27 | 0.9872 | 2.71 |
| mine3 c1 sharp | ours 8 | 45.13 | 0.9881 | 2.59 |
| mine3 c1 sharp | ours 16 | 45.73 | 0.9887 | 2.50 |
| mine3 c1 sharp | ours 16, past only | 45.49 | 0.9882 | 2.62 |
| mine3 c1 sharp | ours 16, alone | 45.03 | 0.9865 | 3.18 |
| mine3 c1 sharp | OIDN 4 | 43.43 | 0.9864 | 3.15 |
| mine3 c1 sharp | OIDN 16 | 45.56 | 0.9885 | 2.65 |
| mine3 c1 blurred | noisy 4 | 27.08 | 0.5537 | 28.96 |
| mine3 c1 blurred | noisy 16 | 30.99 | 0.7353 | 18.41 |
| mine3 c1 blurred | ours 4 | 40.91 | 0.9706 | 4.45 |
| mine3 c1 blurred | ours 8 | 41.51 | 0.9715 | 4.33 |
| mine3 c1 blurred | ours 16 | 41.81 | 0.9721 | 4.25 |
| mine3 c1 blurred | ours 16, past only | 41.76 | 0.9717 | 4.32 |
| mine3 c1 blurred | ours 16, alone | 41.59 | 0.9702 | 4.70 |
| mine3 c1 blurred | OIDN 4 | 40.59 | 0.9706 | 4.70 |
| mine3 c1 blurred | OIDN 16 | 41.77 | 0.9724 | 4.30 |
| power2 c1 sharp | noisy 4 | 22.27 | 0.3351 | 53.12 |
| power2 c1 sharp | noisy 16 | 25.79 | 0.5102 | 36.00 |
| power2 c1 sharp | ours 4 | 38.76 | 0.9461 | 6.41 |
| power2 c1 sharp | ours 8 | 39.59 | 0.9501 | 6.18 |
| power2 c1 sharp | ours 16 | 40.46 | 0.9537 | 6.01 |
| power2 c1 sharp | ours 16, past only | 40.04 | 0.9517 | 6.25 |
| power2 c1 sharp | ours 16, alone | 39.57 | 0.9485 | 7.20 |
| power2 c1 sharp | OIDN 4 | 37.76 | 0.9434 | 7.69 |
| power2 c1 sharp | OIDN 16 | 39.83 | 0.9519 | 6.79 |
| power2 c1 blurred | noisy 4 | 22.25 | 0.3218 | 54.22 |
| power2 c1 blurred | noisy 16 | 25.69 | 0.4859 | 37.45 |
| power2 c1 blurred | ours 4 | 35.94 | 0.8750 | 11.46 |
| power2 c1 blurred | ours 8 | 36.44 | 0.8780 | 11.15 |
| power2 c1 blurred | ours 16 | 36.78 | 0.8804 | 10.95 |
| power2 c1 blurred | ours 16, past only | 36.73 | 0.8797 | 11.00 |
| power2 c1 blurred | ours 16, alone | 36.64 | 0.8784 | 11.44 |
| power2 c1 blurred | OIDN 4 | 35.83 | 0.8774 | 11.92 |
| power2 c1 blurred | OIDN 16 | 36.85 | 0.8827 | 11.03 |
| q2dm4 c1 sharp | noisy 4 | 21.47 | 0.2282 | 66.06 |
| q2dm4 c1 sharp | noisy 16 | 25.35 | 0.4022 | 42.01 |
| q2dm4 c1 sharp | ours 4 | 37.14 | 0.8832 | 10.40 |
| q2dm4 c1 sharp | ours 8 | 37.44 | 0.8851 | 10.07 |
| q2dm4 c1 sharp | ours 16 | 37.67 | 0.8863 | 9.86 |
| q2dm4 c1 sharp | ours 16, past only | 37.64 | 0.8860 | 9.93 |
| q2dm4 c1 sharp | ours 16, alone | 37.61 | 0.8856 | 10.10 |
| q2dm4 c1 sharp | OIDN 4 | 37.10 | 0.8847 | 10.52 |
| q2dm4 c1 sharp | OIDN 16 | 37.67 | 0.8876 | 9.93 |
| q2dm4 c1 blurred | noisy 4 | 20.94 | 0.2079 | 72.65 |
| q2dm4 c1 blurred | noisy 16 | 24.70 | 0.3671 | 46.45 |
| q2dm4 c1 blurred | ours 4 | 32.53 | 0.7344 | 17.84 |
| q2dm4 c1 blurred | ours 8 | 32.68 | 0.7361 | 17.53 |
| q2dm4 c1 blurred | ours 16 | 32.78 | 0.7372 | 17.30 |
| q2dm4 c1 blurred | ours 16, past only | 32.78 | 0.7370 | 17.35 |
| q2dm4 c1 blurred | ours 16, alone | 32.77 | 0.7368 | 17.43 |
| q2dm4 c1 blurred | OIDN 4 | 32.46 | 0.7357 | 17.94 |
| q2dm4 c1 blurred | OIDN 16 | 32.74 | 0.7379 | 17.36 |
| ware2 c1 sharp | noisy 4 | 19.68 | 0.2186 | 58.50 |
| ware2 c1 sharp | noisy 16 | 22.46 | 0.3176 | 53.27 |
| ware2 c1 sharp | ours 4 | 35.64 | 0.8595 | 12.41 |
| ware2 c1 sharp | ours 8 | 36.40 | 0.8674 | 11.72 |
| ware2 c1 sharp | ours 16 | 36.97 | 0.8730 | 11.22 |
| ware2 c1 sharp | ours 16, past only | 36.85 | 0.8716 | 11.33 |
| ware2 c1 sharp | ours 16, alone | 34.51 | 0.8340 | 14.67 |
| ware2 c1 sharp | OIDN 4 | 35.31 | 0.8745 | 12.55 |
| ware2 c1 sharp | OIDN 16 | 36.36 | 0.8795 | 11.56 |
| ware2 c1 blurred | noisy 4 | 19.15 | 0.2096 | 67.87 |
| ware2 c1 blurred | noisy 16 | 21.77 | 0.3024 | 60.34 |
| ware2 c1 blurred | ours 4 | 31.01 | 0.6726 | 23.29 |
| ware2 c1 blurred | ours 8 | 31.50 | 0.6786 | 22.00 |
| ware2 c1 blurred | ours 16 | 31.76 | 0.6830 | 21.40 |
| ware2 c1 blurred | ours 16, past only | 31.72 | 0.6821 | 21.46 |
| ware2 c1 blurred | ours 16, alone | 30.80 | 0.6566 | 23.89 |
| ware2 c1 blurred | OIDN 4 | 30.88 | 0.6859 | 22.89 |
| ware2 c1 blurred | OIDN 16 | 31.68 | 0.6896 | 21.12 |
