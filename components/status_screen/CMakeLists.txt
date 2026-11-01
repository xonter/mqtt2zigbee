# CONFIG_* values are not available while IDF scans component requirements, so sources and
# requirements are unconditional; the code itself is guarded by CONFIG_STATUS_SCREEN_GEEK and
# the linker drops the unused renderer in the default build (--gc-sections).
idf_component_register(
    SRCS "status_screen.c" "screen_render.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_ledc esp_driver_gpio freertos
)
