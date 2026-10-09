## All test clips, sharp

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 23.30 | 0.4364 | 42.56 |
| noisy 16 | 26.74 | 0.6052 | 29.36 |
| ours 4 | 39.09 | 0.9422 | 6.22 |
| ours 8 | 39.79 | 0.9447 | 5.96 |
| ours 16 | 40.39 | 0.9466 | 5.77 |
| ours 16, past only | 40.22 | 0.9458 | 5.88 |
| ours 16, alone | 38.78 | 0.9375 | 6.85 |
| OIDN 4 | 38.91 | 0.9433 | 6.77 |
| OIDN 16 | 40.14 | 0.9471 | 6.13 |

## All test clips, blurred

| | PSNR | SSIM | flicker |
|---|---|---|---|
| noisy 4 | 22.92 | 0.4247 | 45.80 |
| noisy 16 | 26.21 | 0.5873 | 31.89 |
| ours 4 | 35.11 | 0.8650 | 11.16 |
| ours 8 | 35.52 | 0.8670 | 10.75 |
| ours 16 | 35.77 | 0.8685 | 10.53 |
| ours 16, past only | 35.73 | 0.8681 | 10.58 |
| ours 16, alone | 35.18 | 0.8628 | 11.22 |
| OIDN 4 | 35.06 | 0.8672 | 11.34 |
| OIDN 16 | 35.75 | 0.8699 | 10.59 |

## Motion blur: two ways, against frames rendered blurred (16 paths)

| | PSNR | SSIM | flicker |
|---|---|---|---|
| blurred frames denoised | 35.77 | 0.8685 | 10.53 |
| sharp frames denoised, then blurred | 27.77 | 0.8654 | 17.01 |
| sharp reference, then blurred | 27.87 | 0.8710 | 16.81 |

## PSNR by how far into a clip the frame is (sharp)

| frame | noisy 4 | noisy 16 | ours 4 | ours 8 | ours 16 | ours 16, past only | ours 16, alone | OIDN 4 | OIDN 16 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 22.21 | 25.63 | 38.80 | 39.35 | 39.81 | 39.55 | 39.40 | 38.71 | 39.70 |
| 2 | 22.47 | 25.89 | 39.33 | 39.79 | 40.19 | 40.03 | 39.61 | 38.84 | 39.94 |
| 3 | 23.05 | 26.38 | 39.82 | 40.26 | 40.66 | 40.48 | 39.96 | 39.31 | 40.41 |
| 4 | 22.34 | 25.79 | 39.14 | 39.61 | 40.03 | 39.90 | 39.45 | 38.72 | 39.79 |
| 5 | 22.53 | 25.97 | 39.45 | 39.89 | 40.30 | 40.15 | 39.60 | 38.90 | 39.97 |
| 6 | 23.38 | 26.59 | 39.63 | 40.12 | 40.58 | 40.74 | 40.11 | 39.47 | 40.59 |
| 7 | 24.18 | 27.78 | 38.19 | 39.29 | 40.19 | 39.69 | 36.35 | 38.57 | 40.02 |
| 8 | 24.14 | 27.86 | 37.44 | 38.79 | 39.97 | 39.85 | 36.09 | 38.73 | 40.12 |
| 9 | 24.74 | 28.11 | 40.29 | 40.87 | 41.35 | 41.18 | 41.04 | 39.64 | 40.88 |
| 10 | 24.04 | 27.78 | 39.24 | 39.93 | 40.55 | 40.35 | 38.64 | 38.74 | 40.20 |
| 11 | 23.92 | 27.44 | 39.48 | 40.23 | 40.89 | 40.67 | 39.00 | 38.77 | 40.18 |
| 12 | 23.55 | 26.79 | 39.07 | 39.79 | 40.40 | 40.38 | 39.03 | 38.66 | 40.08 |

## Each clip

