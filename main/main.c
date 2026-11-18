#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "esp_zigbee_core.h"
#include "esp_zigbee_endpoint.h"

#include "esp_coexist.h"
#include "app_config.h"
#include "device_manager.h"
#include "wifi_manager.h"
#include "mqtt_client_wrapper.h"
#include "zigbee_coordinator.h"
#include "web_server.h"
#include "locale.h"
#include "button_handler.h"
#include "status_led.h"
#include "wb_driver.h"
#include "z2m_driver.h"
#include "status_screen.h"

static const char *TAG = "MAIN";
static const char *HEALTH_TAG = "HEALTH";

static void health_check_cb(void *arg)
{
    wifi_ap_record_t ap;
    bool up = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK);
    ESP_LOGI(HEALTH_TAG, "WiFi: %s RSSI: %d heap: %lu mqtt: %s",
             up ? "UP" : "DOWN",
             up ? ap.rssi : 0,
             esp_get_free_heap_size(),
             mqtt_client_is_connected() ? "UP" : "DOWN");
}

/* GPIO for the status LED — change to match your board wiring. */
#define STATUS_LED_GPIO 8

extern void device_drivers_init(void);

static void print_chip_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "This is ESP32 chip with %d CPU core(s), WiFi%s%s, %dMB %s flash",
             (int)chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             (int)(flash_size / (1024 * 1024)),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
}

/* MQTT connect/disconnect → LED + WB subscription + discovery window */
static void mqtt_connect_callback(bool connected)
{
    if (connected) {
        status_led_set(LED_STATE_MQTT_CONNECTED);
        wb_driver_enable_discovery();  /* also (re)starts the 60-second discovery timer */
        wb_driver_subscribe();
        if (app_config_get_discovery_mode() == DISC_MODE_Z2M)
            z2m_driver_refresh();
    } else {
        status_led_set(LED_STATE_CONN_ERROR);
    }
}

/* Bridge: routes MQTT messages to the appropriate handler. */
static void mqtt_message_bridge(const char *topic, const char *data, int len)
{
    /* Wirenboard devices — pass to wb_driver for discovery, then fall through to device_manager */
    if (topic && (strncmp(topic, "/devices/", 9) == 0 || strncmp(topic, "devices/", 8) == 0))
        wb_driver_process(topic, data, len);

    /* Zigbee2MQTT device states are JSON of several hundred bytes. Static: this runs only on
       the MQTT event task, and it keeps 1 KB off that task's stack. */
    static char payload[1024];
    memset(payload, 0, sizeof(payload));
    int copy = len < (int)(sizeof(payload) - 1) ? len : (int)(sizeof(payload) - 1);
    if (data && copy > 0) memcpy(payload, data, copy);
    device_manager_handle_mqtt_message(topic, payload);
}

/* Bridge: Zigbee attribute reports → device_manager (avoids circular component dep). */
static void zigbee_window_covering_cmd_bridge(uint8_t endpoint_id, const void *message)
{
    device_manager_handle_window_covering_command(endpoint_id, message);
}

static void zigbee_attr_bridge(uint8_t endpoint_id, uint16_t cluster, uint16_t attr_id,
                                const void *value, uint16_t size, uint8_t data_type)
{
    device_manager_handle_zigbee_attribute(endpoint_id, cluster, attr_id, value, size, data_type);
}

/* Bridge: Zigbee network join → web server pair page. */
static volatile bool     s_zb_joined;
static volatile uint8_t  s_zb_channel;
static volatile uint16_t s_zb_pan;

static void zigbee_join_bridge(uint8_t channel, uint16_t pan_id, uint16_t short_addr)
{
    s_zb_channel = channel;
    s_zb_pan     = pan_id;
    s_zb_joined  = true;
    web_server_set_zigbee_info(NULL, pan_id, channel, short_addr);
}

#if CONFIG_STATUS_SCREEN_GEEK
/* Collects what the GEEK status screen shows; called from the screen task about once a second. */
static void screen_snapshot(screen_snapshot_t *s)
{
    memset(s, 0, sizeof(*s));
    s->provisioned = app_config_is_provisioned();
    s->wifi_up     = wifi_manager_is_connected();
    wifi_manager_get_sta_ssid(s->sta_ssid, sizeof(s->sta_ssid));
    const char *ip = wifi_manager_get_ip();
    if (s->wifi_up && ip) snprintf(s->ip, sizeof(s->ip), "%s", ip);
    s->mqtt_up = mqtt_client_is_connected();
    snprintf(s->mqtt_host, sizeof(s->mqtt_host), "%s", app_config_get_mqtt_host());
    s->zigbee_joined = s_zb_joined;
    s->zb_channel    = s_zb_channel;
    s->zb_pan        = s_zb_pan;
    s->lang          = (uint8_t)locale_get();
    s->ap_on         = wifi_manager_ap_active();
    snprintf(s->ap_ssid, sizeof(s->ap_ssid), "ZigbeeGW");
    const char *user = app_config_get_ui_user(), *pass = app_config_get_ui_pass();
    snprintf(s->ui_user, sizeof(s->ui_user), "%s", user);
    bool dflt = !strcmp(user, "admin") && !strcmp(pass, "gateway");
    s->pass_custom = !dflt;
    if (dflt) snprintf(s->ui_pass, sizeof(s->ui_pass), "gateway");
}
#endif

