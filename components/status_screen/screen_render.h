#ifndef SCREEN_RENDER_H
#define SCREEN_RENDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "screen_snapshot.h"

#define SCREEN_W 240
#define SCREEN_H 135
#define SCREEN_STRIP_H 27
#define C_OK  0x07E0   /* green */
#define C_BAD 0xF800   /* red   */

/* Render rows [y0, y0+h) of the screen into px (h*SCREEN_W RGB565). swap_bytes stores each
   pixel big-endian (what the ST7789 wants over SPI on a little-endian MCU). */
void screen_render_strip(const screen_snapshot_t *s, uint16_t *px, int y0, int h, bool swap_bytes);

/* Copy src to dst (cap chars incl. NUL): bytes outside 0x20..0x7E become '?', text longer
   than cap-1 is truncated with "..", an empty string becomes "-". */
void screen_fit_text(char *dst, size_t cap, const char *src);
#endif
