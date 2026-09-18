#ifndef BROWSER_H
#define BROWSER_H

#include <nds/ndstypes.h>

// Longest path the browser can hand back, including the nul terminator
#define BROWSER_PATH_LENGTH 256

// Walks the SD card on the bottom screen console and lets the user pick a .BIN
// recording. Returns true and fills `outPath` when a recording was selected,
// false when the user backed out with START.
bool browserPickBin(char *outPath, int outPathLen, const char *startDirectory);

#endif // BROWSER_H
