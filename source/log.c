#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "log.h"

static FILE *log_fp;
static bool log_file_enabled;

// Recent messages are also kept in memory, so they can be shown on screen
// when the SD card is not usable.
static char lines[LOG_MAX_LINES][LOG_LINE_LEN];
static int line_count;

void log_reset(void)
{
	remove(LOG_PATH);
	log_file_enabled = true;
}

void log_msg(const char *fmt, ...)
{
	char buf[LOG_LINE_LEN];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (line_count < LOG_MAX_LINES)
		strcpy(lines[line_count++], buf);
	else {
		memmove(lines[0], lines[1], (LOG_MAX_LINES - 1) * LOG_LINE_LEN);
		strcpy(lines[LOG_MAX_LINES - 1], buf);
	}

	if (!log_file_enabled)
		return;
	if (!log_fp) {
		log_fp = fopen(LOG_PATH, "w");
		if (!log_fp)
			return;
	}
	fprintf(log_fp, "%s\n", buf);
}

int log_lines(const char **out, int max)
{
	int n = line_count < max ? line_count : max;
	for (int i = 0; i < n; i++)
		out[i] = lines[line_count - n + i];
	return n;
}

void log_close(void)
{
	if (log_fp) {
		fclose(log_fp);
		log_fp = NULL;
	}
	log_file_enabled = false;
}
