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

/* Disabled bool Kconfig options are undefined, not 0. */
#ifdef CONFIG_STATUS_SCREEN_INVERT
#define OPT_INVERT true
#else
#define OPT_INVERT false
#endif
#ifdef CONFIG_STATUS_SCREEN_SWAP_XY
#define OPT_SWAP_XY true
#else
#define OPT_SWAP_XY false
#endif
#ifdef CONFIG_STATUS_SCREEN_MIRROR_X
#define OPT_MIRROR_X true
#else
#define OPT_MIRROR_X false
#endif
#ifdef CONFIG_STATUS_SCREEN_MIRROR_Y
#define OPT_MIRROR_Y true
#else
#define OPT_MIRROR_Y false
#endif
#ifdef CONFIG_STATUS_SCREEN_BL_ACTIVE_HIGH
#define OPT_BL_HIGH true
#else
#define OPT_BL_HIGH false
#endif

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
    if (!OPT_BL_HIGH) duty = 255 - duty;
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
    esp_lcd_panel_invert_color(s_panel, OPT_INVERT);
    esp_lcd_panel_swap_xy(s_panel, OPT_SWAP_XY);
    esp_lcd_panel_mirror(s_panel, OPT_MIRROR_X, OPT_MIRROR_Y);
    esp_lcd_panel_set_gap(s_panel, CONFIG_STATUS_SCREEN_GAP_X, CONFIG_STATUS_SCREEN_GAP_Y);
    esp_lcd_panel_disp_on_off(s_panel, true);
    return ESP_OK;
}

/* false if a transfer failed or timed out; the caller then retries the whole frame. */
static bool draw(const screen_snapshot_t *sn)
{
    for (int y0 = 0; y0 < SCREEN_H; y0 += SCREEN_STRIP_H) {
        int h = SCREEN_H - y0 < SCREEN_STRIP_H ? SCREEN_H - y0 : SCREEN_STRIP_H;
        screen_render_strip(sn, s_strip, y0, h, true);
        if (esp_lcd_panel_draw_bitmap(s_panel, 0, y0, SCREEN_W, y0 + h, s_strip) != ESP_OK) return false;
        if (xSemaphoreTake(s_done, pdMS_TO_TICKS(500)) != pdTRUE) {
            xSemaphoreTake(s_done, 0);   /* drop a late completion so it cannot release the next strip early */
            return false;
        }
    }
    return true;
}

static void screen_task(void *arg)
{
    screen_snapshot_t cur, last;
    memset(&last, 0xFF, sizeof last);   /* force the first draw */
    for (;;) {
        memset(&cur, 0, sizeof cur);
        s_cb(&cur);
        if (memcmp(&cur, &last, sizeof cur) != 0) {
            if (draw(&cur)) memcpy(&last, &cur, sizeof cur);   /* memcpy keeps padding bytes identical */
            else ESP_LOGW(TAG, "draw failed, retrying");
        }
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
