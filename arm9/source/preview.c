#include "preview.h"

#include "convert.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

static int previewBackground = -1;
static u16 *previewGfx;

// Scratch buffer for loading a recording's first frame
static u16 previewFrame[BIN_FRAME_PIXELS];

void previewInit(void) {
	vramSetBankA(VRAM_A_MAIN_BG);
	videoSetMode(MODE_5_2D);
	previewBackground = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 1, 0);
	previewGfx = bgGetGfxPtr(previewBackground);

	bgShow(previewBackground);
}

u16 *previewGraphics(void) {
	return previewGfx;
}

void previewBitmap(const u16 *frame) {
	if(previewGfx == NULL)
		return;

	memcpy(previewGfx, frame, BIN_FRAME_PIXEL_BYTES);
}

void previewClear(void) {
	if(previewGfx == NULL)
		return;

	memset(previewGfx, 0, BIN_FRAME_PIXEL_BYTES);
}

bool previewFile(const char *binPath) {
	FILE *file = fopen(binPath, "rb");

	if(file == NULL)
		return false;

	bool ok = fread(previewFrame, 1, BIN_FRAME_PIXEL_BYTES, file) == BIN_FRAME_PIXEL_BYTES;

	fclose(file);

	if(ok)
		previewBitmap(previewFrame);

	return ok;
}
