#ifndef IOS_H
#define IOS_H

// Makes sure an IOS with SDHC support is running. Priiloader may start this
// program under an old IOS (e.g. IOS36) that cannot access SDHC cards, so
// reload IOS58 (or IOS61) like the Homebrew Channel does.
// Must be called before WPAD, SD or ES are used.
void ios_ensure_sdhc(void);

#endif
