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
    /* Wi-Fi icon dot (icon coords 8,13 at scale 2, origin 6,21) is green when up. */
    screen_snapshot_t s = all_up();
    render_full(&s);
    assert(at(22, 47) == C_OK);
    dump("status_up.raw");

    /* All down: dot not green, slash centre (icon 8,7.5) is red. */
    screen_snapshot_t d = all_up();
    d.wifi_up = d.mqtt_up = d.zigbee_joined = false;
    render_full(&d);
    assert(at(22, 47) != C_OK);
    assert(at(22, 36) == C_BAD);
    dump("status_down.raw");

    /* Setup screen: text rows are drawn; password text change alters pixels. */
    screen_snapshot_t u = all_up(); u.provisioned = false;
    render_full(&u);
    assert(lit(6, 94, 234, 110) > 50);
    int a = lit(6, 94, 234, 110);
    strcpy(u.ui_pass, "(yours)");
    render_full(&u);
    assert(lit(6, 94, 234, 110) != a);
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
    assert(lit(46, 21, 234, 53) > 0);

    /* Long strings never write outside the frame (ASan/UBSan would trip). */
    screen_snapshot_t l = all_up();
    memset(l.sta_ssid, 'W', 32); memset(l.mqtt_host, 'h', 40); memset(l.ap_ssid, 'A', 32);
    render_full(&l); l.provisioned = false; render_full(&l);

    /* Setup-screen lines must stay inside x < 234 even with maximum-length values. */
    screen_snapshot_t w = all_up(); w.provisioned = false;
    memset(w.ap_ssid, 'A', 32); memset(w.ui_user, 'u', 31);
    render_full(&w);
    assert(lit(234, 28, SCREEN_W, 44) == 0);   /* WiFi line  */
    assert(lit(234, 72, SCREEN_W, 88) == 0);   /* Login line */

    /* Russian: text differs from English; Cyrillic glyphs are drawn (not '?'); AP footer too. */
    screen_snapshot_t en = all_up(); en.wifi_up = false;
    render_full(&en);
    static uint16_t ref_en[SCREEN_W * SCREEN_H];
    memcpy(ref_en, fb, sizeof ref_en);
    screen_snapshot_t ru = en; ru.lang = 1;
    render_full(&ru);
    assert(memcmp(ref_en, fb, sizeof ref_en) != 0);
    assert(lit(46, 29, 234, 45) > 0);          /* single-line "Нет WiFi" centred on the icon */
    dump("status_ru.raw");

    /* AP footer sits bottom-left, below the separator at y=116, and follows ap_on. */
    screen_snapshot_t ap = all_up(); ap.ap_on = false;
    render_full(&ap);
    assert(lit(6, 118, 22, 134) > 0);
    assert(lit(0, 118, SCREEN_W, SCREEN_H) > 0 && lit(150, 0, SCREEN_W, 17) == 0);

    puts("render tests OK");
    return 0;
}
