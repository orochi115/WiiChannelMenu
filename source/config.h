#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#define CONFIG_DIR       "sd:/channelmenu/"
#define CONFIG_PATH      CONFIG_DIR "config.ini"
#define MAX_CHANNELS     12
#define CFG_STR_MAX      256

typedef struct {
	char id[CFG_STR_MAX];     // "HAXX" or "0001000148415858"
	char name[CFG_STR_MAX];   // UTF-8, optional
	char image[CFG_STR_MAX];  // absolute "sd:/..." or relative to CONFIG_DIR, optional
	int line;                 // line of the [channel] header, for error messages
} ChannelEntry;

typedef struct {
	char font[CFG_STR_MAX];   // optional
	int count;
	ChannelEntry ch[MAX_CHANNELS];
} MenuConfig;

// Parses the config file. Returns false (after logging the reason) on any problem.
bool config_load(const char *path, MenuConfig *cfg);

// Resolves a path from the config: relative paths are taken relative to CONFIG_DIR.
void config_resolve_path(const char *in, char *out, int outsize);

#endif
