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

/* Decode one UTF-8 code point (ASCII and 2-byte sequences); advances *p. */
static unsigned next_cp(const char **p)
{
    const unsigned char *u = (const unsigned char *)*p;
    if (u[0] >= 0xC0 && u[0] < 0xE0 && (u[1] & 0xC0) == 0x80) {
        *p += 2;
        return ((unsigned)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    }
    *p += 1;
    return u[0];
}

static const uint8_t *glyph(unsigned cp)
{
    if (cp == 0x401) cp = 0x415;                 /* Ё -> Е */
    if (cp == 0x451) cp = 0x435;                 /* ё -> е */
    if (cp >= 0x20 && cp <= 0x7E) return font8x16[cp - 0x20];
    if (cp >= 0x410 && cp <= 0x44F) return font8x16[95 + cp - 0x410];
    return font8x16['?' - 0x20];
}

static void text(strip_t *s, int x, int y, const char *str, uint16_t c)
{
    if (y + 16 <= s->y0 || y >= s->y0 + s->h) return;
    while (*str) {
        const uint8_t *g = glyph(next_cp(&str));
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 8; col++)
                if (g[row] & (0x80 >> col)) put(s, x + col, y + row, c);
        x += 8;
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

enum { S_NO_WIFI, S_CONNECTING, S_NO_MQTT, S_NO_ZIGBEE, S_ZIGBEE_CH, S_PAN,
       S_L_WIFI, S_L_URL, S_L_LOGIN, S_L_PASS, S_AP_ON, S_AP_OFF, S_HINT, S_COUNT };

static const char *const STR[2][S_COUNT] = {
    { "No WiFi", "Connecting...", "No MQTT", "No Zigbee", "Zigbee, ch %u", "PAN 0x%04X",
      "WiFi:", "URL:", "Login:", "Pass:", "AP on", "AP off", "BOOT: on/off" },
    { "Нет WiFi", "Подключение...", "Нет MQTT", "Нет Zigbee", "Zigbee, кан. %u", "PAN 0x%04X",
      "Сеть:", "Адрес:", "Логин:", "Пароль:", "AP вкл", "AP выкл", "BOOT: вкл/выкл" },
};

static const char *tr(const screen_snapshot_t *sn, int id) { return STR[sn->lang == 1][id]; }

static void header(strip_t *s)
{
    text(s, 6, 2, "mqtt2zigbee", C_TEXT);
    rect(s, 0, 19, SCREEN_W, 1, C_LINE);
}

/* Bottom-left, next to the BOOT button. */
static void footer(strip_t *s, const screen_snapshot_t *sn)
{
    rect(s, 0, 116, SCREEN_W, 1, C_LINE);
    icon(s, 6, 118, 1, ICON_AP, sn->ap_on ? C_OK : C_DIM, !sn->ap_on);
    const char *st = tr(sn, sn->ap_on ? S_AP_ON : S_AP_OFF);
    text(s, 26, 118, st, sn->ap_on ? C_OK : C_DIM);
    text(s, 26 + 8 * 8, 118, tr(sn, S_HINT), C_HINT);
}

#define VALUE_X 70   /* value column; labels are at x=6 */

/* "<label>   <value>" on one line, value truncated so the line ends before x=234. */
static void kv_line(strip_t *s, int y, const char *label, const char *value)
{
    char v[32];
    int room = (234 - VALUE_X) / 8;               /* characters left for the value */
    screen_fit_text(v, (size_t)room + 1 < sizeof v ? (size_t)room + 1 : sizeof v, value);
    text(s, 6, y, label, C_DIM);
    text(s, VALUE_X, y, v, C_TEXT);
}

static void setup_screen(strip_t *s, const screen_snapshot_t *sn)
{
    kv_line(s, 28, tr(sn, S_L_WIFI), sn->ap_ssid);
    kv_line(s, 50, tr(sn, S_L_URL), "http://192.168.4.1");
    kv_line(s, 72, tr(sn, S_L_LOGIN), sn->ui_user);
    kv_line(s, 94, tr(sn, S_L_PASS), sn->ui_pass);
}

/* One line is centred on the icon, two lines fill its height. */
static void row(strip_t *s, int i, icon_t k, bool up, const char *l1, const char *l2)
{
    int y = 21 + 31 * i;
    icon(s, 6, y, 2, k, up ? C_OK : C_DIM, !up);
    text(s, 46, l2 ? y : y + 8, l1, up ? C_TEXT : C_DIM);
    if (l2) text(s, 46, y + 16, l2, C_DIM);
}

static void status_body(strip_t *s, const screen_snapshot_t *sn)
{
    char a[25], b[25], line[40];
    if (sn->wifi_up) {
        screen_fit_text(a, sizeof a, sn->sta_ssid);
        screen_fit_text(b, sizeof b, sn->ip);
        row(s, 0, ICON_WIFI, true, a, b);
    } else {
        row(s, 0, ICON_WIFI, false, tr(sn, S_NO_WIFI), sn->provisioned ? tr(sn, S_CONNECTING) : NULL);
    }

    if (sn->mqtt_up) {
        screen_fit_text(a, sizeof a - 6, sn->mqtt_host);
        snprintf(line, sizeof line, "MQTT: %s", a);
        row(s, 1, ICON_MQTT, true, line, NULL);
    } else {
        row(s, 1, ICON_MQTT, false, tr(sn, S_NO_MQTT), NULL);
    }

    if (sn->zigbee_joined) {
        snprintf(line, sizeof line, tr(sn, S_ZIGBEE_CH), sn->zb_channel);
        snprintf(b, sizeof b, tr(sn, S_PAN), sn->zb_pan);
        row(s, 2, ICON_ZIGBEE, true, line, b);
    } else {
        row(s, 2, ICON_ZIGBEE, false, tr(sn, S_NO_ZIGBEE), NULL);
    }
}

void screen_render_strip(const screen_snapshot_t *sn, uint16_t *px, int y0, int h, bool swap_bytes)
{
    strip_t s = { px, y0, h, swap_bytes };
    rect(&s, 0, y0, SCREEN_W, h, C_BG);
    header(&s);
    footer(&s, sn);
    if (!sn->provisioned) setup_screen(&s, sn); else status_body(&s, sn);
}
