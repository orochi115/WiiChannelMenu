#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gccore.h>
#include <fat.h>
#include <sdcard/wiisd_io.h>
#include <ogc/wiilaunch.h>
#include <wiiuse/wpad.h>

#include "stb_image.h"
#include "config.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "ios.h"
#include "launch.h"
#include "log.h"

#define MARGIN_X        48.0f
#define MARGIN_Y        40.0f
#define CELL_GAP        28.0f
#define CARD_PAD        10.0f
#define LABEL_H         40.0f
#define LABEL_PX        26.0f
#define SELECT_SCALE    1.10f
#define FRAME_THICK     6.0f
#define MAX_IMAGE_PIXELS (2048 * 2048)
#define SD_MOUNT_TRIES    20
#define SD_MOUNT_DELAY_US 250000

typedef struct {
	u64 tid;
	const ChannelEntry *entry;
	int row, col;
	float cx, cy;           // card center
	float card_w, card_h;
	float scale;            // animated toward 1.0 / SELECT_SCALE
	Texture img;
	bool has_img;
	float img_aspect;
	Texture label;
	bool has_label;
	int label_w, label_h;
} Tile;

static const GXColor COL_CARD     = { 0x2a, 0x30, 0x3c, 0xff };
static const GXColor COL_CARD_SEL = { 0x3a, 0x44, 0x56, 0xff };
static const GXColor COL_DIM      = { 0x90, 0x90, 0x90, 0xff };
static const GXColor COL_WHITE    = { 0xff, 0xff, 0xff, 0xff };
static const GXColor COL_NOIMAGE  = { 0x50, 0x5a, 0x6c, 0xff };

static MenuConfig cfg;
static Tile tiles[MAX_CHANNELS];
static int tile_count;
static int rows, cols;
static bool sd_mounted;

static volatile bool reset_pressed;
static volatile bool power_pressed;

static void reset_cb(u32 irq, void *ctx) { reset_pressed = true; }
static void power_cb(void) { power_pressed = true; }
static void wpad_power_cb(s32 chan) { power_pressed = true; }

static void unmount_sd(void)
{
	if (sd_mounted) {
		log_close();
		fatUnmount("sd");
		sd_mounted = false;
	}
}

#ifdef DEBUG_SCREEN
// Debug builds show the log on screen before leaving, since a failure may
// happen before the SD card (and so log.txt) is usable.
static void show_debug_screen(void)
{
	GXRModeObj *rm = VIDEO_GetPreferredMode(NULL);
	void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rm));
	VIDEO_Configure(rm);
	VIDEO_ClearFrameBuffer(rm, fb, COLOR_BLACK);
	CON_Init(fb, 24, 24, rm->fbWidth - 48, rm->xfbHeight - 48, rm->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_SetNextFramebuffer(fb);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();

	const char *lines[LOG_MAX_LINES];
	int n = log_lines(lines, LOG_MAX_LINES);
	printf("WiiChannelMenu debug - fatal error\n\n");
	for (int i = 0; i < n; i++)
		printf("%s\n", lines[i]);
	printf("\nReturning to the System Menu in 20 seconds...\n");

	for (int i = 0; i < 20 * 60; i++)
		VIDEO_WaitVSync();
}
#endif

static void __attribute__((noreturn)) fail_to_system_menu(void)
{
	unmount_sd();
#ifdef DEBUG_SCREEN
	show_debug_screen();
#endif
	launch_system_menu();
}

// Right after an IOS reload (e.g. when started by Priiloader) the SD card
// may not be ready yet, so retry for a few seconds.
static bool mount_sd(void)
{
	for (int i = 0; i < SD_MOUNT_TRIES; i++) {
		if (__io_wiisd.startup() && __io_wiisd.isInserted() && fatMountSimple("sd", &__io_wiisd)) {
			sd_mounted = true;
			return true;
		}
		log_msg("sd: mount attempt %d failed", i + 1);
		fatUnmount("sd");
		__io_wiisd.shutdown();
		usleep(SD_MOUNT_DELAY_US);
	}
	return false;
}

