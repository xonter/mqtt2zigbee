idf_component_register(
    SRCS "wifi_manager.c" "captive_dns.c" "dns_reply.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash lwip
)
