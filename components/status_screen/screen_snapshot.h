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
