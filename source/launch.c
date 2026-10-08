#include <string.h>
#include <ctype.h>
#include <gccore.h>
#include <ogc/es.h>
#include <ogc/wiilaunch.h>

#include "launch.h"

// Priiloader checks these addresses for a magic word at boot.
#define PRIILOADER_MAGIC_ADDR1 0x8132FFFB
#define PRIILOADER_MAGIC_ADDR2 0x817FEFF0
#define PRIILOADER_MAGIC_PUNE  0x50756e65 // "Pune": boot the System Menu, skip autoboot

// Title types that can hold a launchable channel, in lookup order.
static const u32 channel_types[] = {
	0x00010001, // downloaded channels (WiiWare, VC, forwarders, HBC)
	0x00010002, // system channels (Mii, Photo, Shop, ...)
	0x00010008, // hidden channels
	0x00010004, // channels installed from discs
};

static bool title_installed(u64 tid)
{
	u32 views = 0, tmd_size = 0;

	if (ES_GetNumTicketViews(tid, &views) < 0 || views == 0)
		return false;
	// A leftover ticket without a TMD means the channel was deleted.
	if (ES_GetStoredTMDSize(tid, &tmd_size) < 0 || tmd_size == 0)
		return false;
	return true;
}

static bool parse_hex(const char *s, int len, u32 *out)
{
	u32 v = 0;
	for (int i = 0; i < len; i++) {
		char c = s[i];
		if (!isxdigit((unsigned char)c))
			return false;
		v = (v << 4) | (u32)(isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10));
	}
	*out = v;
	return true;
}

bool launch_resolve_title(const char *id, u64 *tid)
{
	size_t len = strlen(id);
	u32 hi, lo;

	if (len == 4) {
		lo = ((u32)(u8)id[0] << 24) | ((u32)(u8)id[1] << 16) | ((u32)(u8)id[2] << 8) | (u32)(u8)id[3];
		for (size_t i = 0; i < sizeof(channel_types) / sizeof(channel_types[0]); i++) {
			u64 t = ((u64)channel_types[i] << 32) | lo;
			if (title_installed(t)) {
				*tid = t;
				return true;
			}
		}
		return false;
	}

	if (len == 16 && parse_hex(id, 8, &hi) && parse_hex(id + 8, 8, &lo))
		;
	else if (len == 17 && id[8] == '-' && parse_hex(id, 8, &hi) && parse_hex(id + 9, 8, &lo))
		;
	else
		return false;

	*tid = ((u64)hi << 32) | lo;
	return title_installed(*tid);
}

s32 launch_title(u64 tid)
{
	return WII_LaunchTitle(tid);
}

void launch_system_menu(void)
{
	*(vu32 *)PRIILOADER_MAGIC_ADDR1 = PRIILOADER_MAGIC_PUNE;
	*(vu32 *)PRIILOADER_MAGIC_ADDR2 = PRIILOADER_MAGIC_PUNE;
	DCFlushRange((void *)(PRIILOADER_MAGIC_ADDR1 & ~31), 64);
	DCFlushRange((void *)(PRIILOADER_MAGIC_ADDR2 & ~31), 32);

	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
	// Fallback if the reset above did not happen.
	WII_ReturnToMenu();
	for (;;)
		VIDEO_WaitVSync();
}

void launch_power_off(void)
{
	SYS_ResetSystem(SYS_POWEROFF, 0, 0);
	for (;;)
		VIDEO_WaitVSync();
}
