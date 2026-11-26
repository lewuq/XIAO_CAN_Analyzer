#ifndef ANALYZER_DIRTY_H
#define ANALYZER_DIRTY_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define PANEL_WIDTH 120U
#define PANEL_HEIGHT 240U
#define PANEL_STRIDE (PANEL_WIDTH * 2U)
#define PANEL_COLUMN_ALIGN 4U
#define DIRTY_MAX_REGIONS ((PANEL_HEIGHT + 15U) / 16U)

struct panel_rect {
    uint16_t x0, y0, x1, y1; /* inclusive native coordinates */
};

static inline bool panel_window_valid(const struct panel_rect *rect)
{
    return rect->x0 <= rect->x1 && rect->x1 < PANEL_WIDTH &&
           rect->x0 % PANEL_COLUMN_ALIGN == 0U &&
           (rect->x1 + 1U) % PANEL_COLUMN_ALIGN == 0U &&
           rect->y0 < rect->y1 && rect->y1 < PANEL_HEIGHT;
}

size_t dirty_regions(const uint8_t *current, const uint8_t *previous,
                     struct panel_rect regions[DIRTY_MAX_REGIONS]);

#endif