| clip | | PSNR | SSIM | flicker |
|---|---|---|---|---|
| city2 c1 sharp | noisy 4 | 29.66 | 0.6882 | 22.01 |
| city2 c1 sharp | noisy 16 | 34.66 | 0.8514 | 12.25 |
| city2 c1 sharp | ours 4 | 46.96 | 0.9898 | 2.39 |
| city2 c1 sharp | ours 8 | 47.71 | 0.9904 | 2.29 |
| city2 c1 sharp | ours 16 | 48.20 | 0.9908 | 2.23 |
| city2 c1 sharp | ours 16, past only | 48.00 | 0.9906 | 2.29 |
| city2 c1 sharp | ours 16, alone | 47.51 | 0.9899 | 2.52 |
| city2 c1 sharp | OIDN 4 | 45.52 | 0.9883 | 2.76 |
| city2 c1 sharp | OIDN 16 | 47.20 | 0.9897 | 2.41 |
| city2 c1 blurred | noisy 4 | 29.56 | 0.6840 | 22.32 |
| city2 c1 blurred | noisy 16 | 34.33 | 0.8442 | 12.73 |
| city2 c1 blurred | ours 4 | 43.07 | 0.9729 | 4.12 |
| city2 c1 blurred | ours 8 | 43.35 | 0.9735 | 4.05 |
| city2 c1 blurred | ours 16 | 43.53 | 0.9739 | 4.01 |
| city2 c1 blurred | ours 16, past only | 43.47 | 0.9737 | 4.04 |
| city2 c1 blurred | ours 16, alone | 43.33 | 0.9731 | 4.20 |
| city2 c1 blurred | OIDN 4 | 42.53 | 0.9723 | 4.34 |
| city2 c1 blurred | OIDN 16 | 43.23 | 0.9735 | 4.09 |
| jail3 c1 sharp | noisy 4 | 29.23 | 0.5900 | 27.08 |
| jail3 c1 sharp | noisy 16 | 34.43 | 0.8089 | 14.63 |
| jail3 c1 sharp | ours 4 | 46.73 | 0.9887 | 2.88 |
| jail3 c1 sharp | ours 8 | 47.49 | 0.9894 | 2.74 |
| jail3 c1 sharp | ours 16 | 48.04 | 0.9899 | 2.64 |
| jail3 c1 sharp | ours 16, past only | 47.83 | 0.9897 | 2.67 |
| jail3 c1 sharp | ours 16, alone | 47.73 | 0.9891 | 2.87 |
| jail3 c1 sharp | OIDN 4 | 43.83 | 0.9827 | 3.97 |
| jail3 c1 sharp | OIDN 16 | 45.63 | 0.9852 | 3.45 |
| jail3 c1 blurred | noisy 4 | 28.63 | 0.5712 | 28.80 |
| jail3 c1 blurred | noisy 16 | 33.43 | 0.7886 | 15.97 |
| jail3 c1 blurred | ours 4 | 41.75 | 0.9643 | 5.60 |
| jail3 c1 blurred | ours 8 | 42.32 | 0.9652 | 5.29 |
| jail3 c1 blurred | ours 16 | 42.57 | 0.9657 | 5.20 |
| jail3 c1 blurred | ours 16, past only | 42.52 | 0.9656 | 5.22 |
| jail3 c1 blurred | ours 16, alone | 42.54 | 0.9652 | 5.31 |
| jail3 c1 blurred | OIDN 4 | 40.91 | 0.9610 | 6.25 |
| jail3 c1 blurred | OIDN 16 | 42.01 | 0.9633 | 5.63 |
| mine3 c1 sharp | noisy 4 | 27.10 | 0.5580 | 28.61 |
| mine3 c1 sharp | noisy 16 | 31.15 | 0.7409 | 17.99 |
| mine3 c1 sharp | ours 4 | 44.16 | 0.9871 | 2.70 |
| mine3 c1 sharp | ours 8 | 44.92 | 0.9880 | 2.59 |
| mine3 c1 sharp | ours 16 | 45.57 | 0.9886 | 2.50 |
| mine3 c1 sharp | ours 16, past only | 45.33 | 0.9881 | 2.61 |
| mine3 c1 sharp | ours 16, alone | 44.89 | 0.9863 | 3.17 |
| mine3 c1 sharp | OIDN 4 | 43.43 | 0.9864 | 3.15 |
| mine3 c1 sharp | OIDN 16 | 45.56 | 0.9885 | 2.65 |
| mine3 c1 blurred | noisy 4 | 27.08 | 0.5537 | 28.96 |
| mine3 c1 blurred | noisy 16 | 30.99 | 0.7353 | 18.41 |
| mine3 c1 blurred | ours 4 | 40.91 | 0.9705 | 4.44 |
| mine3 c1 blurred | ours 8 | 41.48 | 0.9714 | 4.33 |
| mine3 c1 blurred | ours 16 | 41.80 | 0.9720 | 4.25 |
| mine3 c1 blurred | ours 16, past only | 41.74 | 0.9717 | 4.31 |
| mine3 c1 blurred | ours 16, alone | 41.57 | 0.9701 | 4.69 |
| mine3 c1 blurred | OIDN 4 | 40.59 | 0.9706 | 4.70 |
| mine3 c1 blurred | OIDN 16 | 41.77 | 0.9724 | 4.30 |
| power2 c1 sharp | noisy 4 | 22.27 | 0.3351 | 53.12 |
| power2 c1 sharp | noisy 16 | 25.79 | 0.5102 | 36.00 |
| power2 c1 sharp | ours 4 | 38.63 | 0.9457 | 6.42 |
| power2 c1 sharp | ours 8 | 39.46 | 0.9496 | 6.21 |
| power2 c1 sharp | ours 16 | 40.34 | 0.9533 | 6.04 |
| power2 c1 sharp | ours 16, past only | 39.89 | 0.9510 | 6.29 |
| power2 c1 sharp | ours 16, alone | 39.39 | 0.9474 | 7.28 |
| power2 c1 sharp | OIDN 4 | 37.76 | 0.9434 | 7.69 |
| power2 c1 sharp | OIDN 16 | 39.83 | 0.9519 | 6.79 |
| power2 c1 blurred | noisy 4 | 22.25 | 0.3218 | 54.22 |
| power2 c1 blurred | noisy 16 | 25.69 | 0.4859 | 37.45 |
| power2 c1 blurred | ours 4 | 35.90 | 0.8747 | 11.45 |
| power2 c1 blurred | ours 8 | 36.40 | 0.8776 | 11.16 |
| power2 c1 blurred | ours 16 | 36.75 | 0.8801 | 10.95 |
| power2 c1 blurred | ours 16, past only | 36.71 | 0.8794 | 11.01 |
| power2 c1 blurred | ours 16, alone | 36.60 | 0.8779 | 11.48 |
| power2 c1 blurred | OIDN 4 | 35.83 | 0.8774 | 11.92 |
| power2 c1 blurred | OIDN 16 | 36.85 | 0.8827 | 11.03 |
| q2dm4 c1 sharp | noisy 4 | 21.47 | 0.2282 | 66.06 |
| q2dm4 c1 sharp | noisy 16 | 25.35 | 0.4022 | 42.01 |
| q2dm4 c1 sharp | ours 4 | 37.10 | 0.8833 | 10.35 |
| q2dm4 c1 sharp | ours 8 | 37.32 | 0.8848 | 10.04 |
| q2dm4 c1 sharp | ours 16 | 37.58 | 0.8862 | 9.83 |
| q2dm4 c1 sharp | ours 16, past only | 37.54 | 0.8859 | 9.90 |
| q2dm4 c1 sharp | ours 16, alone | 37.53 | 0.8853 | 10.09 |
| q2dm4 c1 sharp | OIDN 4 | 37.10 | 0.8847 | 10.52 |
| q2dm4 c1 sharp | OIDN 16 | 37.67 | 0.8876 | 9.93 |
| q2dm4 c1 blurred | noisy 4 | 20.94 | 0.2079 | 72.65 |
| q2dm4 c1 blurred | noisy 16 | 24.70 | 0.3671 | 46.45 |
| q2dm4 c1 blurred | ours 4 | 32.51 | 0.7345 | 17.82 |
| q2dm4 c1 blurred | ours 8 | 32.67 | 0.7360 | 17.51 |
| q2dm4 c1 blurred | ours 16 | 32.77 | 0.7372 | 17.28 |
| q2dm4 c1 blurred | ours 16, past only | 32.76 | 0.7370 | 17.33 |
| q2dm4 c1 blurred | ours 16, alone | 32.76 | 0.7367 | 17.42 |
| q2dm4 c1 blurred | OIDN 4 | 32.46 | 0.7357 | 17.94 |
| q2dm4 c1 blurred | OIDN 16 | 32.74 | 0.7379 | 17.36 |
| ware2 c1 sharp | noisy 4 | 19.68 | 0.2186 | 58.50 |
| ware2 c1 sharp | noisy 16 | 22.46 | 0.3176 | 53.27 |
| ware2 c1 sharp | ours 4 | 34.85 | 0.8584 | 12.59 |
| ware2 c1 sharp | ours 8 | 35.80 | 0.8659 | 11.87 |
| ware2 c1 sharp | ours 16 | 36.53 | 0.8712 | 11.40 |
| ware2 c1 sharp | ours 16, past only | 36.39 | 0.8693 | 11.51 |
| ware2 c1 sharp | ours 16, alone | 33.61 | 0.8268 | 15.19 |
| ware2 c1 sharp | OIDN 4 | 35.31 | 0.8745 | 12.55 |
| ware2 c1 sharp | OIDN 16 | 36.36 | 0.8795 | 11.56 |
| ware2 c1 blurred | noisy 4 | 19.15 | 0.2096 | 67.87 |
| ware2 c1 blurred | noisy 16 | 21.77 | 0.3024 | 60.34 |
| ware2 c1 blurred | ours 4 | 30.77 | 0.6730 | 23.51 |
| ware2 c1 blurred | ours 8 | 31.33 | 0.6783 | 22.17 |
| ware2 c1 blurred | ours 16 | 31.64 | 0.6822 | 21.53 |
| ware2 c1 blurred | ours 16, past only | 31.59 | 0.6811 | 21.58 |
| ware2 c1 blurred | ours 16, alone | 30.46 | 0.6538 | 24.25 |
| ware2 c1 blurred | OIDN 4 | 30.88 | 0.6859 | 22.89 |
| ware2 c1 blurred | OIDN 16 | 31.68 | 0.6896 | 21.12 |
