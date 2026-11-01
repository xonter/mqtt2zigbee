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
    for (int i = 0; i < SCREEN_W * SCREEN_STRIP_H; i++)
        assert(strip[i] == (uint16_t)((ref[i] >> 8) | (ref[i] << 8)));

    /* Text fitting. */
    char out[25];
    screen_fit_text(out, sizeof out, "0123456789012345678901234567890123");
    assert(strlen(out) == 24 && strcmp(out + 22, "..") == 0);
    screen_fit_text(out, sizeof out, "caf\xc3\xa9");
    assert(strcmp(out, "caf??") == 0);
    screen_fit_text(out, sizeof out, "");
    assert(strcmp(out, "-") == 0);

    /* Empty fields while "up" show the placeholder (not blank). */
    screen_snapshot_t e = all_up(); e.sta_ssid[0] = 0; e.ip[0] = 0;
    render_full(&e);
    assert(lit(46, 36, 234, 68) > 0);

    /* Long strings never write outside the frame (ASan/UBSan would trip). */
    screen_snapshot_t l = all_up();
    memset(l.sta_ssid, 'W', 32); memset(l.mqtt_host, 'h', 40); memset(l.ap_ssid, 'A', 32);
    render_full(&l); l.provisioned = false; render_full(&l);

    puts("render tests OK");
    return 0;
}
