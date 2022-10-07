#!/bin/bash
RASP_HOST=192.168.0.9
CROSS_COMPILE=/root/toolchain/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

SRC="${SRC} main.c"
SRC="${SRC} gpio.c"
SRC="${SRC} touch.c"
SRC="${SRC} led_flash.c"

${CROSS_COMPILE}gcc -o iotest ${SRC} && \
scp iotest root@${RASP_HOST}:/root/ || \
echo "build failed"
