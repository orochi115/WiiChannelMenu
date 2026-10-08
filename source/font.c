#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "stb_truetype.h"
#include "font.h"
#include "log.h"

#define MAX_CODEPOINTS 128
#ifndef FONT_MAX_SIZE
#define FONT_MAX_SIZE  (24 * 1024 * 1024)
#endif

static unsigned char *font_data;
static stbtt_fontinfo font;

bool font_load(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		log_msg("font: cannot open %s, labels disabled", path);
		return false;
	}

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size <= 0 || size > FONT_MAX_SIZE) {
		log_msg("font: %s has unsupported size %ld, labels disabled", path, size);
		fclose(fp);
		return false;
	}

	font_data = malloc(size);
	if (!font_data || fread(font_data, 1, size, fp) != (size_t)size) {
		log_msg("font: cannot read %s, labels disabled", path);
		fclose(fp);
		font_unload();
		return false;
	}
	fclose(fp);

	int offset = stbtt_GetFontOffsetForIndex(font_data, 0);
	if (offset < 0 || !stbtt_InitFont(&font, font_data, offset)) {
		log_msg("font: %s is not a valid font, labels disabled", path);
		font_unload();
		return false;
	}
	return true;
}

void font_unload(void)
{
	free(font_data);
	font_data = NULL;
}

static int utf8_decode(const char *s, int *out, int max)
{
	const unsigned char *p = (const unsigned char *)s;
	int n = 0;

	while (*p && n < max) {
		int cp, extra;
		if (*p < 0x80)                { cp = *p;        extra = 0; }
		else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
		else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
		else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
		else { p++; continue; }
		p++;
		for (int i = 0; i < extra; i++, p++) {
			if ((*p & 0xC0) != 0x80) { cp = -1; break; }
			cp = (cp << 6) | (*p & 0x3F);
		}
		if (cp > 0)
			out[n++] = cp;
	}
	return n;
}

static float measure(const int *cps, int n, float scale)
{
	float w = 0;
	for (int i = 0; i < n; i++) {
		int adv, lsb;
		stbtt_GetCodepointHMetrics(&font, cps[i], &adv, &lsb);
		w += adv * scale;
		if (i + 1 < n)
			w += stbtt_GetCodepointKernAdvance(&font, cps[i], cps[i + 1]) * scale;
	}
	return w;
}

bool font_render(const char *utf8, float px_height, int max_w, Texture *tex, int *draw_w, int *draw_h)
{
	if (!font_data || !utf8[0])
		return false;

	int cps[MAX_CODEPOINTS + 3];
	int n = utf8_decode(utf8, cps, MAX_CODEPOINTS);
	if (n == 0)
		return false;

	// Shrink up to 75% to fit, then truncate with "...".
	float scale = stbtt_ScaleForPixelHeight(&font, px_height);
	float width = measure(cps, n, scale);
	if (width > max_w) {
		float min_scale = scale * 0.75f;
		scale = scale * max_w / width;
		if (scale < min_scale)
			scale = min_scale;
		width = measure(cps, n, scale);
	}
	if (width > max_w) {
		static const int dots[3] = { '.', '.', '.' };
		float dots_w = measure(dots, 3, scale);
		while (n > 0 && measure(cps, n, scale) + dots_w > max_w)
			n--;
		memcpy(&cps[n], dots, sizeof(dots));
		n += 3;
		width = measure(cps, n, scale);
	}

	int ascent, descent, gap;
	stbtt_GetFontVMetrics(&font, &ascent, &descent, &gap);
	int baseline = (int)ceilf(ascent * scale);
	int h = baseline + (int)ceilf(-descent * scale);
	int w = (int)ceilf(width) + 2;
	int tw = (w + 3) & ~3, th = (h + 3) & ~3;

	unsigned char *alpha = calloc(tw * th, 1);
	unsigned char *rgba = malloc(tw * th * 4);
	if (!alpha || !rgba) {
		free(alpha);
		free(rgba);
		return false;
	}

	float x = 1;
	for (int i = 0; i < n; i++) {
		int adv, lsb, x0, y0, x1, y1;
		float shift_x = x - floorf(x);
		stbtt_GetCodepointHMetrics(&font, cps[i], &adv, &lsb);
		stbtt_GetCodepointBitmapBoxSubpixel(&font, cps[i], scale, scale, shift_x, 0, &x0, &y0, &x1, &y1);

		int gx = (int)floorf(x) + x0, gy = baseline + y0;
		int gw = x1 - x0, gh = y1 - y0;
		// Clip to the buffer.
		if (gx >= 0 && gy >= 0 && gx + gw <= tw && gy + gh <= th && gw > 0 && gh > 0) {
			stbtt_MakeCodepointBitmapSubpixel(&font, alpha + gy * tw + gx, gw, gh, tw,
			                                  scale, scale, shift_x, 0, cps[i]);
		}

		x += adv * scale;
		if (i + 1 < n)
			x += stbtt_GetCodepointKernAdvance(&font, cps[i], cps[i + 1]) * scale;
	}

	for (int i = 0; i < tw * th; i++) {
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = alpha[i];
	}

	bool ok = gfx_texture_from_rgba(tex, rgba, tw, th, tw, th);
	free(alpha);
	free(rgba);
	if (ok) {
		*draw_w = tw;
		*draw_h = th;
	}
	return ok;
}