static void layout_tiles(bool with_labels)
{
	static const int cols_for_count[MAX_CHANNELS + 1] = { 1, 1, 2, 2, 2, 3, 3, 4, 4, 4, 4, 4, 4 };
	cols = cols_for_count[tile_count];
	rows = (tile_count + cols - 1) / cols;

	float sw = gfx_screen_w();
	float cell_w = (sw - 2 * MARGIN_X) / cols;
	float cell_h = (SCREEN_H - 2 * MARGIN_Y) / rows;
	float label_h = with_labels ? LABEL_H : 0.0f;

	// Cards share one size; the image area is kept at 4:3 when space allows.
	float card_w = cell_w - CELL_GAP;
	float card_h = cell_h - CELL_GAP;
	float img_w = card_w - 2 * CARD_PAD;
	float img_h = card_h - 2 * CARD_PAD - label_h;
	if (img_w > img_h * 4.0f / 3.0f)
		img_w = img_h * 4.0f / 3.0f;
	else
		img_h = img_w * 3.0f / 4.0f;
	card_w = img_w + 2 * CARD_PAD;
	card_h = img_h + 2 * CARD_PAD + label_h;

	float grid_h = rows * cell_h;
	float top = (SCREEN_H - grid_h) / 2;

	for (int i = 0; i < tile_count; i++) {
		Tile *t = &tiles[i];
		t->row = i / cols;
		t->col = i % cols;
		int in_row = (t->row == rows - 1) ? tile_count - t->row * cols : cols;
		float row_left = (sw - in_row * cell_w) / 2;
		t->cx = row_left + (t->col + 0.5f) * cell_w;
		t->cy = top + (t->row + 0.5f) * cell_h;
		t->card_w = card_w;
		t->card_h = card_h;
		t->scale = 1.0f;
	}
}

static void image_box(const Tile *t, float s, float *x, float *y, float *w, float *h, bool with_labels)
{
	float cw = t->card_w * s, ch = t->card_h * s;
	float label_h = with_labels ? LABEL_H * s : 0.0f;
	*x = t->cx - cw / 2 + CARD_PAD * s;
	*y = t->cy - ch / 2 + CARD_PAD * s;
	*w = cw - 2 * CARD_PAD * s;
	*h = ch - 2 * CARD_PAD * s - label_h;
}

static void load_image(Tile *t, bool with_labels)
{
	if (!t->entry->image[0])
		return;

	char path[CFG_STR_MAX + 32];
	config_resolve_path(t->entry->image, path, sizeof(path));

	int w, h, n;
	if (!stbi_info(path, &w, &h, &n)) {
		log_msg("image: cannot read %s (%s)", path, stbi_failure_reason());
		return;
	}
	if ((long long)w * h > MAX_IMAGE_PIXELS) {
		log_msg("image: %s is too large (%dx%d, max 2048x2048 pixels)", path, w, h);
		return;
	}

	unsigned char *rgba = stbi_load(path, &w, &h, &n, 4);
	if (!rgba) {
		log_msg("image: cannot decode %s (%s)", path, stbi_failure_reason());
		return;
	}

	// Texture resolution = largest on-screen size (selected), aspect preserved.
	float bx, by, bw, bh;
	image_box(t, SELECT_SCALE, &bx, &by, &bw, &bh, with_labels);
	float fit = fminf(bw / w, bh / h);
	int tw = (int)ceilf(w * fit), th = (int)ceilf(h * fit);

	t->img_aspect = (float)w / h;
	t->has_img = gfx_texture_from_rgba(&t->img, rgba, w, h, tw, th);
	stbi_image_free(rgba);
}

static void draw_tile(const Tile *t, bool selected, bool with_labels, float pulse)
{
	float s = t->scale;
	float cw = t->card_w * s, ch = t->card_h * s;
	float x = t->cx - cw / 2, y = t->cy - ch / 2;

	if (selected) {
		GXColor glow = { 0x00, (u8)(0xa0 + 0x50 * pulse), 0xff, 0xff };
		gfx_draw_frame(x, y, cw, ch, FRAME_THICK, glow);
	}
	gfx_draw_rect(x, y, cw, ch, selected ? COL_CARD_SEL : COL_CARD);

	float bx, by, bw, bh;
	image_box(t, s, &bx, &by, &bw, &bh, with_labels);
	GXColor mod = selected ? COL_WHITE : COL_DIM;

	if (t->has_img) {
		float w = bw, h = bw / t->img_aspect;
		if (h > bh) {
			h = bh;
			w = bh * t->img_aspect;
		}
		gfx_draw_texture(&t->img, bx + (bw - w) / 2, by + (bh - h) / 2, w, h, mod);
	} else {
		GXColor c = COL_NOIMAGE;
		if (!selected) {
			c.r = c.r * 0x90 / 0xff;
			c.g = c.g * 0x90 / 0xff;
			c.b = c.b * 0x90 / 0xff;
		}
		gfx_draw_rect(bx, by, bw, bh, c);
	}

	if (t->has_label) {
		float lw = t->label_w * s, lh = t->label_h * s;
		float ly = by + bh + (LABEL_H * s - lh) / 2;
		gfx_draw_texture(&t->label, t->cx - lw / 2, ly, lw, lh, mod);
	}
}

