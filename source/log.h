#ifndef LOG_H
#define LOG_H

#include <stdbool.h>

#define LOG_PATH      "sd:/channelmenu/log.txt"
#define LOG_MAX_LINES 24
#define LOG_LINE_LEN  160

// Removes the log of the previous boot and enables writing to LOG_PATH.
// Call once SD is mounted; before that, messages are only kept in memory.
void log_reset(void);
// Appends a line to LOG_PATH (file is created lazily, only when something is logged).
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Returns up to max of the most recent messages (oldest first).
int log_lines(const char **out, int max);
// Closes the log file. Must be called before unmounting SD.
void log_close(void);

#endif
