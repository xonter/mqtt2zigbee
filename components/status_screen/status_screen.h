#ifndef STATUS_SCREEN_H
#define STATUS_SCREEN_H
#include "sdkconfig.h"
#include "screen_snapshot.h"

typedef void (*status_screen_snapshot_cb_t)(screen_snapshot_t *out);

#if CONFIG_STATUS_SCREEN_GEEK
/* Initialise the LCD and start the refresh task. On failure logs and returns; the rest of
   the firmware keeps running without a screen. */
void status_screen_start(status_screen_snapshot_cb_t cb);
#else
static inline void status_screen_start(status_screen_snapshot_cb_t cb) { (void)cb; }
#endif
#endif