/* Heavy init runs in its own task — the event loop stack is only 2304 bytes,
   too small for cJSON_Parse + Zigbee task creation. */
static void net_init_task(void *arg)
{
    device_manager_restore_from_nvs();
    zigbee_coordinator_start(device_manager_build_zigbee_endpoints);
    mqtt_client_start();
    vTaskDelete(NULL);
}

static bool s_net_init_done = false;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        status_led_set(LED_STATE_WIFI_CONNECTED);
        if (app_config_get_ap_disabled())
            wifi_manager_set_ap(false, "ZigbeeGW");

        if (!s_net_init_done) {
            s_net_init_done = true;
            xTaskCreate(net_init_task, "net_init", 6144, NULL, 5, NULL);
        }

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting…");
        status_led_set(LED_STATE_WIFI_SEARCHING);
    }
}

static void app_init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void factory_reset_wrapper(void)
{
    /* Flash LED rapidly for 1 s so the user sees the reset is happening. */
    for (int i = 0; i < 10; i++) {
        status_led_set(LED_STATE_WIFI_SEARCHING);
        vTaskDelay(pdMS_TO_TICKS(50));
        status_led_set(LED_STATE_CONN_ERROR);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    app_config_factory_reset();
}

static void wifi_mode_toggle(void)
{
    wifi_manager_toggle_ap("ZigbeeGW");
    /* Keep the persisted "AP disabled" setting (web UI / YAML) in sync with the button. */
    app_config_set_ap_disabled(!wifi_manager_ap_active());
}

void app_main(void)
{
    print_chip_info();

    app_init_nvs();

    /* Enable WiFi + IEEE 802.15.4 (Zigbee) radio coexistence before either
       stack initialises; without this the Zigbee radio starves WiFi. */
    ESP_ERROR_CHECK(esp_coex_wifi_i154_enable());

    status_led_init(STATUS_LED_GPIO);

    device_manager_init();
    wb_driver_init();
    {
        /* AUTO (never chosen) behaves as plain WB until the UI sees whether WB topics exist. */
        uint8_t mode = app_config_get_discovery_mode();
        wb_driver_set_mode(mode == DISC_MODE_AUTO ? DISC_MODE_WB : mode);
    }

    wifi_manager_init();

    device_drivers_init();

    /* Wire up cross-component bridges before any stack activity. */
    mqtt_client_set_message_handler(mqtt_message_bridge);
    mqtt_client_set_connect_callback(mqtt_connect_callback);
    zigbee_coordinator_set_attr_report_callback(zigbee_attr_bridge);
    zigbee_coordinator_set_window_covering_cmd_callback(zigbee_window_covering_cmd_bridge);
    zigbee_coordinator_set_network_join_callback(zigbee_join_bridge);

    wifi_manager_register_events(wifi_event_handler);

    /* Always start in AP+STA mode so the web UI is reachable at 192.168.4.1
       regardless of whether STA connects.  The AP can be toggled off later
       with the short-press button once the user no longer needs local access. */
    wifi_manager_ap_init("ZigbeeGW", NULL);
    web_server_start(80);

    /* Mark firmware valid so bootloader won't roll back if we reach this point. */
    esp_ota_mark_app_valid_cancel_rollback();

    if (app_config_is_provisioned()) {
        const char *ssid = app_config_get_wifi_ssid();
        const char *password = app_config_get_wifi_password();
        if (ssid && strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Connecting to %s", ssid);
            status_led_set(LED_STATE_WIFI_SEARCHING);
            wifi_manager_connect(ssid, password);
        } else {
            ESP_LOGI(TAG, "No WiFi SSID saved, staying in provisioning mode");
            app_config_set_provisioned(false);
            status_led_set(LED_STATE_NOT_CONFIGURED);
        }
    } else {
        ESP_LOGI(TAG, "Provisioning mode");
        status_led_set(LED_STATE_NOT_CONFIGURED);
    }

    /* Long press (3 s) on BOOT/RESET button → factory reset. */
    button_handler_init();
    button_handler_set_callback(RECONFIG_TRIGGER, factory_reset_wrapper);
    button_handler_set_short_callback(wifi_mode_toggle);

#if CONFIG_STATUS_SCREEN_GEEK
    status_screen_start(screen_snapshot);
#endif

    esp_timer_handle_t health_timer;
    esp_timer_create_args_t timer_args = { .callback = health_check_cb, .name = "health" };
    if (esp_timer_create(&timer_args, &health_timer) == ESP_OK) {
        esp_timer_start_periodic(health_timer, 10 * 1000 * 1000); /* 10 s */
    }

    ESP_LOGI(TAG, "Application initialized successfully");
}
