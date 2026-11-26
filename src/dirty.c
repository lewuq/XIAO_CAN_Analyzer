#include "dirty.h"
#include <string.h>

_Static_assert(PANEL_WIDTH % PANEL_COLUMN_ALIGN == 0U,
               "A full panel row must satisfy CASET alignment");
_Static_assert(PANEL_HEIGHT % 2U == 0U, "Panel rows must allow two-row windows");

static unsigned int area(struct panel_rect r)
{
    return (r.x1 - r.x0 + 1U) * (r.y1 - r.y0 + 1U);
}

size_t dirty_regions(const uint8_t *current, const uint8_t *previous,
                     struct panel_rect regions[DIRTY_MAX_REGIONS])
{
    if (previous == NULL) {
        regions[0] = (struct panel_rect){0, 0, PANEL_WIDTH - 1U, PANEL_HEIGHT - 1U};
        return 1;
    }

    size_t n = 0;
    for (unsigned int top = 0; top < PANEL_HEIGHT; top += 16U) {
        struct panel_rect rect = {PANEL_WIDTH, PANEL_HEIGHT, 0, 0};
        for (unsigned int y = top; y < top + 16U && y < PANEL_HEIGHT; ++y) {
            unsigned int offset = y * PANEL_STRIDE;
            const uint8_t *a = current + offset;
            const uint8_t *b = previous + offset;
            if (memcmp(a, b, PANEL_STRIDE) == 0) {
                continue;
            }
            unsigned int left = 0, right = PANEL_WIDTH - 1U;
            while (a[left * 2U] == b[left * 2U] &&
                   a[left * 2U + 1U] == b[left * 2U + 1U]) {
                ++left;
            }
            while (a[right * 2U] == b[right * 2U] &&
                   a[right * 2U + 1U] == b[right * 2U + 1U]) {
                --right;
            }
            if (left < rect.x0) { rect.x0 = left; }
            if (right > rect.x1) { rect.x1 = right; }
            if (rect.y0 == PANEL_HEIGHT) { rect.y0 = y; }
            rect.y1 = y;
        }
        if (rect.y0 == PANEL_HEIGHT) {
            continue;
        }
        /* SH8501 CASET requires a 4-pixel start and inclusive width alignment.
         * Expand before merging so both the address and payload use one window.
         */
        rect.x0 -= rect.x0 % PANEL_COLUMN_ALIGN;
        rect.x1 += PANEL_COLUMN_ALIGN - 1U - rect.x1 % PANEL_COLUMN_ALIGN;
        if (rect.y0 == rect.y1) {
            /* PASET requires start < end. Stay within this 16-row band. */
            rect.y0 &= ~1U;
            rect.y1 = rect.y0 + 1U;
        }
        if (n != 0U) {
            struct panel_rect last = regions[n - 1U];
            struct panel_rect combined = {
                .x0 = last.x0 < rect.x0 ? last.x0 : rect.x0,
                .y0 = last.y0,
                .x1 = last.x1 > rect.x1 ? last.x1 : rect.x1,
                .y1 = rect.y1,
            };
            /* Merge if the added pixels are cheaper than another address window. */
            if (area(combined) <= area(last) + area(rect) + 900U) {
                regions[n - 1U] = combined;
                continue;
            }
        }
        regions[n++] = rect;
    }
    return n;
}
