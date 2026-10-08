#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include "input.h"

#define REPEAT_DELAY_MS    400
#define REPEAT_INTERVAL_MS 120
#define STICK_THRESHOLD    0.5f

enum { DIR_UP = 1, DIR_DOWN = 2, DIR_LEFT = 4, DIR_RIGHT = 8 };

static u32 prev_dirs;
static u64 repeat_at;

void input_init(void)
{
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS);
}

void input_shutdown(void)
{
	WPAD_Shutdown();
}

static u32 held_dirs(int chan, u32 held)
{
	u32 dirs = 0;

	if (held & (WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP))
		dirs |= DIR_UP;
	if (held & (WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN))
		dirs |= DIR_DOWN;
	if (held & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT))
		dirs |= DIR_LEFT;
	if (held & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT))
		dirs |= DIR_RIGHT;

	WPADData *data = WPAD_Data(chan);
	if (data && data->exp.type == WPAD_EXP_CLASSIC) {
		const joystick_t *js = &data->exp.classic.ljs;
		if (js->mag > STICK_THRESHOLD) {
			// 0 = up, 90 = right, 180 = down, 270 = left
			float a = js->ang;
			if (a >= 315 || a < 45)
				dirs |= DIR_UP;
			else if (a < 135)
				dirs |= DIR_RIGHT;
			else if (a < 225)
				dirs |= DIR_DOWN;
			else
				dirs |= DIR_LEFT;
		}
	}
	return dirs;
}

static InputAction dir_to_action(u32 d)
{
	if (d & DIR_UP)    return INPUT_UP;
	if (d & DIR_DOWN)  return INPUT_DOWN;
	if (d & DIR_LEFT)  return INPUT_LEFT;
	if (d & DIR_RIGHT) return INPUT_RIGHT;
	return INPUT_NONE;
}

InputAction input_poll(void)
{
	u32 down = 0, dirs = 0;

	WPAD_ScanPads();
	for (int chan = 0; chan < WPAD_MAX_WIIMOTES; chan++) {
		down |= WPAD_ButtonsDown(chan);
		dirs |= held_dirs(chan, WPAD_ButtonsHeld(chan));
	}

	if (down & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME))
		return INPUT_HOME;
	if (down & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))
		return INPUT_ACCEPT;

	u64 now = ticks_to_millisecs(gettime());
	u32 pressed = dirs & ~prev_dirs;
	prev_dirs = dirs;

	if (pressed) {
		repeat_at = now + REPEAT_DELAY_MS;
		return dir_to_action(pressed);
	}
	if (dirs && now >= repeat_at) {
		repeat_at = now + REPEAT_INTERVAL_MS;
		return dir_to_action(dirs);
	}
	return INPUT_NONE;
}
