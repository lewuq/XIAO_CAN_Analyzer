#include "button.h"
#include <string.h>
void button_init(struct button_state *b, bool down, uint32_t now)
{
    memset(b, 0, sizeof(*b));
    b->raw = b->stable = b->blocked = down;
    b->changed_ms = b->pressed_ms = now;
}
enum press_event button_step(struct button_state *b, bool down, uint32_t now)
{
    if (down != b->raw) { b->raw = down; b->changed_ms = now; }
    if (b->stable != b->raw && now - b->changed_ms >= 25U) {
        b->stable = b->raw;
        if (b->stable) { b->pressed_ms = now; b->long_sent = false; }
        else {
            bool short_press = !b->blocked && !b->long_sent;
            b->blocked = false;
            return short_press ? PRESS_SHORT : PRESS_NONE;
        }
    }
    if (b->stable && !b->blocked && !b->long_sent && now - b->pressed_ms >= 700U) {
        b->long_sent = true;
        return PRESS_LONG;
    }
    return PRESS_NONE;
}
