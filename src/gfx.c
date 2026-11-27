#include "gfx.h"
#include <stddef.h>

struct glyph {
    /* Byte offset into the font bitmap; each glyph starts on a byte boundary. */
    uint16_t offset;
    uint8_t width;
    /* Pixel distance from this glyph's origin to the next glyph's origin. */
    uint8_t advance;
};

struct bitmap_font {
    const struct glyph *glyphs;
    const uint8_t *pixels;
    uint8_t height;
};

#include "ui_font.h"

void gfx_pixel(struct canvas *c, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= UI_WIDTH || y >= UI_HEIGHT) { return; }
    /* Software quarter turn: UI(x,y) -> panel(y,239-x). */
    size_t i = ((size_t)(239 - x) * 120U + (unsigned int)y) * 2U;
    c->pixels[i] = (uint8_t)(color >> 8);
    c->pixels[i+1] = (uint8_t)color;
}
uint16_t gfx_read(const struct canvas *c, int x, int y)
{
    if (x < 0 || y < 0 || x >= UI_WIDTH || y >= UI_HEIGHT) { return 0; }
    size_t i = ((size_t)(239-x) * 120U + (unsigned int)y) * 2U;
    return (uint16_t)((c->pixels[i] << 8) | c->pixels[i+1]);
}
void gfx_rect(struct canvas *c, int x, int y, int w, int h, uint16_t color)
{
    for (int yy=y; yy<y+h; ++yy) { for (int xx=x; xx<x+w; ++xx) { gfx_pixel(c,xx,yy,color); } }
}
void gfx_clear(struct canvas *c, uint16_t color)
{
    for (size_t i=0; i<GFX_BYTES; i+=2U) { c->pixels[i]=(uint8_t)(color>>8); c->pixels[i+1]=(uint8_t)color; }
    c->text_overflow=false;
}
static const struct glyph *lookup(const struct bitmap_font *font, unsigned char ch)
{
    /* The embedded table covers printable ASCII. */
    return &font->glyphs[(ch >= ' ' && ch <= '~' ? ch : '?') - ' '];
}

int gfx_width(const char *s, int size)
{
    /* Match proportional drawing so centered and right-aligned labels stay aligned. */
    const struct bitmap_font *font = &fonts[size - 1];
    int width = 0;

    for (; *s; ++s) {
        width += lookup(font, (unsigned char)*s)->advance;
    }
    return width;
}

static void draw_text(struct canvas *c, int x, int y, const char *s, int size,
                      uint16_t color, int cell_width)
{
    const struct bitmap_font *font = &fonts[size - 1];

    if (x < 0 || y < 0 || y + font->height > UI_HEIGHT) {
        c->text_overflow = true;
    }
    for (; *s; ++s) {
        const struct glyph *glyph = lookup(font, (unsigned char)*s);
        int advance = cell_width != 0 ? cell_width : glyph->advance;

        if (x + advance > UI_WIDTH || glyph->width > advance) {
            c->text_overflow = true;
        }
        /* Center the ink in fixed-width cells used for IDs and payloads. */
        int left = x + (cell_width ? (cell_width - glyph->width) / 2 : 0);
        const uint8_t *pixels = font->pixels + glyph->offset;
        unsigned int pixel = 0;

        /* A set bit writes the exact foreground color, with no edge blending. */
        for (int row = 0; row < font->height; ++row) {
            for (int col = 0; col < glyph->width; ++col, ++pixel) {
                if (pixels[pixel / 8] & (1U << (pixel % 8))) {
                    gfx_pixel(c, left + col, y + row, color);
                }
            }
        }
        x += advance;
    }
}

void gfx_text(struct canvas *c, int x, int y, const char *s, int size, uint16_t color)
{
    draw_text(c, x, y, s, size, color, 0);
}

void gfx_text_mono(struct canvas *c, int x, int y, const char *s, int size, uint16_t color)
{
    draw_text(c, x, y, s, size, color, 6 * size);
}
