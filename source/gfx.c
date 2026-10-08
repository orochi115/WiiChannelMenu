#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>

#include "gfx.h"

#define FIFO_SIZE (256 * 1024)

static GXRModeObj *rmode;
static void *xfb[2];
static int cur_fb;
static void *gp_fifo;
static float screen_w = 640.0f;

void gfx_init(void)
{
	rmode = VIDEO_GetPreferredMode(NULL);

	// On 16:9 the TV stretches the 640 wide framebuffer horizontally, so draw
	// into a wider virtual space to keep images undistorted.
	if (CONF_GetAspectRatio() == CONF_ASPECT_16_9)
		screen_w = 854.0f;

	VIDEO_Configure(rmode);
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	cur_fb = 1;

	gp_fifo = memalign(32, FIFO_SIZE);
	memset(gp_fifo, 0, FIFO_SIZE);
	GX_Init(gp_fifo, FIFO_SIZE);

	GXColor bg = { 0x14, 0x18, 0x20, 0xff };
	GX_SetCopyClear(bg, GX_MAX_Z24);

	f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
	u32 xfb_height = GX_SetDispCopyYScale(yscale);
	GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
	GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, xfb_height);
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_SetCullMode(GX_CULL_NONE);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumTevStages(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_TRUE);
	GX_SetColorUpdate(GX_TRUE);

	Mtx44 proj;
	guOrtho(proj, 0, SCREEN_H, 0, screen_w, 0, 1);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);

	Mtx view;
	guMtxIdentity(view);
	GX_LoadPosMtxImm(view, GX_PNMTX0);

	GX_InvalidateTexAll();
}

float gfx_screen_w(void)
{
	return screen_w;
}

void gfx_begin_frame(void)
{
	GX_InvVtxCache();
	GX_InvalidateTexAll();
}

void gfx_end_frame(void)
{
	GX_DrawDone();
	GX_CopyDisp(xfb[cur_fb], GX_TRUE);
	GX_Flush();
	VIDEO_SetNextFramebuffer(xfb[cur_fb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	cur_fb ^= 1;
}

void gfx_shutdown(void)
{
	VIDEO_SetBlack(true);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	VIDEO_WaitVSync();
	GX_AbortFrame();
	GX_Flush();
}

static inline unsigned char sample(const unsigned char *src, int w, int x, int y, int c)
{
	return src[(y * w + x) * 4 + c];
}

bool gfx_texture_from_rgba(Texture *tex, const unsigned char *rgba, int w, int h, int new_w, int new_h)
{
	memset(tex, 0, sizeof(*tex));
	if (w <= 0 || h <= 0 || new_w <= 0 || new_h <= 0)
		return false;

	int tw = (new_w + 3) & ~3;
	int th = (new_h + 3) & ~3;
	if (tw > 1024) tw = 1024;
	if (th > 1024) th = 1024;

	unsigned char *data = memalign(32, tw * th * 4);
	if (!data)
		return false;

	// Box filter when shrinking (keeps downscaled photos clean), bilinear otherwise.
	float sx = (float)w / tw, sy = (float)h / th;
	int off = 0;
	for (int by = 0; by < th; by += 4) {
		for (int bx = 0; bx < tw; bx += 4) {
			for (int py = 0; py < 4; py++) {
				for (int px = 0; px < 4; px++) {
					int dx = bx + px, dy = by + py;
					unsigned char out[4];

					if (sx > 1.0f || sy > 1.0f) {
						int x0 = (int)(dx * sx), x1 = (int)((dx + 1) * sx);
						int y0 = (int)(dy * sy), y1 = (int)((dy + 1) * sy);
						if (x1 <= x0) x1 = x0 + 1;
						if (y1 <= y0) y1 = y0 + 1;
						if (x1 > w) x1 = w;
						if (y1 > h) y1 = h;
						unsigned sum[4] = { 0, 0, 0, 0 };
						unsigned n = 0;
						for (int yy = y0; yy < y1; yy++)
							for (int xx = x0; xx < x1; xx++, n++)
								for (int c = 0; c < 4; c++)
									sum[c] += sample(rgba, w, xx, yy, c);
						for (int c = 0; c < 4; c++)
							out[c] = n ? sum[c] / n : 0;
					} else {
						float fx = (dx + 0.5f) * sx - 0.5f, fy = (dy + 0.5f) * sy - 0.5f;
						if (fx < 0) fx = 0;
						if (fy < 0) fy = 0;
						int x0 = (int)fx, y0 = (int)fy;
						int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
						int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
						float ax = fx - x0, ay = fy - y0;
						for (int c = 0; c < 4; c++) {
							float top = sample(rgba, w, x0, y0, c) * (1 - ax) + sample(rgba, w, x1, y0, c) * ax;
							float bot = sample(rgba, w, x0, y1, c) * (1 - ax) + sample(rgba, w, x1, y1, c) * ax;
							out[c] = (unsigned char)(top * (1 - ay) + bot * ay + 0.5f);
						}
					}

					// RGBA8 4x4 tile: 16 AR pairs followed by 16 GB pairs.
					int i = (py * 4 + px) * 2;
					data[off + i]          = out[3];
					data[off + i + 1]      = out[0];
					data[off + 32 + i]     = out[1];
					data[off + 32 + i + 1] = out[2];
				}
			}
			off += 64;
		}
	}

	DCFlushRange(data, tw * th * 4);

	tex->data = data;
	tex->w = tw;
	tex->h = th;
	GX_InitTexObj(&tex->obj, data, tw, th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjFilterMode(&tex->obj, GX_LINEAR, GX_LINEAR);
	return true;
}

void gfx_texture_free(Texture *tex)
{
	free(tex->data);
	tex->data = NULL;
}

static void quad(float x, float y, float w, float h, GXColor c)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position2f32(x, y);         GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0, 0);
	GX_Position2f32(x + w, y);     GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(1, 0);
	GX_Position2f32(x + w, y + h); GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(1, 1);
	GX_Position2f32(x, y + h);     GX_Color4u8(c.r, c.g, c.b, c.a); GX_TexCoord2f32(0, 1);
	GX_End();
}

void gfx_draw_rect(float x, float y, float w, float h, GXColor c)
{
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	quad(x, y, w, h, c);
}

void gfx_draw_frame(float x, float y, float w, float h, float t, GXColor c)
{
	gfx_draw_rect(x - t, y - t, w + 2 * t, t, c);
	gfx_draw_rect(x - t, y + h, w + 2 * t, t, c);
	gfx_draw_rect(x - t, y, t, h, c);
	gfx_draw_rect(x + w, y, t, h, c);
}

void gfx_draw_texture(const Texture *tex, float x, float y, float w, float h, GXColor modulate)
{
	GX_LoadTexObj((GXTexObj *)&tex->obj, GX_TEXMAP0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	quad(x, y, w, h, modulate);
}
