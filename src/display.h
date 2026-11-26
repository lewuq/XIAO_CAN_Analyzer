#ifndef DISPLAY_H
#define DISPLAY_H
#include <stdint.h>
#include "dirty.h"
#define DISPLAY_BYTES (PANEL_STRIDE * PANEL_HEIGHT)
int display_init(void);
/* Native 120x240, RGB565 high byte first. UI rotates in software. */
int display_update(const uint8_t *native_rgb565, const struct panel_rect *rect);
#endif
