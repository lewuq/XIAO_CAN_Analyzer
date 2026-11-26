#include "gfx.h"
#include <stddef.h>
#include <string.h>

/* Five columns, seven rows; only the English UI's glyphs are needed. */
static const uint8_t font[96][5] = {
 ['!'-32]={0,0,0x5f,0,0}, ['+'-32]={8,8,0x3e,8,8},
 ['-'-32]={8,8,8,8,8}, ['.'-32]={0,0x60,0x60,0,0},
 ['/'-32]={0x20,0x10,8,4,2}, [':'-32]={0,0x36,0x36,0,0},
 ['0'-32]={0x3e,0x51,0x49,0x45,0x3e}, ['1'-32]={0,0x42,0x7f,0x40,0},
 ['2'-32]={0x42,0x61,0x51,0x49,0x46}, ['3'-32]={0x21,0x41,0x45,0x4b,0x31},
 ['4'-32]={0x18,0x14,0x12,0x7f,0x10}, ['5'-32]={0x27,0x45,0x45,0x45,0x39},
 ['6'-32]={0x3c,0x4a,0x49,0x49,0x30}, ['7'-32]={1,0x71,9,5,3},
 ['8'-32]={0x36,0x49,0x49,0x49,0x36}, ['9'-32]={6,0x49,0x49,0x29,0x1e},
 ['A'-32]={0x7e,0x11,0x11,0x11,0x7e}, ['B'-32]={0x7f,0x49,0x49,0x49,0x36},
 ['C'-32]={0x3e,0x41,0x41,0x41,0x22}, ['D'-32]={0x7f,0x41,0x41,0x22,0x1c},
 ['E'-32]={0x7f,0x49,0x49,0x49,0x41}, ['F'-32]={0x7f,9,9,9,1},
 ['G'-32]={0x3e,0x41,0x49,0x49,0x7a}, ['H'-32]={0x7f,8,8,8,0x7f},
 ['I'-32]={0,0x41,0x7f,0x41,0}, ['J'-32]={0x20,0x40,0x41,0x3f,1},
 ['K'-32]={0x7f,8,0x14,0x22,0x41}, ['L'-32]={0x7f,0x40,0x40,0x40,0x40},
 ['M'-32]={0x7f,2,0x0c,2,0x7f}, ['N'-32]={0x7f,4,8,0x10,0x7f},
 ['O'-32]={0x3e,0x41,0x41,0x41,0x3e}, ['P'-32]={0x7f,9,9,9,6},
 ['Q'-32]={0x3e,0x41,0x51,0x21,0x5e}, ['R'-32]={0x7f,9,0x19,0x29,0x46},
 ['S'-32]={0x46,0x49,0x49,0x49,0x31}, ['T'-32]={1,1,0x7f,1,1},
 ['U'-32]={0x3f,0x40,0x40,0x40,0x3f}, ['V'-32]={0x1f,0x20,0x40,0x20,0x1f},
 ['W'-32]={0x3f,0x40,0x38,0x40,0x3f}, ['X'-32]={0x63,0x14,8,0x14,0x63},
 ['Y'-32]={7,8,0x70,8,7}, ['Z'-32]={0x61,0x51,0x49,0x45,0x43},
};

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
int gfx_width(const char *s, int scale) { return (int)strlen(s)*6*scale; }
void gfx_text(struct canvas *c, int x, int y, const char *s, int scale, uint16_t color)
{
    if (x<0 || y<0 || x+gfx_width(s,scale)>UI_WIDTH || y+7*scale>UI_HEIGHT) { c->text_overflow=true; }
    for (; *s; ++s, x+=6*scale) {
        unsigned int ch=(unsigned char)*s;
        if (ch<32U || ch>127U) { continue; }
        for (int col=0; col<5; ++col) {
            for (int row=0; row<7; ++row) {
                if (font[ch-32U][col] & (1U<<row)) { gfx_rect(c,x+col*scale,y+row*scale,scale,scale,color); }
            }
        }
    }
}
