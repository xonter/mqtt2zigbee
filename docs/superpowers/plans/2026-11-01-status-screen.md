# Status Screen (Waveshare ESP32-C6-GEEK) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show setup info or Wi-Fi/MQTT/Zigbee status with icons on the GEEK's 240x135 ST7789 LCD, as an opt-in build variant.

**Architecture:** New component `components/status_screen`. A pure-C renderer (`screen_render.c`) turns a `screen_snapshot_t` into RGB565 strips and is unit-tested on the host. A thin ESP-IDF driver (`status_screen.c`) owns SPI/ST7789 (core `esp_lcd_new_panel_st7789`), backlight PWM and a ~1 s task that asks `main` for a snapshot through a callback and redraws only on change. Everything is behind Kconfig `STATUS_SCREEN_GEEK` (default n); with it off the public header provides inline no-op stubs.

**Tech Stack:** ESP-IDF v5.4.1 (esp_lcd, esp_driver_spi, esp_driver_ledc), C, Python 3 + Pillow (font generation, host PNG dump), gcc (host tests), GitHub Actions matrix.

**Spec:** `docs/superpowers/specs/2026-11-01-status-screen-design.md` (deltas agreed in chat: AP indicator fixed at **top-right** — no position Kconfig; ST7789 comes from core `esp_lcd`, no raw init; snapshot carries the display text for the UI password).

## Global Constraints

- Screen 240x135 landscape, RGB565, ASCII only (font bytes outside 0x20..0x7E render as `?`).
- No LVGL, no full framebuffer: strip buffer of 27 rows (240*27*2 = 12,960 bytes, DMA-capable).
- BOOT = GPIO 9; short press toggles AP (existing), 3 s hold = factory reset. Do not change button behaviour.
- Default pins (UNVERIFIED, third-party pinout; must be Kconfig): CLK 1, MOSI 2, CS 5, DC 3, RST 4, BL 6.
- UI password is shown only while credentials are the defaults `admin`/`gateway`; otherwise the text `(yours)`.
- Default (option off) firmware must build and behave exactly as before.
- Commits: no `Co-Authored-By` trailer in this repo (user request); author `Vadim <vadim.surpin@gmail.com>`.
- Build env: `. /tmp/claude-1000/-home-vadim-zigbee-mqtt-opencode/9dca5977-fa36-4548-a4ab-c3b3a872b02f/scratchpad/env.sh`; default build dir `build_linux`.

## Review Focus

1. Long SSID/host (32/40 chars) must truncate with `..`, never wrap or overflow the 240 px width — Task 3 test.
2. Non-ASCII (UTF-8) SSID must show `?` per byte, not garbage/crash — Task 3 test.
3. Empty SSID/host/IP while "up" must show a placeholder (`-`), not blank — Task 3 test.
4. Wrong pins / panel init failure must log and disable the screen, never abort or boot-loop — Task 4 (no `ESP_ERROR_CHECK` in init path).
5. Strip-by-strip rendering must equal one-shot rendering (boundary rows) — Task 3 test.
6. Default build (option off) must compile and not link any LCD code — Task 1 and Task 6 builds.

---

### Task 1: Component skeleton, Kconfig, stubs

**Files:**
- Create: `components/status_screen/Kconfig`
- Create: `components/status_screen/CMakeLists.txt`
- Create: `components/status_screen/screen_snapshot.h`
- Create: `components/status_screen/status_screen.h`
- Create: `components/status_screen/status_screen.c`
- Modify: `.gitignore` (add `sdkconfig.geek`)

**Interfaces:**
- Produces: `screen_snapshot_t`, `typedef void (*status_screen_snapshot_cb_t)(screen_snapshot_t *out);`, `void status_screen_start(status_screen_snapshot_cb_t cb);` (stub when option off).

- [ ] **Step 1: Kconfig**

`components/status_screen/Kconfig`:
```
menu "Status screen"
    config STATUS_SCREEN_GEEK
        bool "Waveshare ESP32-C6-GEEK 1.14\" ST7789 status screen"
        default n
        help
            Drive the GEEK's LCD with setup / status information.

    if STATUS_SCREEN_GEEK
        config STATUS_SCREEN_PIN_CLK
            int "SPI CLK GPIO"
            default 1
        config STATUS_SCREEN_PIN_MOSI
            int "SPI MOSI GPIO"
            default 2
        config STATUS_SCREEN_PIN_CS
            int "LCD CS GPIO"
            default 5
        config STATUS_SCREEN_PIN_DC
            int "LCD DC GPIO"
            default 3
        config STATUS_SCREEN_PIN_RST
            int "LCD RST GPIO"
            default 4
        config STATUS_SCREEN_PIN_BL
            int "Backlight GPIO"
            default 6
        config STATUS_SCREEN_BL_PERCENT
            int "Backlight brightness (%)"
            range 5 100
            default 60
        config STATUS_SCREEN_BL_ACTIVE_HIGH
            bool "Backlight is active-high"
            default y
        config STATUS_SCREEN_GAP_X
            int "Panel column offset (after swap_xy)"
            default 40
        config STATUS_SCREEN_GAP_Y
            int "Panel row offset (after swap_xy)"
            default 52
        config STATUS_SCREEN_INVERT
            bool "Invert colours"
            default y
        config STATUS_SCREEN_MIRROR_X
            bool "Mirror X"
            default n
        config STATUS_SCREEN_MIRROR_Y
            bool "Mirror Y"
            default y
        config STATUS_SCREEN_SWAP_XY
            bool "Swap X/Y (landscape)"
            default y
    endif
endmenu
```

