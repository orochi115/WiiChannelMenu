#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <gccore.h>

#define SCREEN_H 480.0f

typedef struct {
	GXTexObj obj;
	void *data;
	int w, h;   // texture size (multiple of 4)
} Texture;

void gfx_init(void);
// Virtual screen width: 640 (4:3) or 854 (16:9). Height is always SCREEN_H.
float gfx_screen_w(void);

void gfx_begin_frame(void);
void gfx_end_frame(void);
void gfx_shutdown(void);

// Creates an RGBA8 texture from an RGBA buffer, resizing it to new_w x new_h
// (rounded up to a multiple of 4).
bool gfx_texture_from_rgba(Texture *tex, const unsigned char *rgba, int w, int h, int new_w, int new_h);
void gfx_texture_free(Texture *tex);

void gfx_draw_rect(float x, float y, float w, float h, GXColor c);
void gfx_draw_frame(float x, float y, float w, float h, float thickness, GXColor c);
void gfx_draw_texture(const Texture *tex, float x, float y, float w, float h, GXColor modulate);

#endif
