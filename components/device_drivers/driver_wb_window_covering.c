#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "device_driver.h"
#include "mqtt_client_wrapper.h"
#include "zigbee_coordinator.h"
#include "zcl/esp_zigbee_zcl_window_covering.h"
#include "zcl/esp_zigbee_zcl_command.h"

static const char *TAG = "DRIVER_WB_COVERING";

typedef struct {
    uint8_t endpoint_id;
    char position_topic[128];
    uint8_t position;
} wb_window_covering_data_t;

static void wb_window_covering_set_topic(void *device_data, const char *topic)
{
    wb_window_covering_data_t *data = device_data;
    if (data && topic) strncpy(data->position_topic, topic, sizeof(data->position_topic) - 1);
}

static esp_err_t wb_window_covering_init_endpoint(uint8_t endpoint_id, const void *config)
{
    (void)config;
    ESP_LOGI(TAG, "Registering Dooya/WB window covering endpoint %u", endpoint_id);
    return zigbee_coordinator_add_window_covering_endpoint(endpoint_id);
}

static esp_err_t wb_window_covering_handle_mqtt(const char *payload, void *device_data)
{
    wb_window_covering_data_t *data = device_data;
    if (!data || !payload) return ESP_ERR_INVALID_ARG;
    char *end = NULL;
    float value = strtof(payload, &end);
    if (end == payload || value < 0.0f || value > 100.0f) {
        ESP_LOGW(TAG, "Ignoring invalid WB position: %s", payload);
        return ESP_OK;
    }
    data->position = (uint8_t)(value + 0.5f);
    /* WB: 100=open, 0=closed. Zigbee Window Covering: 0=open, 100=closed. */
    uint8_t zigbee_position = 100 - data->position;
    return zigbee_coordinator_set_window_covering_position(data->endpoint_id, zigbee_position);
}

static esp_err_t wb_window_covering_handle_zigbee(void *zcl_cmd, void *device_data)
{
    wb_window_covering_data_t *data = device_data;
    const esp_zb_zcl_window_covering_movement_message_t *cmd = zcl_cmd;
    if (!data || !cmd || !data->position_topic[0]) return ESP_ERR_INVALID_ARG;

    const char *control = NULL;
    char value[8] = "1";
    switch (cmd->command) {
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_UP_OPEN:
        control = "Open";
        break;
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_DOWN_CLOSE:
        control = "Close";
        break;
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_STOP:
        control = "Stop";
        break;
    case ESP_ZB_ZCL_CMD_WINDOW_COVERING_GO_TO_LIFT_PERCENTAGE:
        control = "Position";
        snprintf(value, sizeof(value), "%u", 100 - (cmd->payload.percentage_lift_value > 100 ? 100 : cmd->payload.percentage_lift_value));
        break;
    default:
        ESP_LOGW(TAG, "Unsupported window covering command 0x%04x", cmd->command);
        return ESP_OK;
    }

    char topic[160];
    size_t n = strlen(data->position_topic);
    const char *slash = strrchr(data->position_topic, '/');
    if (!slash) return ESP_ERR_INVALID_ARG;
    size_t prefix_len = (size_t)(slash - data->position_topic);
    if (prefix_len + strlen(control) + 1 >= sizeof(topic)) return ESP_ERR_INVALID_SIZE;
    memcpy(topic, data->position_topic, prefix_len);
    topic[prefix_len] = '/';
    strcpy(topic + prefix_len + 1, control);

    esp_err_t err = mqtt_client_publish(topic, value, 1, 0);
    ESP_LOGI(TAG, "Zigbee command -> WB %s = %s (%s)", topic, value, esp_err_to_name(err));
    return err;
}

static esp_err_t wb_window_covering_publish_state(void *device_data, const char *topic)
{
    (void)device_data; (void)topic;
    return ESP_OK;
}
static size_t wb_window_covering_get_config_size(void) { return sizeof(wb_window_covering_data_t); }
static esp_err_t wb_window_covering_save_config(const void *d, char *blob, size_t *len)
{ *len = sizeof(wb_window_covering_data_t); memcpy(blob, d, *len); return ESP_OK; }
static esp_err_t wb_window_covering_load_config(void *d, const char *blob, size_t len)
{ if (len != sizeof(wb_window_covering_data_t)) return ESP_ERR_INVALID_SIZE; memcpy(d, blob, len); return ESP_OK; }
static void wb_window_covering_free_data(void *d) { free(d); }

static const device_driver_interface_t s_wb_window_covering_driver = {
    .device_type = "wb_window_covering",
    .display_name = "WB Window Covering",
    .description = "Wiren Board roller blind with native Zigbee Window Covering cluster",
    .readonly = false,
    .properties = "[]",
    .init_endpoint = wb_window_covering_init_endpoint,
    .set_topic = wb_window_covering_set_topic,
    .handle_mqtt = wb_window_covering_handle_mqtt,
    .handle_zigbee = wb_window_covering_handle_zigbee,
    .publish_state = wb_window_covering_publish_state,
    .get_config_size = wb_window_covering_get_config_size,
    .save_config = wb_window_covering_save_config,
    .load_config = wb_window_covering_load_config,
    .free_data = wb_window_covering_free_data,
};

void driver_wb_window_covering_register(void)
{
    device_driver_register(&s_wb_window_covering_driver);
}
