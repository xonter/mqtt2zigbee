#ifndef CAPTIVE_DNS_H
#define CAPTIVE_DNS_H
#include <stddef.h>
#include <stdint.h>

/* Tiny DNS server for the setup AP: resolves every name to 192.168.4.1 so phones open the
   captive-portal page. Started/stopped with the AP. */
void captive_dns_start(void);
void captive_dns_stop(void);

/* Pure helper (host-tested): build the reply for query q into out. Returns 0 if unusable. */
size_t captive_dns_reply(const uint8_t *q, size_t len, uint8_t *out, size_t cap, uint32_t ip_be);
#endif
