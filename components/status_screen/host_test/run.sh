#!/bin/sh
set -e
cd "$(dirname "$0")"
${CC:-gcc} -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=undefined -o test_render test_render.c ../screen_render.c -lm
./test_render
${PYTHON:-python3} to_png.py
