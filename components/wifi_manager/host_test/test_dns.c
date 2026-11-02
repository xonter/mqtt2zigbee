#include <assert.h>
#include <string.h>
#include "../captive_dns.h"

int main(void)
{
    /* Query for "a.io" A IN, id 0x1234, RD. */
    const uint8_t q[] = { 0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0,
                          1, 'a', 2, 'i', 'o', 0, 0, 1, 0, 1 };
    uint8_t out[128];
    uint32_t ip = 0x0104A8C0;   /* 192.168.4.1 little-endian in memory: C0 A8 04 01 */
    size_t n = captive_dns_reply(q, sizeof q, out, sizeof out, ip);
    assert(n == sizeof q + 16);
    assert(out[0] == 0x12 && out[1] == 0x34 && (out[2] & 0x80) && out[7] == 1);
    assert(out[n - 4] == 0xC0 && out[n - 3] == 0xA8 && out[n - 2] == 4 && out[n - 1] == 1);

    /* AAAA: empty answer, not an error. */
    uint8_t q6[sizeof q]; memcpy(q6, q, sizeof q); q6[19] = 28;
    n = captive_dns_reply(q6, sizeof q6, out, sizeof out, ip);
    assert(n == sizeof q6 && out[7] == 0);

    /* Responses, truncated packets and junk are ignored. */
    uint8_t resp[sizeof q]; memcpy(resp, q, sizeof q); resp[2] |= 0x80;
    assert(captive_dns_reply(resp, sizeof resp, out, sizeof out, ip) == 0);
    assert(captive_dns_reply(q, 10, out, sizeof out, ip) == 0);
    assert(captive_dns_reply(q, sizeof q - 3, out, sizeof out, ip) == 0);
    uint8_t bad[20]; memset(bad, 0, sizeof bad); bad[5] = 1; bad[12] = 63;   /* label runs off the end */
    assert(captive_dns_reply(bad, sizeof bad, out, sizeof out, ip) == 0);
    return 0;
}
