#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "captive_dns.h"

static const char *TAG = "CAPTIVE_DNS";
static volatile bool s_run, s_alive;

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    struct timeval tv = { .tv_sec = 1 };
    if (sock >= 0 && bind(sock, (struct sockaddr *)&addr, sizeof addr) == 0) {
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        uint8_t in[256], out[272];
        uint32_t ip = inet_addr("192.168.4.1");
        while (s_run) {
            struct sockaddr_in from; socklen_t fl = sizeof from;
            int n = recvfrom(sock, in, sizeof in, 0, (struct sockaddr *)&from, &fl);
            if (n <= 0) continue;
            size_t r = captive_dns_reply(in, (size_t)n, out, sizeof out, ip);
            if (r) sendto(sock, out, r, 0, (struct sockaddr *)&from, fl);
        }
    } else {
        ESP_LOGE(TAG, "DNS socket setup failed");
    }
    if (sock >= 0) close(sock);
    s_alive = false;
    vTaskDelete(NULL);
}

void captive_dns_start(void)
{
    s_run = true;
    if (s_alive) return;
    s_alive = true;
    if (xTaskCreate(dns_task, "cdns", 3072, NULL, 3, NULL) != pdPASS) s_alive = false;
    else ESP_LOGI(TAG, "started");
}

void captive_dns_stop(void) { s_run = false; }
