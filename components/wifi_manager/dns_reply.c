#include <stdbool.h>
#include <string.h>
#include "captive_dns.h"

/* Answer every A query with the AP address; other types get an empty answer so clients
   fall back to IPv4. Returns the reply length, or 0 if the packet is not a usable query. */
size_t captive_dns_reply(const uint8_t *q, size_t len, uint8_t *out, size_t cap, uint32_t ip_be)
{
    if (len < 17 || len + 16 > cap || (q[2] & 0x80) || q[4] != 0 || q[5] != 1) return 0;
    size_t i = 12;
    while (i < len && q[i]) i += (size_t)q[i] + 1;       /* QNAME labels */
    if (i + 5 > len) return 0;
    size_t qend = i + 5;                                 /* NUL + QTYPE + QCLASS */
    bool is_a = q[i + 1] == 0 && q[i + 2] == 1;

    memcpy(out, q, qend);
    out[2] = 0x81; out[3] = 0x80;                        /* response, RD, RA, no error */
    out[6] = 0; out[7] = is_a ? 1 : 0;                   /* ANCOUNT */
    out[8] = out[9] = out[10] = out[11] = 0;             /* no authority/additional */
    if (!is_a) return qend;
    static const uint8_t ans[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4 };
    memcpy(out + qend, ans, sizeof ans);
    memcpy(out + qend + sizeof ans, &ip_be, 4);
    return qend + sizeof ans + 4;
}