static int move_selection(int sel, InputAction a)
{
	const Tile *cur = &tiles[sel];

	switch (a) {
	case INPUT_LEFT:
		return (sel + tile_count - 1) % tile_count;
	case INPUT_RIGHT:
		return (sel + 1) % tile_count;
	case INPUT_UP:
	case INPUT_DOWN: {
		int row = cur->row + (a == INPUT_UP ? -1 : 1);
		if (row < 0 || row >= rows)
			return sel;
		int best = sel;
		float best_d = 1e9f;
		for (int i = 0; i < tile_count; i++) {
			if (tiles[i].row != row)
				continue;
			float d = fabsf(tiles[i].cx - cur->cx);
			if (d < best_d) {
				best_d = d;
				best = i;
			}
		}
		return best;
	}
	default:
		return sel;
	}
}

int main(int argc, char **argv)
{
	VIDEO_Init();
	ios_ensure_sdhc();
	SYS_SetResetCallback(reset_cb);
	SYS_SetPowerCallback(power_cb);
	s32 wii_ret = WII_Initialize();
#ifdef DEBUG_SCREEN
	log_msg("IOS%d v%d, WII_Initialize = %d", IOS_GetVersion(), IOS_GetRevision(), wii_ret);
#else
	(void)wii_ret;
#endif

	if (!mount_sd()) {
		log_msg("sd: cannot mount the SD card");
		fail_to_system_menu();
	}
	log_reset();

	if (!config_load(CONFIG_PATH, &cfg))
		fail_to_system_menu();

	for (int i = 0; i < cfg.count; i++) {
		u64 tid;
		if (!launch_resolve_title(cfg.ch[i].id, &tid)) {
			log_msg("config: line %d: channel '%s' is not installed or the id is invalid, skipped",
			        cfg.ch[i].line, cfg.ch[i].id);
			continue;
		}
		tiles[tile_count].tid = tid;
		tiles[tile_count].entry = &cfg.ch[i];
		tile_count++;
	}
	if (tile_count == 0) {
		log_msg("config: no installed channel to show");
		fail_to_system_menu();
	}

	input_init();
	WPAD_SetPowerButtonCallback(wpad_power_cb);
	gfx_init();
	gfx_begin_frame();
	gfx_end_frame();

	bool with_labels = false;
	if (cfg.font[0]) {
		char path[CFG_STR_MAX + 32];
		config_resolve_path(cfg.font, path, sizeof(path));
		with_labels = font_load(path);
	}

	layout_tiles(with_labels);
	for (int i = 0; i < tile_count; i++) {
		Tile *t = &tiles[i];
		load_image(t, with_labels);
		if (with_labels && t->entry->name[0]) {
			float bx, by, bw, bh;
			image_box(t, SELECT_SCALE, &bx, &by, &bw, &bh, with_labels);
			t->has_label = font_render(t->entry->name, LABEL_PX * SELECT_SCALE, (int)bw,
			                           &t->label, &t->label_w, &t->label_h);
			// Rendered at the selected size; scale back to the base size for drawing.
			t->label_w = (int)(t->label_w / SELECT_SCALE);
			t->label_h = (int)(t->label_h / SELECT_SCALE);
		}
	}
	font_unload();
	unmount_sd();

	int sel = 0;
	u32 frame = 0;

	for (;;) {
		if (power_pressed)
			launch_power_off();
		if (reset_pressed)
			launch_system_menu();

		InputAction a = input_poll();
		if (a == INPUT_HOME) {
			gfx_shutdown();
			launch_system_menu();
		}
		if (a == INPUT_ACCEPT) {
			gfx_shutdown();
			input_shutdown();
			s32 ret = launch_title(tiles[sel].tid);
			// Only reached if the launch failed.
			log_msg("launch: %016llX failed (%d)", tiles[sel].tid, ret);
			fail_to_system_menu();
		}
		sel = move_selection(sel, a);

		for (int i = 0; i < tile_count; i++) {
			float target = (i == sel) ? SELECT_SCALE : 1.0f;
			tiles[i].scale += (target - tiles[i].scale) * 0.3f;
		}
		float pulse = 0.5f + 0.5f * sinf(frame * 0.1f);
		frame++;

		gfx_begin_frame();
		for (int i = 0; i < tile_count; i++)
			if (i != sel)
				draw_tile(&tiles[i], false, with_labels, pulse);
		draw_tile(&tiles[sel], true, with_labels, pulse);
		gfx_end_frame();
	}
}
