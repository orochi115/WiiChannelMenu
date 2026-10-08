#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <ogc/es.h>

#include "ios.h"
#include "log.h"

// IOS versions with SDHC (and USB 2.0) support, in order of preference.
static const int sdhc_ios[] = { 58, 61 };

// Same heuristic as Priiloader's IsIOSstub(): stubs have a title version
// ending in 0x00 and one own content plus two shared contents.
static bool ios_usable(int ios)
{
	u64 tid = 0x0000000100000000ULL | (u32)ios;
	u32 size = 0;

	if (ES_GetTMDViewSize(tid, &size) < 0 || size < sizeof(tmd_view))
		return false;

	tmd_view *view = memalign(32, (size + 31) & ~31);
	if (!view)
		return false;

	bool usable = false;
	if (ES_GetTMDView(tid, view, size) >= 0) {
		bool stub = (view->title_version & 0xff) == 0 && view->num_contents == 3 &&
		            view->contents[0].type == 1 && view->contents[1].type == 0x8001 &&
		            view->contents[2].type == 0x8001;
		usable = !stub;
	}
	free(view);
	return usable;
}

void ios_ensure_sdhc(void)
{
	int cur = IOS_GetVersion();

	for (size_t i = 0; i < sizeof(sdhc_ios) / sizeof(sdhc_ios[0]); i++) {
		if (cur == sdhc_ios[i])
			return;
	}

	for (size_t i = 0; i < sizeof(sdhc_ios) / sizeof(sdhc_ios[0]); i++) {
		int ios = sdhc_ios[i];
		if (!ios_usable(ios)) {
			log_msg("ios: IOS%d not installed or a stub", ios);
			continue;
		}
		s32 ret = IOS_ReloadIOS(ios);
		log_msg("ios: IOS%d -> IOS%d (%d)", cur, IOS_GetVersion(), ret);
		if (ret >= 0)
			return;
	}
	log_msg("ios: staying on IOS%d, SDHC cards may not work", IOS_GetVersion());
}