- [ ] **Step 2: CMakeLists**

`components/status_screen/CMakeLists.txt`:
```cmake
set(srcs "status_screen.c")
set(priv_reqs "")
if(CONFIG_STATUS_SCREEN_GEEK)
    list(APPEND srcs "screen_render.c")
    list(APPEND priv_reqs esp_lcd esp_driver_spi esp_driver_ledc esp_driver_gpio freertos)
endif()
idf_component_register(SRCS ${srcs} INCLUDE_DIRS "." PRIV_REQUIRES ${priv_reqs})
```

- [ ] **Step 3: Snapshot + public header**

`components/status_screen/screen_snapshot.h`:
```c
#ifndef SCREEN_SNAPSHOT_H
#define SCREEN_SNAPSHOT_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     provisioned;      /* Wi-Fi credentials saved */
    bool     wifi_up;
    char     sta_ssid[33];
    char     ip[16];
    bool     mqtt_up;
    char     mqtt_host[41];
    bool     zigbee_joined;
    uint8_t  zb_channel;
    uint16_t zb_pan;
    bool     ap_on;
    char     ap_ssid[33];
    char     ui_user[32];
    char     ui_pass[16];      /* text to display: the default password, or "(yours)" */
} screen_snapshot_t;
#endif
```
`components/status_screen/status_screen.h`:
```c
#ifndef STATUS_SCREEN_H
#define STATUS_SCREEN_H
#include "sdkconfig.h"
#include "screen_snapshot.h"

typedef void (*status_screen_snapshot_cb_t)(screen_snapshot_t *out);

#if CONFIG_STATUS_SCREEN_GEEK
/* Initialise the LCD and start the refresh task. On failure logs and returns; the rest of
   the firmware keeps running without a screen. cb must memset *out to 0 first. */
void status_screen_start(status_screen_snapshot_cb_t cb);
#else
static inline void status_screen_start(status_screen_snapshot_cb_t cb) { (void)cb; }
#endif
#endif
```
`components/status_screen/status_screen.c` (placeholder file only so the component has a source when the option is off; real driver arrives in Task 4):
```c
#include "status_screen.h"
```

- [ ] **Step 4: .gitignore** — append line `sdkconfig.geek`.

- [ ] **Step 5: Build default firmware**

Run: `. $ENV && ninja -C build_linux 2>&1 | grep -E "error|FAILED|binary size"` (re-run cmake happens automatically; if the new component is not seen run `idf.py -B build_linux reconfigure` first).
Expected: `mqtt2zigbee.bin binary size ...`, no errors.

- [ ] **Step 6: Commit**
```bash
git add components/status_screen .gitignore
git commit -m "status_screen: component skeleton, Kconfig, stubs"
```

---

### Task 2: Font generator and font data

**Files:**
- Create: `tools/gen_font.py`
- Create: `components/status_screen/font8x16.h` (generated, committed)

**Interfaces:**
- Produces: `static const uint8_t font8x16[95][16]` — glyph for char `c` at index `c-0x20`; row byte bit 7 = leftmost pixel.

- [ ] **Step 1: Script** — `tools/gen_font.py`:
```python
#!/usr/bin/env python3
"""Rasterise Liberation Mono to an 8x16 ASCII bitmap font -> font8x16.h"""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf"
out = sys.argv[1]
font = ImageFont.truetype(FONT, 13)
rows = []
for code in range(0x20, 0x7F):
    im = Image.new("L", (8, 16), 0)
    ImageDraw.Draw(im).text((0, 0), chr(code), font=font, fill=255)
    glyph = []
    for y in range(16):
        b = 0
        for x in range(8):
            if im.getpixel((x, y)) >= 110:
                b |= 0x80 >> x
        glyph.append(b)
    rows.append(glyph)
with open(out, "w") as f:
    f.write("/* Generated by tools/gen_font.py (Liberation Mono Bold 13px) - do not edit. */\n")
    f.write("#ifndef FONT8X16_H\n#define FONT8X16_H\n#include <stdint.h>\n")
    f.write("static const uint8_t font8x16[95][16] = {\n")
    for code, g in zip(range(0x20, 0x7F), rows):
        f.write("    {" + ",".join("0x%02x" % b for b in g) + "}, /* %r */\n" % chr(code))
    f.write("};\n#endif\n")
```

- [ ] **Step 2: Generate and eyeball**

