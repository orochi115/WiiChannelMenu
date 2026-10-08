#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include "gfx.h"

// Loads a TTF/OTF/TTC font file into memory. Returns false if missing or invalid.
bool font_load(const char *path);
// Renders a UTF-8 string as a white texture with alpha, at most max_w pixels wide
// (shrinks, then truncates if needed). draw_w/draw_h receive the size to draw at.
bool font_render(const char *utf8, float px_height, int max_w, Texture *tex, int *draw_w, int *draw_h);
// Frees the font file buffer (rendered textures stay valid).
void font_unload(void);

#endif
