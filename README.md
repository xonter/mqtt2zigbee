# mqtt2zigbee

[![Build](https://github.com/VadimSurpin/mqtt2zigbee/actions/workflows/build.yml/badge.svg)](https://github.com/VadimSurpin/mqtt2zigbee/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

ESP32-C6 firmware that exposes MQTT controls as **virtual Zigbee endpoints**, so a
Zigbee hub (Yandex Alice, Home Assistant/ZHA, zigbee2mqtt, ...) can see and control
devices that only speak MQTT — for example [Wiren Board](https://wirenboard.com)
relays and sensors — without any extra gateway software.

```
 MQTT broker  <──Wi-Fi──>  ESP32-C6  <──802.15.4──>  Zigbee hub / Alice
 (WB, z2m, ...)          (one Zigbee router
                          with N endpoints)
```

## Features

- One Zigbee **router** node with many endpoints: switches, lights (on/off + level),
  roller blinds (native Window Covering cluster), temperature, humidity, pressure, contact and analog sensors.
- **MQTT discovery** page in the web UI with three sources:
  - *Wirenboard* — WB devices without the built-in ones (`system`, `network`, `wb-gpio`, ...);
  - *Wirenboard with built-ins*;
  - *Zigbee2MQTT* — devices published by an existing zigbee2mqtt instance
    (streamed parser, safe for large `bridge/devices` payloads).
  Sources that are not available on the broker are disabled automatically.
- Web UI for Wi-Fi provisioning, MQTT settings, device selection, Zigbee pairing
  (English and Russian).
- Wi-Fi/Zigbee coexistence, OTA-ready partition table with rollback, status LED,
  long-press factory reset.

## Screenshots

| Status | Wi-Fi |
|--------|-------|
| ![Status](docs/img/status.png) | ![Wi-Fi](docs/img/wifi.png) |
| **MQTT** | **Zigbee pairing** |
| ![MQTT](docs/img/mqtt.png) | ![Pairing](docs/img/pair.png) |

> Screenshots are rendered from `tools/mock_server.py`, which mimics the web UI; the
> device discovery page on real firmware additionally has the source selector.

## Tested with

- **Board:** ESP32-C6
- **MQTT source:** Wiren Board controller with **WB-MRC** relay modules

Other boards, brokers and Zigbee2MQTT mode have not been verified on hardware yet —
reports are welcome.

## Status LED

RGB LED on GPIO 8 (`STATUS_LED_GPIO`).

| State | Colour | Pattern | Meaning |
|-------|--------|---------|---------|
| Not configured | Blue | slow pulse, 1 Hz | Setup mode, no Wi-Fi saved — join the `ZigbeeGW` AP |
| Wi-Fi connecting | Yellow | fast blink, 5 Hz | Connecting to the router |
| Wi-Fi connected | Cyan | double pulse every 2 s | Got an IP, MQTT not connected yet |
| Operational | — | **off** | Wi-Fi and MQTT connected |
| Connection error | Red | triple flash every 2 s | Wi-Fi or MQTT connection lost |

## Waveshare ESP32-C6-GEEK (status screen)

The [ESP32-C6-GEEK](https://docs.waveshare.com/ESP32-C6-GEEK) dongle has a 1.14" 240x135 LCD.
Use the separate **`mqtt2zigbee-<version>-geek-merged.bin`** image from the release to drive it
(the normal image leaves the screen untouched):

- Not configured: AP name, URL, login and password (the password is shown only while it is still the default).
- Otherwise: Wi-Fi (SSID, IP), MQTT (broker) and Zigbee (channel, PAN) rows with icons; a slashed
  grey icon plus "no WiFi" / "no MQTT" / "no Zigbee" when down.
- Bottom-left, next to the BOOT button: AP on/off indicator with a `BOOT: on/off` hint.

Build it yourself:

```bash
idf.py -B build_geek -D SDKCONFIG=sdkconfig.geek \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.geek" set-target esp32c6 build
```

> The display pins (CLK 1, MOSI 2, CS 5, DC 3, RST 4, BL 6) and panel offsets come from a
> third-party pinout and have not been verified on hardware. Adjust them under
> `idf.py menuconfig` → *Status screen*. Renderer tests: `sh components/status_screen/host_test/run.sh`.

## Button

BOOT button on GPIO 9:

| Action | Press | Effect |
|--------|-------|--------|
| Short press | < 0.8 s | Toggle the setup access point on/off (also updates the saved "AP disabled" setting) |
| Long press | hold 3 s | Factory reset — erases Wi-Fi, MQTT, devices and credentials; LED flashes yellow/red |
| — | 0.8 – 3 s | Ignored |

## Hardware

- ESP32-C6 board with ≥ 8 MB flash (`partitions.csv` uses two 3 MB OTA slots).
- Status LED on GPIO 8 (change `STATUS_LED_GPIO` in `main/main.c`) and BOOT button on GPIO 9 — see above.

## Build

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32c6/get-started/) **v5.4.1**.

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

CI builds every push and pull request; tagged releases (`v*`) attach ready-made
firmware binaries.

### Flashing a release

```bash
pip install esptool
esptool.py --chip esp32c6 -p /dev/ttyACM0 write_flash 0x0 mqtt2zigbee-<version>-merged.bin
```

## Wiren Board roller blinds

Select the **WB Window Covering** driver and use the blind's Position control topic
(for example, `/devices/dooya_0x0101/controls/Position`). The driver derives the
Open, Close, and Stop command topics from that control path.

The position scale is translated between Wiren Board (100 = fully open) and Zigbee
Window Covering (0 = fully open). The endpoint exposes a single lift axis; tilt and
motor configuration controls are intentionally not exposed. After changing the driver
or its topic, restart the gateway and re-pair the device with the Zigbee hub if the
hub has cached the old endpoint definition.

## First run

1. Connect to the Wi-Fi access point **ZigbeeGW** and open <http://192.168.4.1>.
2. Log in with the default credentials `admin` / `gateway` — you are asked to change them.
3. Enter your Wi-Fi and MQTT broker settings.
4. Open **Devices**, pick the source and the controls to expose.
5. Open **Pair** and add the node on your Zigbee hub.

> The hub caches endpoints. After adding or removing exposed controls, restart the
> gateway and re-pair the node.

## Project layout

| Path | Purpose |
|------|---------|
| `main/` | app entry, config storage, device manager |
| `components/zigbee_core` | Zigbee stack glue, endpoint creation |
| `components/device_drivers` | per-type drivers (WB switch/range/sensor, z2m, ...) |
| `components/wb_driver` | Wiren Board topic discovery |
| `components/z2m_driver` | zigbee2mqtt device-list parser |
| `components/web_server`, `web_ui` | HTTP server and frontend |
| `components/esp-zigbee-lib` | vendored Espressif esp-zigbee-lib (Apache-2.0) |
| `tools/mock_server.py` | Flask mock of the web UI for frontend development |

More detail in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security issues: [SECURITY.md](SECURITY.md).

## License

MIT — see [LICENSE](LICENSE). `components/esp-zigbee-lib` is © Espressif Systems under
Apache-2.0 (see its own `LICENSE`).