Run: `python3 tools/gen_font.py components/status_screen/font8x16.h && python3 - <<'EOF'
import re
s=open('components/status_screen/font8x16.h').read()
m=re.findall(r'\{((?:0x[0-9a-f]{2},?){16})\}',s)
assert len(m)==95
g=[int(x,16) for x in m[ord('A')-0x20].split(',')]
print('\n'.join(''.join('#' if b&(0x80>>i) else '.' for i in range(8)) for b in g))
EOF`
Expected: a recognisable `A` (assert passes). If glyphs look clipped, change the PIL font size/offset in the script and regenerate.

- [ ] **Step 3: Commit**
```bash
git add tools/gen_font.py components/status_screen/font8x16.h
git commit -m "status_screen: generated 8x16 ASCII font"
```

---

### Task 3: Renderer with host tests (TDD)

**Files:**
- Create: `components/status_screen/screen_render.h`
- Create: `components/status_screen/screen_render.c`
- Create: `components/status_screen/host_test/test_render.c`
- Create: `components/status_screen/host_test/run.sh`
- Create: `components/status_screen/host_test/to_png.py`

**Interfaces:**
- Consumes: `screen_snapshot_t`, `font8x16`.
- Produces:
```c
#define SCREEN_W 240
#define SCREEN_H 135
#define SCREEN_STRIP_H 27
#define C_OK 0x07E0   /* green */
#define C_BAD 0xF800  /* red   */
/* Render rows [y0, y0+h) of the screen into px (h*SCREEN_W RGB565). swap_bytes: store
   big-endian (what the ST7789 SPI wants on a little-endian MCU). */
void screen_render_strip(const screen_snapshot_t *s, uint16_t *px, int y0, int h, bool swap_bytes);
/* Copies src to dst (cap chars incl. NUL), mapping non 0x20..0x7E bytes to '?', truncating with ".." if longer, "-" if empty. */
void screen_fit_text(char *dst, size_t cap, const char *src);
```
Layout constants (px): header height 34 with divider at y=34; title `mqtt2zigbee` at (6,2); AP cluster top-right: icon 16x16 at (218,2), text `AP on`/`AP off` right-aligned ending at x=214 on y=2, hint `BOOT: on/off` right-aligned ending at x=234 on y=18. Status rows: icon 32x32 (16x16 art at scale 2) at x=6, y=36+33*i (i=0 Wi-Fi, 1 MQTT, 2 Zigbee); text at x=46, primary line y, secondary line y+16, max 24 chars. Setup lines at x=6, y=42,62,82,102: `WiFi: <ap_ssid>`, `Open: http://192.168.4.1`, `Login: <ui_user>`, `Pass:  <ui_pass>`.

- [ ] **Step 1: Header** — `screen_render.h` containing exactly the declarations/macros above plus `#include <stdbool.h>`, `<stddef.h>`, `<stdint.h>`, `"screen_snapshot.h"`.

