#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define GLYPH_W 5
#define GLYPH_H 7
#define GLYPH_ADV 6

/* Rows of a 5x7 bitmap glyph, bit 4 = leftmost pixel. NULL if unsupported. */
const uint8_t *font_glyph(char c);
int font_text_width(const char *s, int scale);
int font_text_height(int scale);

#endif /* FONT_H */
