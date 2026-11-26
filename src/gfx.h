#ifndef ANALYZER_GFX_H
#define ANALYZER_GFX_H
#include <stdbool.h>
#include <stdint.h>
#define UI_WIDTH 240
#define UI_HEIGHT 120
#define GFX_BYTES (UI_WIDTH * UI_HEIGHT * 2)
#define RGB565(r,g,b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_NAVY RGB565(23,43,77)
#ifndef SCREEN_YELLOW_RED
#define SCREEN_YELLOW_RED 232U
#endif
#ifndef SCREEN_YELLOW_GREEN
#define SCREEN_YELLOW_GREEN 255U
#endif
#define C_YELLOW RGB565(SCREEN_YELLOW_RED,SCREEN_YELLOW_GREEN,0U)
#define C_RED RGB565(229,57,53)
#define C_IVORY RGB565(255,248,231)
struct canvas { uint8_t pixels[GFX_BYTES]; bool text_overflow; };
void gfx_pixel(struct canvas *c, int x, int y, uint16_t color);
uint16_t gfx_read(const struct canvas *c, int x, int y);
void gfx_rect(struct canvas *c, int x, int y, int w, int h, uint16_t color);
void gfx_clear(struct canvas *c, uint16_t color);
int gfx_width(const char *s, int scale);
void gfx_text(struct canvas *c, int x, int y, const char *s, int scale, uint16_t color);
#endif
