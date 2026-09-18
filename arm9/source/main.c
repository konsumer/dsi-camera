#include "browser.h"
#include "camera.h"
#include "convert.h"
#include "preview.h"
#include "version.h"

#include <dirent.h>
#include <fat.h>
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RECORDING_DIRECTORY "/DCIM/100DSI00"

// Width of the progress bar on the conversion screen
#define PROGRESS_BAR_WIDTH 28

int getVideoNumber() {
	int highest = -1;

	DIR *pdir = opendir(RECORDING_DIRECTORY);
	if(pdir == NULL) {
		printf("Unable to open directory");
		return -1;
	} else {
		while(true) {
			struct dirent *pent = readdir(pdir);
			if(pent == NULL)
				break;

			if(strncmp(pent->d_name, "VID_", 4) == 0) {
				int val = atoi(pent->d_name + 4);
				if(val > highest)
					highest = val;
			}
		}
		closedir(pdir);
	}

	return highest + 1;
}

static void waitForKey(u32 keys) {
	while(true) {
		swiWaitForVBlank();
		scanKeys();

		if(keysDown() & keys)
			return;
	}
}

static int fileFrames(const char *path) {
	FILE *file = fopen(path, "rb");
	long size;

	if(file == NULL)
		return 0;

	if(fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return 0;
	}

	size = ftell(file);
	fclose(file);

	return size > 0 ? (int)(size / BIN_FRAME_BYTES) : 0;
}

static const char *convertMessage(BinConvertStatus status) {
	switch(status) {
	case BIN_CONVERT_OK:
		return "GIF written";
	case BIN_CONVERT_EMPTY:
		return "No complete frames in that file";
	case BIN_CONVERT_OPEN_FAILED:
		return "Unable to open the file";
	case BIN_CONVERT_READ_ERROR:
		return "Unable to read the recording";
	case BIN_CONVERT_WRITE_ERROR:
		return "Unable to write the GIF";
	default:
		return "Cancelled";
	}
}

// Shows the frame that is being encoded on the top screen and keeps the console
// in step. Returns false when the user cancels with B.
static bool conversionProgress(void *user, const u16 *pixels, int frame, int totalFrames) {
	(void)user;

	previewBitmap(pixels);

	if((frame % 8) == 0 || frame == totalFrames - 1) {
		int percent = (int)(((u64)(frame + 1) * 100) / totalFrames);
		int filled = (percent * PROGRESS_BAR_WIDTH) / 100;

		consoleClear();
		printf("Converting to GIF\n\n[");

		for(int i = 0; i < PROGRESS_BAR_WIDTH; i++)
			printf("%c", i < filled ? '=' : ' ');

		printf("]\n\nframe %d/%d\n\nB: cancel", frame + 1, totalFrames);
	}

	swiWaitForVBlank();
	scanKeys();

	return !(keysDown() & KEY_B);
}

static void convertRecording(void) {
	char binPath[BROWSER_PATH_LENGTH];
	char gifPath[BROWSER_PATH_LENGTH];
	BinConvertInfo info;
	size_t stem;

	if(!browserPickBin(binPath, sizeof(binPath), RECORDING_DIRECTORY))
		return;

	// Replace the .BIN extension so the GIF sits next to the recording
	stem = strlen(binPath);
	if(stem > 4 && strcasecmp(binPath + stem - 4, ".bin") == 0)
		stem -= 4;

	snprintf(gifPath, sizeof(gifPath), "%.*s.gif", (int)stem, binPath);

	consoleClear();
	printf("Converting\n%s\n\n%d frames\n", binPath, fileFrames(binPath));

	BinConvertStatus status = binToGif(binPath, gifPath, conversionProgress, NULL, &info);

	consoleClear();
	printf("%s\n\n", convertMessage(status));

	if(status == BIN_CONVERT_OK) {
		printf("%d frames at %d fps\n", info.convertedFrames, info.playbackFps);
		printf("%uKB GIF\n\n", (unsigned)(info.outputBytes / 1024));
		printf("%s\n", gifPath);
	} else {
		printf("%s\n", binPath);
	}

	printf("\nA: menu");
	waitForKey(KEY_A | KEY_START);
}

static void recordVideo(void) {
	char vidName[64];
	FILE *out;

	snprintf(vidName, sizeof(vidName), "%s/VID_%04d.BIN", RECORDING_DIRECTORY, getVideoNumber());
	out = fopen(vidName, "wb");

	if(out == NULL) {
		printf("Unable to create\n%s\n\nA: menu", vidName);
		waitForKey(KEY_A);
		return;
	}

	cameraActivate(CAM_OUTER);

	consoleClear();
	printf("It's recording!\n");
	printf("START to finish\n\n");
	printf("Output file:\n%s\n", vidName);

	cpuStartTiming(0);

	while(true) {
		swiWaitForVBlank();

		cameraTransferStart(previewGraphics(), CAPTURE_MODE_PREVIEW);
		while(cameraTransferActive())
			swiDelay(100);

		if(fwrite(previewGraphics(), 1, BIN_FRAME_PIXEL_BYTES, out) != (size_t)BIN_FRAME_PIXEL_BYTES) {
			cameraDeactivate(CAM_OUTER);
			fclose(out);

			consoleClear();
			printf("Unable to write the recording\n%s\n\nA: menu", vidName);
			waitForKey(KEY_A);
			return;
		}

		u32 time = cpuGetTiming();
		fwrite(&time, 4, 1, out);
		cpuStartTiming(0);

		scanKeys();
		if(keysDown() & KEY_START) {
			// Disable camera so the light turns off
			cameraDeactivate(CAM_OUTER);
			fclose(out);

			consoleClear();
			printf("Saved %d frames\n%s\n\nA: menu", fileFrames(vidName), vidName);
			waitForKey(KEY_A);
			return;
		}
	}
}

static void showMenu(void) {
	consoleClear();
	printf("dsi-camcorder " VER_NUMBER "\n\n");
	printf("A: record a video\n");
	printf("B: convert a recording\n\n");
	printf("START: quit\n\n");
	printf("recordings live in\n%s\n", RECORDING_DIRECTORY);
}

int main(int argc, char **argv) {
	bool cameraReady = false;

	consoleDemoInit();
	previewInit();
	previewClear();

	if(!fatInitDefault()) {
		printf("FAT init failed!\n\nSTART to quit\n");
		waitForKey(KEY_START);
		return 0;
	}

	mkdir("/DCIM", 0777);
	mkdir(RECORDING_DIRECTORY, 0777);

	while(true) {
		showMenu();
		waitForKey(KEY_A | KEY_B | KEY_START);

		if(keysDown() & KEY_START)
			break;

		if(keysDown() & KEY_B) {
			convertRecording();
			continue;
		}

		if(!cameraReady) {
			consoleClear();
			printf("Initializing camera...\n");

			pxiWaitRemote(PXI_CAMERA); // Wait for ARM7 to initialize PXI
			cameraReady = cameraInit();

			if(!cameraReady) {
				consoleClear();
				printf("Camera init failed\n\nA: menu");
				waitForKey(KEY_A);
				continue;
			}
		}

		recordVideo();
	}

	previewClear();
	return 0;
}