- [ ] **Step 2: Write failing tests** — `host_test/test_render.c`:
```c
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../screen_render.h"

static uint16_t fb[SCREEN_W * SCREEN_H];
static void render_full(const screen_snapshot_t *s) { screen_render_strip(s, fb, 0, SCREEN_H, false); }
static uint16_t at(int x, int y) { return fb[y * SCREEN_W + x]; }
static int lit(int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (at(x, y) != 0) n++;
    return n;
}
static void dump(const char *name) {
    FILE *f = fopen(name, "wb"); fwrite(fb, 2, SCREEN_W * SCREEN_H, f); fclose(f);
}
static screen_snapshot_t all_up(void) {
    screen_snapshot_t s; memset(&s, 0, sizeof s);
    s.provisioned = true; s.wifi_up = true; s.mqtt_up = true; s.zigbee_joined = true;
    strcpy(s.sta_ssid, "HomeNet"); strcpy(s.ip, "192.168.1.50"); strcpy(s.mqtt_host, "192.168.1.10");
    s.zb_channel = 15; s.zb_pan = 0x1A2B; s.ap_on = true; strcpy(s.ap_ssid, "ZigbeeGW");
    strcpy(s.ui_user, "admin"); strcpy(s.ui_pass, "gateway");
    return s;
}

int main(void) {
    /* Wi-Fi icon dot (icon coords 8,13 at scale 2, origin 6,36) is green when up. */
    screen_snapshot_t s = all_up();
    render_full(&s);
    assert(at(22, 62) == C_OK);
    dump("status_up.raw");

    /* All down: dot not green, slash centre (icon 8,7.5) is red. */
    screen_snapshot_t d = all_up();
    d.wifi_up = d.mqtt_up = d.zigbee_joined = false;
    render_full(&d);
    assert(at(22, 62) != C_OK);
    assert(at(22, 51) == C_BAD);
    dump("status_down.raw");

    /* Setup screen: text rows are drawn; password text change alters pixels. */
    screen_snapshot_t u = all_up(); u.provisioned = false;
    render_full(&u);
    assert(lit(6, 102, 234, 118) > 50);
    int a = lit(6, 102, 234, 118);
    strcpy(u.ui_pass, "(yours)");
    render_full(&u);
    assert(lit(6, 102, 234, 118) != a);
    dump("setup.raw");

    /* Strip rendering == one-shot rendering. */
    screen_snapshot_t t = all_up();
    render_full(&t);
    static uint16_t ref[SCREEN_W * SCREEN_H];
    memcpy(ref, fb, sizeof ref);
    static uint16_t strip[SCREEN_W * SCREEN_STRIP_H];
    for (int y0 = 0; y0 < SCREEN_H; y0 += SCREEN_STRIP_H) {
        int h = SCREEN_H - y0 < SCREEN_STRIP_H ? SCREEN_H - y0 : SCREEN_STRIP_H;
        screen_render_strip(&t, strip, y0, h, false);
        assert(memcmp(strip, &ref[y0 * SCREEN_W], (size_t)h * SCREEN_W * 2) == 0);
    }

    /* Byte swap flag swaps every pixel. */
    screen_render_strip(&t, strip, 0, SCREEN_STRIP_H, true);
    assert(strip[0] == ((ref[0] >> 8) | (ref[0] << 8)));

    /* Text fitting. */
    char out[25];
    screen_fit_text(out, sizeof out, "0123456789012345678901234567890123");
    assert(strlen(out) == 24 && strcmp(out + 22, "..") == 0);
    screen_fit_text(out, sizeof out, "caf\xc3\xa9");
    assert(strcmp(out, "caf??") == 0);
    screen_fit_text(out, sizeof out, "");
    assert(strcmp(out, "-") == 0);

    /* Long strings never write outside the frame: render with max-length fields. */
    screen_snapshot_t l = all_up();
    memset(l.sta_ssid, 'W', 32); memset(l.mqtt_host, 'h', 40); memset(l.ap_ssid, 'A', 32);
    render_full(&l); l.provisioned = false; render_full(&l);

    puts("render tests OK");
    return 0;
}
```
`host_test/run.sh`:
```bash
#!/bin/sh
set -e
cd "$(dirname "$0")"
gcc -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -o test_render test_render.c ../screen_render.c -lm
./test_render
python3 to_png.py
```
`host_test/to_png.py`:
```python
from PIL import Image
import struct
for n in ("status_up", "status_down", "setup"):
    d = open(n + ".raw", "rb").read()
    px = struct.unpack("<%dH" % (240 * 135), d)
    im = Image.new("RGB", (240, 135))
    im.putdata([(((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31) for p in px])
    im.resize((720, 405), Image.NEAREST).save(n + ".png")
```
Add `components/status_screen/host_test/test_render`, `*.raw` and `*.png` to `.gitignore`.

- [ ] **Step 3: Run to verify failure**

Run: `sh components/status_screen/host_test/run.sh`
Expected: FAIL — `screen_render.c: No such file` / undefined references.

