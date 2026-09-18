#ifndef CONVERT_H
#define CONVERT_H

#include <nds/ndstypes.h>

// A recording is a flat list of frames, each one a 256x192 RGB555 preview
// followed by the tick count the recorder measured while capturing it.
#define BIN_FRAME_WIDTH 256
#define BIN_FRAME_HEIGHT 192
#define BIN_FRAME_PIXELS (BIN_FRAME_WIDTH * BIN_FRAME_HEIGHT)
#define BIN_FRAME_PIXEL_BYTES (BIN_FRAME_PIXELS * 2)
#define BIN_FRAME_DELAY_BYTES 4
#define BIN_FRAME_BYTES (BIN_FRAME_PIXEL_BYTES + BIN_FRAME_DELAY_BYTES)

// GIF palette entries, and how many frames are sampled to build that palette
#define BIN_PALETTE_COLORS 256
#define BIN_PALETTE_SAMPLES 64

// Playback rate used when a recording has no usable timing information
#define BIN_FALLBACK_FPS 10

typedef enum {
	BIN_CONVERT_OK,
	BIN_CONVERT_EMPTY,
	BIN_CONVERT_OPEN_FAILED,
	BIN_CONVERT_READ_ERROR,
	BIN_CONVERT_WRITE_ERROR,
	BIN_CONVERT_ABORTED
} BinConvertStatus;

typedef struct {
	int totalFrames;      // frames found in the recording
	int convertedFrames;  // frames written to the GIF
	bool recordedTiming;  // true when the stored per frame delays were used
	int playbackFps;      // playback rate of the GIF, rounded
	u32 outputBytes;      // size of the GIF that was written
} BinConvertInfo;

// Called for every frame once it has been read. Returns false to abort.
typedef bool (*BinConvertFrameFn)(void *user, const u16 *pixels, int frame, int totalFrames);

// Converts a recording into an animated GIF at `gifPath`. `onFrame` may be NULL.
// A partial GIF is removed when the conversion fails or is aborted.
BinConvertStatus binToGif(const char *binPath, const char *gifPath, BinConvertFrameFn onFrame, void *user, BinConvertInfo *info);

#endif // CONVERT_H
