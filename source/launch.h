#ifndef LAUNCH_H
#define LAUNCH_H

#include <stdbool.h>
#include <gctypes.h>

// Parses "ABCD" (4-char title ID, the high half is guessed among installed
// channel types) or "0001000148415858" / "00010001-48415858" (full title ID)
// and checks that the title is installed. Returns false if not parseable or not installed.
bool launch_resolve_title(const char *id, u64 *tid);

// Launches a channel directly (no banner screen). Returns (an error code) only on failure.
s32 launch_title(u64 tid);

// Returns to the System Menu, telling Priiloader to skip autoboot
// (otherwise it would boot this program again). Never returns.
void launch_system_menu(void) __attribute__((noreturn));

// Powers the console off. Never returns.
void launch_power_off(void) __attribute__((noreturn));

#endif
