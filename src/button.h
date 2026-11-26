#ifndef ANALYZER_BUTTON_H
#define ANALYZER_BUTTON_H
#include <stdbool.h>
#include <stdint.h>
enum press_event { PRESS_NONE, PRESS_SHORT, PRESS_LONG };
struct button_state {
    bool raw, stable, long_sent, blocked;
    uint32_t changed_ms, pressed_ms;
};
void button_init(struct button_state *b, bool down, uint32_t now);
enum press_event button_step(struct button_state *b, bool down, uint32_t now);
#endif