- [ ] **Step 4: Implement renderer** — `screen_render.c`:
```c
#include "screen_render.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "font8x16.h"

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BG    0x0000
#define C_TEXT  RGB(235, 235, 235)
#define C_DIM   RGB(140, 140, 150)
#define C_LINE  RGB(60, 60, 70)
#define C_HINT  RGB(240, 200, 60)

typedef struct { uint16_t *px; int y0, h; bool swap; } strip_t;
typedef enum { ICON_WIFI, ICON_MQTT, ICON_ZIGBEE, ICON_AP } icon_t;

static inline void put(strip_t *s, int x, int y, uint16_t c)
{
    if ((unsigned)x >= SCREEN_W || y < s->y0 || y >= s->y0 + s->h) return;
    s->px[(y - s->y0) * SCREEN_W + x] = s->swap ? (uint16_t)((c >> 8) | (c << 8)) : c;
}

static void rect(strip_t *s, int x, int y, int w, int h, uint16_t c)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) put(s, xx, yy, c);
}

static void text(strip_t *s, int x, int y, const char *str, uint16_t c)
{
    if (y + 16 <= s->y0 || y >= s->y0 + s->h) return;
    for (; *str; str++, x += 8) {
        unsigned char ch = (unsigned char)*str;
        const uint8_t *g = font8x16[(ch >= 0x20 && ch <= 0x7E) ? ch - 0x20 : '?' - 0x20];
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 8; col++)
                if (g[row] & (0x80 >> col)) put(s, x + col, y + row, c);
    }
}

void screen_fit_text(char *dst, size_t cap, const char *src)
{
    size_t max = cap - 1, n = 0;
    if (!src || !src[0]) { snprintf(dst, cap, "-"); return; }
    size_t len = strlen(src);
    for (; n < len && n < max; n++) {
        unsigned char ch = (unsigned char)src[n];
        dst[n] = (ch >= 0x20 && ch <= 0x7E) ? (char)ch : '?';
    }
    dst[n] = '\0';
    if (len > max && max >= 2) { dst[max - 2] = '.'; dst[max - 1] = '.'; }
}

static float seg_d(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = (vx * wx + vy * wy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float dx = px - (ax + t * vx), dy = py - (ay + t * vy);
    return sqrtf(dx * dx + dy * dy);
}

/* Icon art in a 16x16 box, sampled at pixel centres. */
static bool icon_hit(icon_t k, float x, float y)
{
    switch (k) {
    case ICON_WIFI: {
        float dx = x - 8, dy = y - 13, d = sqrtf(dx * dx + dy * dy);
        if (d <= 1.8f) return true;
        if (dy < 0 && fabsf(dx) <= -dy + 0.5f)
            return fabsf(d - 5) <= 0.9f || fabsf(d - 8) <= 0.9f || fabsf(d - 11) <= 0.9f;
        return false;
    }
    case ICON_MQTT: {
        static const float n[3][2] = {{8, 3.5f}, {3, 12.5f}, {13, 12.5f}};
        for (int i = 0; i < 3; i++) {
            float dx = x - n[i][0], dy = y - n[i][1];
            if (dx * dx + dy * dy <= 2.4f * 2.4f) return true;
            int j = (i + 1) % 3;
            if (seg_d(x, y, n[i][0], n[i][1], n[j][0], n[j][1]) <= 0.7f) return true;
        }
        return false;
    }
    case ICON_ZIGBEE: {
        float ax = fabsf(x - 8), ay = fabsf(y - 8);
        if (ax <= 7.6f && ay <= 7.6f && (ax >= 6.4f || ay >= 6.4f)) return true;
        return seg_d(x, y, 4.5f, 5, 11.5f, 5) <= 0.9f || seg_d(x, y, 11.5f, 5, 4.5f, 11) <= 0.9f ||
               seg_d(x, y, 4.5f, 11, 11.5f, 11) <= 0.9f;
    }
    case ICON_AP: {
        float dx = x - 8, dy = y - 8, d = sqrtf(dx * dx + dy * dy);
        if (d <= 1.6f) return true;
        if (fabsf(dy) <= fabsf(dx) + 0.5f) return fabsf(d - 4) <= 0.9f || fabsf(d - 7) <= 0.9f;
        return false;
    }
    }
    return false;
}

static void icon(strip_t *s, int x, int y, int scale, icon_t k, uint16_t c, bool slash)
{
    for (int py = 0; py < 16 * scale; py++) {
        int sy = y + py;
        if (sy < s->y0 || sy >= s->y0 + s->h) continue;
        for (int px = 0; px < 16 * scale; px++) {
            float fx = (px + 0.5f) / scale, fy = (py + 0.5f) / scale;
            if (slash) {
                float d = seg_d(fx, fy, 1, 14.5f, 14.5f, 1);
                if (d <= 1.1f) { put(s, x + px, sy, C_BAD); continue; }
                if (d <= 2.3f) continue;
            }
            if (icon_hit(k, fx, fy)) put(s, x + px, sy, c);
        }
    }
}

static void right_text(strip_t *s, int x_end, int y, const char *str, uint16_t c)
{
    text(s, x_end - (int)strlen(str) * 8, y, str, c);
}

static void header(strip_t *s, const screen_snapshot_t *sn)
{
    text(s, 6, 2, "mqtt2zigbee", C_TEXT);
    icon(s, 218, 2, 1, ICON_AP, sn->ap_on ? C_OK : C_DIM, !sn->ap_on);
    right_text(s, 214, 2, sn->ap_on ? "AP on" : "AP off", sn->ap_on ? C_OK : C_DIM);
    right_text(s, 234, 18, "BOOT: on/off", C_HINT);
    rect(s, 0, 34, SCREEN_W, 1, C_LINE);
}

static void setup_screen(strip_t *s, const screen_snapshot_t *sn)
{
    char v[25], line[40];
    screen_fit_text(v, sizeof v, sn->ap_ssid);   snprintf(line, sizeof line, "WiFi: %s", v);  text(s, 6, 42, line, C_TEXT);
    text(s, 6, 62, "Open: http://192.168.4.1", C_TEXT);
    screen_fit_text(v, sizeof v, sn->ui_user);   snprintf(line, sizeof line, "Login: %s", v); text(s, 6, 82, line, C_TEXT);
    screen_fit_text(v, sizeof v, sn->ui_pass);   snprintf(line, sizeof line, "Pass:  %s", v); text(s, 6, 102, line, C_TEXT);
}

static void row(strip_t *s, int i, icon_t k, bool up, const char *l1, const char *l2)
{
    int y = 36 + 33 * i;
    icon(s, 6, y, 2, k, up ? C_OK : C_DIM, !up);
    text(s, 46, y, l1, up ? C_TEXT : C_DIM);
    if (l2) text(s, 46, y + 16, l2, C_DIM);
}

static void status_screen_body(strip_t *s, const screen_snapshot_t *sn)
{
    char a[25], b[25], line[40];
    if (sn->wifi_up) { screen_fit_text(a, sizeof a, sn->sta_ssid); screen_fit_text(b, sizeof b, sn->ip); row(s, 0, ICON_WIFI, true, a, b); }
    else row(s, 0, ICON_WIFI, false, "no WiFi", sn->provisioned ? "connecting..." : NULL);

    if (sn->mqtt_up) { screen_fit_text(a, sizeof a, sn->mqtt_host); row(s, 1, ICON_MQTT, true, a, "MQTT connected"); }
    else row(s, 1, ICON_MQTT, false, "no MQTT", NULL);

    if (sn->zigbee_joined) {
        snprintf(line, sizeof line, "Zigbee ch %u", sn->zb_channel);
        snprintf(b, sizeof b, "PAN 0x%04X", sn->zb_pan);
        row(s, 2, ICON_ZIGBEE, true, line, b);
    } else row(s, 2, ICON_ZIGBEE, false, "no Zigbee", NULL);
}

void screen_render_strip(const screen_snapshot_t *sn, uint16_t *px, int y0, int h, bool swap_bytes)
{
    strip_t s = { px, y0, h, swap_bytes };
    rect(&s, 0, y0, SCREEN_W, h, C_BG);
    header(&s, sn);
    if (!sn->provisioned) setup_screen(&s, sn); else status_screen_body(&s, sn);
}
```
Note: `rect(..., C_BG)` with swap works because `C_BG` is 0.

