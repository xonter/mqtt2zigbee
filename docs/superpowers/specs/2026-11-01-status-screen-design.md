# Status screen for Waveshare ESP32-C6-GEEK — design

## Goal
Show device state on the GEEK's 1.14" ST7789 LCD (240x135): setup instructions while
unconfigured, and Wi-Fi / MQTT / Zigbee status (with icons) otherwise. Optional, build-time
only; the default firmware is unchanged.

## Decisions (agreed)
- **Detection:** build-time only. Kconfig `STATUS_SCREEN_GEEK` (default n) + overlay
  `sdkconfig.defaults.geek`; separate release image `mqtt2zigbee-vX-geek-merged.bin`.
- **No touch** on this board. Only input is BOOT (GPIO 9): short press toggles the AP
  (existing behaviour), 3 s hold = factory reset. The screen shows an AP indicator with a
  "BOOT: on/off" hint, placed by a Kconfig setting (default bottom-right) near the button.
- No LVGL. Raw ST7789 init over `esp_lcd` SPI panel IO; strip-based software rendering
  (~13 KB), built-in ASCII bitmap font, icons drawn from shapes, backlight PWM ~60 %.

## Component `components/status_screen`
- `status_screen.h`: `status_screen_start(snapshot_cb)`; snapshot struct
  `{ provisioned, wifi_up, sta_ssid, ip, mqtt_up, mqtt_host, zigbee_joined, zb_channel,
  zb_pan, ap_on, ap_ssid, ui_user, default_creds }`.
- Option off -> header-only inline no-op stubs; no `#ifdef` in callers.
- Task polls the callback ~1 s, redraws only on change.
- `main` provides the callback (reads wifi_manager, mqtt client, app_config, zigbee join
  info) — avoids a circular dependency on `app_config`.
- Pins/offsets/orientation are Kconfig values. Defaults (from a third-party pinout, NOT
  verified against the schematic): CLK 1, MOSI 2, CS 5, DC 3, RST 4, BL 6.

## Screens
1. **Setup** (no Wi-Fi saved): AP name `ZigbeeGW`, `http://192.168.4.1`, login `admin`,
   password `gateway` only while default credentials are active, otherwise "password: yours".
2. **Status**: three rows with icons — Wi-Fi (SSID + IP, else slashed icon "no WiFi"),
   MQTT (host, else "no MQTT"), Zigbee (channel + PAN when joined, else "no Zigbee").
   Green = up, grey/red slash = down.
3. **AP indicator** on both screens: AP icon, on/off state, hint "BOOT: on/off".

## Build / CI
- `sdkconfig.defaults.geek` selects the option; CI matrix builds default and geek images
  (separate build dirs); release attaches both.

## Testing
- Host-side render test: drawing code compiled on the PC, output dumped to PNG to check
  layouts without hardware.
- On hardware (user): pins, panel offset, orientation, colour inversion, brightness.
  Documented as unverified in the release notes.

## Out of scope
Touch, TF slot, page switching, RGB LED, non-ASCII text, LVGL.