- [ ] **Step 5: Run tests**

Run: `sh components/status_screen/host_test/run.sh`
Expected: `render tests OK`, no sanitizer output. If an icon pixel assertion misses (`at(22,62)` / `at(22,51)`), adjust the coordinates in the test to the true dot/slash centre computed from the art above (dot at icon (8,13) → screen (6+16, 36+26)), not the art.

- [ ] **Step 6: Visual check** — view `components/status_screen/host_test/{setup,status_up,status_down}.png` (Read tool). Fix overlaps/alignment in `screen_render.c` and re-run Step 5 until all three look right.

- [ ] **Step 7: Commit**
```bash
git add components/status_screen .gitignore
git commit -m "status_screen: renderer with host tests"
```

---

### Task 4: ESP-IDF panel driver and refresh task

**Files:**
- Modify: `components/status_screen/status_screen.c` (replace placeholder)

**Interfaces:**
- Consumes: `screen_render_strip`, `screen_snapshot_t`, `SCREEN_W/H/STRIP_H`, Kconfig values from Task 1.
- Produces: `status_screen_start(cb)` as declared in `status_screen.h`.

- [ ] **Step 1: Implement** — `status_screen.c`:
```c
#include "status_screen.h"

#if CONFIG_STATUS_SCREEN_GEEK
#include <string.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "screen_render.h"

static const char *TAG = "STATUS_SCREEN";
#define LCD_HOST SPI2_HOST

static status_screen_snapshot_cb_t s_cb;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_done;
static uint16_t *s_strip;

static bool on_color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static void backlight_on(void)
{
    ledc_timer_config_t t = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
                              .timer_num = LEDC_TIMER_0, .freq_hz = 5000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_timer_config(&t);
    uint32_t duty = 255 * CONFIG_STATUS_SCREEN_BL_PERCENT / 100;
    if (!CONFIG_STATUS_SCREEN_BL_ACTIVE_HIGH) duty = 255 - duty;
    ledc_channel_config_t c = { .gpio_num = CONFIG_STATUS_SCREEN_PIN_BL, .speed_mode = LEDC_LOW_SPEED_MODE,
                                .channel = LEDC_CHANNEL_0, .timer_sel = LEDC_TIMER_0, .duty = duty, .hpoint = 0 };
    ledc_channel_config(&c);
}

static esp_err_t panel_init(void)
{
    spi_bus_config_t bus = { .sclk_io_num = CONFIG_STATUS_SCREEN_PIN_CLK, .mosi_io_num = CONFIG_STATUS_SCREEN_PIN_MOSI,
                             .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
                             .max_transfer_sz = SCREEN_W * SCREEN_STRIP_H * 2 };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return err;

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = CONFIG_STATUS_SCREEN_PIN_DC, .cs_gpio_num = CONFIG_STATUS_SCREEN_PIN_CS,
        .pclk_hz = 20 * 1000 * 1000, .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0,
        .trans_queue_depth = 1, .on_color_trans_done = on_color_done,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io);
    if (err != ESP_OK) return err;

    esp_lcd_panel_dev_config_t pc = { .reset_gpio_num = CONFIG_STATUS_SCREEN_PIN_RST,
                                      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, .bits_per_pixel = 16 };
    err = esp_lcd_new_panel_st7789(io, &pc, &s_panel);
    if (err != ESP_OK) return err;
    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, CONFIG_STATUS_SCREEN_INVERT);
    esp_lcd_panel_swap_xy(s_panel, CONFIG_STATUS_SCREEN_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, CONFIG_STATUS_SCREEN_MIRROR_X, CONFIG_STATUS_SCREEN_MIRROR_Y);
    esp_lcd_panel_set_gap(s_panel, CONFIG_STATUS_SCREEN_GAP_X, CONFIG_STATUS_SCREEN_GAP_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);
    return ESP_OK;
}

static void draw(const screen_snapshot_t *sn)
{
    for (int y0 = 0; y0 < SCREEN_H; y0 += SCREEN_STRIP_H) {
        int h = SCREEN_H - y0 < SCREEN_STRIP_H ? SCREEN_H - y0 : SCREEN_STRIP_H;
        screen_render_strip(sn, s_strip, y0, h, true);
        esp_lcd_panel_draw_bitmap(s_panel, 0, y0, SCREEN_W, y0 + h, s_strip);
        xSemaphoreTake(s_done, pdMS_TO_TICKS(500));
    }
}

static void screen_task(void *arg)
{
    screen_snapshot_t cur, last;
    memset(&last, 0xFF, sizeof last);   /* force the first draw */
    for (;;) {
        memset(&cur, 0, sizeof cur);
        s_cb(&cur);
        if (memcmp(&cur, &last, sizeof cur) != 0) { draw(&cur); last = cur; }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void status_screen_start(status_screen_snapshot_cb_t cb)
{
    s_cb = cb;
    s_done = xSemaphoreCreateBinary();
    s_strip = heap_caps_malloc(SCREEN_W * SCREEN_STRIP_H * 2, MALLOC_CAP_DMA);
    if (!s_done || !s_strip) { ESP_LOGE(TAG, "out of memory, screen disabled"); return; }
    esp_err_t err = panel_init();
    if (err != ESP_OK) { ESP_LOGE(TAG, "LCD init failed (%s), screen disabled", esp_err_to_name(err)); return; }
    backlight_on();
    xTaskCreate(screen_task, "screen", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "status screen started");
}
#endif
```
(No `ESP_ERROR_CHECK` anywhere: a wrong pin map logs and disables the screen.)

- [ ] **Step 2: Compile with the option on** (separate sdkconfig so the user's `sdkconfig` is untouched):
```bash
. $ENV
idf.py -B build_geek -D SDKCONFIG=sdkconfig.geek -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.geek" set-target esp32c6
```
First create `sdkconfig.defaults.geek` containing the single line `CONFIG_STATUS_SCREEN_GEEK=y`, then Wire-up happens in Task 5; for now build only the component: `idf.py -B build_geek ... build 2>&1 | grep -E "status_screen.*(error|warning)|FAILED"`
Expected: no errors/warnings from `status_screen`. (Link of the full app may still pass; main not yet calling start.)

- [ ] **Step 3: Commit**
```bash
git add components/status_screen sdkconfig.defaults.geek
git commit -m "status_screen: ST7789 driver and refresh task"
```

---

### Task 5: Wire into main

**Files:**
- Modify: `main/main.c`
- Modify: `main/CMakeLists.txt` (add `status_screen` to REQUIRES)

**Interfaces:**
- Consumes: `status_screen_start`, `screen_snapshot_t`; `wifi_manager_is_connected/get_sta_ssid/get_ip/ap_active`, `mqtt_client_is_connected`, `app_config_get_mqtt_host/get_ui_user/get_ui_pass/is_provisioned`.

- [ ] **Step 1: Edit `main/CMakeLists.txt`** — append `status_screen` to the `REQUIRES` list.

- [ ] **Step 2: Edit `main/main.c`**
Add `#include "status_screen.h"` with the other includes. Replace `zigbee_join_bridge` and add the snapshot callback:
```c
static volatile bool     s_zb_joined;
static volatile uint8_t  s_zb_channel;
static volatile uint16_t s_zb_pan;

static void zigbee_join_bridge(uint8_t channel, uint16_t pan_id, uint16_t short_addr)
{
    s_zb_channel = channel; s_zb_pan = pan_id; s_zb_joined = true;
    web_server_set_zigbee_info(NULL, pan_id, channel, short_addr);
}

#if CONFIG_STATUS_SCREEN_GEEK
static void screen_snapshot(screen_snapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    s->provisioned   = app_config_is_provisioned();
    s->wifi_up       = wifi_manager_is_connected();
    wifi_manager_get_sta_ssid(s->sta_ssid, sizeof(s->sta_ssid));
    const char *ip = wifi_manager_get_ip();
    if (s->wifi_up && ip) snprintf(s->ip, sizeof(s->ip), "%s", ip);
    s->mqtt_up       = mqtt_client_is_connected();
    snprintf(s->mqtt_host, sizeof(s->mqtt_host), "%s", app_config_get_mqtt_host());
    s->zigbee_joined = s_zb_joined;
    s->zb_channel    = s_zb_channel;
    s->zb_pan        = s_zb_pan;
    s->ap_on         = wifi_manager_ap_active();
    snprintf(s->ap_ssid, sizeof(s->ap_ssid), "ZigbeeGW");
    const char *user = app_config_get_ui_user(), *pass = app_config_get_ui_pass();
    snprintf(s->ui_user, sizeof(s->ui_user), "%s", user);
    bool dflt = !strcmp(user, "admin") && !strcmp(pass, "gateway");
    snprintf(s->ui_pass, sizeof(s->ui_pass), "%s", dflt ? "gateway" : "(yours)");
}
#endif
```
In `app_main()`, right after `button_handler_set_short_callback(wifi_mode_toggle);` add:
```c
#if CONFIG_STATUS_SCREEN_GEEK
    status_screen_start(screen_snapshot);
#endif
```
Add `#include "sdkconfig.h"` if `CONFIG_STATUS_SCREEN_GEEK` is not already visible (it is via `status_screen.h`).

- [ ] **Step 3: Build both variants**
```bash
. $ENV
ninja -C build_linux 2>&1 | grep -E "error|FAILED|mqtt2zigbee.bin binary"
idf.py -B build_geek -D SDKCONFIG=sdkconfig.geek -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.geek" build 2>&1 | grep -E "error|FAILED|mqtt2zigbee.bin binary"
```
Expected: both print `mqtt2zigbee.bin binary size ...`, no errors. Confirm the default image has no LCD code: `nm build_linux/mqtt2zigbee.elf | grep -c esp_lcd_new_panel_st7789` → `0`; geek image → `1` or more (use `riscv32-esp-elf-nm`).

- [ ] **Step 4: Make merged images**
```bash
idf.py -B build_geek merge-bin -o mqtt2zigbee-geek-merged.bin
ls -l build_geek/mqtt2zigbee-geek-merged.bin
```

- [ ] **Step 5: Commit**
```bash
git add main
git commit -m "main: feed status screen snapshots"
```

---

### Task 6: CI matrix, release, docs

**Files:**
- Modify: `.github/workflows/build.yml`
- Modify: `README.md`, `CHANGELOG.md`
- Modify: `docs/superpowers/specs/2026-11-01-status-screen-design.md` (record the agreed deltas)

- [ ] **Step 1: Matrix build.** In `build.yml` replace the `build` job with:
```yaml
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        include:
          - variant: default
            dir: build
            sdk: ""
            suffix: ""
          - variant: geek
            dir: build_geek
            sdk: -D SDKCONFIG=sdkconfig.geek -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.geek"
            suffix: -geek
    steps:
      - uses: actions/checkout@v4
      - name: Build firmware
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.4.1
          target: esp32c6
          path: .
          command: idf.py -B ${{ matrix.dir }} ${{ matrix.sdk }} build
      - name: Create merged flash image
        uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.4.1
          target: esp32c6
          path: .
          command: idf.py -B ${{ matrix.dir }} ${{ matrix.sdk }} merge-bin -o mqtt2zigbee${{ matrix.suffix }}-merged.bin
      - uses: actions/upload-artifact@v4
        with:
          name: firmware-${{ matrix.variant }}
          path: |
            ${{ matrix.dir }}/mqtt2zigbee.bin
            ${{ matrix.dir }}/mqtt2zigbee${{ matrix.suffix }}-merged.bin
          if-no-files-found: error
```
Replace the `release` job's download/rename steps with:
```yaml
      - uses: actions/download-artifact@v4
        with:
          pattern: firmware-*
          path: fw
      - name: Rename for release
        run: |
          v="${GITHUB_REF_NAME}"
          cp fw/firmware-default/build/mqtt2zigbee.bin "mqtt2zigbee-${v}-app.bin"
          cp fw/firmware-default/build/mqtt2zigbee-merged.bin "mqtt2zigbee-${v}-merged.bin"
          cp fw/firmware-geek/build_geek/mqtt2zigbee.bin "mqtt2zigbee-${v}-geek-app.bin"
          cp fw/firmware-geek/build_geek/mqtt2zigbee-geek-merged.bin "mqtt2zigbee-${v}-geek-merged.bin"
          sha256sum mqtt2zigbee-${v}-*.bin > SHA256SUMS
```
Also add a host-test step to `lint.yml`: `- run: sh components/status_screen/host_test/run.sh` after installing Pillow (`pip install pillow`) — tests only need gcc; skip `to_png.py` in CI by running the compiled test directly: `gcc -std=c11 -Wall -Wextra -Werror -o /tmp/t components/status_screen/host_test/test_render.c components/status_screen/screen_render.c -lm && cd components/status_screen/host_test && /tmp/t`.
Note: download-artifact preserves the upload path layout; if the `cp` paths differ in the first CI run, fix them from the run log (`ls -R fw`).

- [ ] **Step 2: README** — add a section "Waveshare ESP32-C6-GEEK (status screen)" after "Status LED": what the screen shows (setup screen, status rows, AP indicator top-right with `BOOT: on/off`), the `-geek-merged.bin` asset, build command, pin table with the "pins/offsets unverified, adjust via `idf.py menuconfig` → Status screen" note.

- [ ] **Step 3: CHANGELOG** — under a new `## [Unreleased]` add `### Added` entry: GEEK status screen (separate `-geek` image).

- [ ] **Step 4: Spec deltas** — in the spec: AP indicator fixed top-right (remove "Kconfig position"); ST7789 via core `esp_lcd` (no raw init); snapshot carries `ui_pass` display text.

- [ ] **Step 5: Verify and commit** — re-run `sh components/status_screen/host_test/run.sh`; `git add -A; git commit -m "CI matrix for GEEK image, docs"`. Do not push or tag; the user flashes `build_geek/mqtt2zigbee-geek-merged.bin` first and reports what the panel shows.
