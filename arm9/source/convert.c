#include "convert.h"

#include "gif.h"

#include <stdio.h>
#include <string.h>

// The recorder times frames with cpuGetTiming(), which is calico's monotonic tick
// counter (timer 2, prescaler 64 on the 33.513982MHz timer clock) shifted left by
// 6: 33513982 / (64 * 64) = 8182 units per second.
#define TIMING_UNITS_PER_SECOND 8182

// GIF delays are hundredths of a second, viewers refuse to go below two
#define MIN_DELAY_CS 2
#define MAX_DELAY_CS 65535

// The palette is picked from a histogram over the 15 bit RGB555 color space
#define COLOR_LEVELS 32
#define COLOR_COUNT (COLOR_LEVELS * COLOR_LEVELS * COLOR_LEVELS)
#define COLOR_INDEX(r, g, b) (((b) << 10) | ((g) << 5) | (r))

typedef struct {
	u8 rMin, rMax, gMin, gMax, bMin, bMax;
	u32 count;
} ColorBox;

static u32 colorHistogram[COLOR_COUNT];
static u8 colorLookup[COLOR_COUNT];
static ColorBox colorBoxes[BIN_PALETTE_COLORS];
static int colorBoxCount;

// Frame and the four delay bytes that follow it
static u16 frameBuffer[BIN_FRAME_PIXELS + BIN_FRAME_DELAY_BYTES / 2];
static u8 frameIndices[BIN_FRAME_PIXELS];
static u8 framePalette[BIN_PALETTE_COLORS * 3];
static u8 paletteLevels[BIN_PALETTE_COLORS * 3];

static u32 readDelay(const u16 *frame) {
	return frame[BIN_FRAME_PIXELS] | ((u32)frame[BIN_FRAME_PIXELS + 1] << 16);
}

static void addFrameToHistogram(const u16 *frame) {
	for(int i = 0; i < BIN_FRAME_PIXELS; i++)
		colorHistogram[frame[i] & 0x7FFF]++;
}

// 5 bit to 8 bit, replicating the top bits so full white stays full white
static u8 expandColor(u32 level) {
	return (level << 3) | (level >> 2);
}

static u16 delayToCentiseconds(u32 units, bool recordedTiming) {
	u32 centiseconds;

	if(!recordedTiming)
		return 100 / BIN_FALLBACK_FPS;

	centiseconds = (u32)(((u64)units * 100 + TIMING_UNITS_PER_SECOND / 2) / TIMING_UNITS_PER_SECOND);

	if(centiseconds < MIN_DELAY_CS)
		centiseconds = MIN_DELAY_CS;
	else if(centiseconds > MAX_DELAY_CS)
		centiseconds = MAX_DELAY_CS;

	return (u16)centiseconds;
}

static bool boxIsSplittable(const ColorBox *box) {
	return box->rMin < box->rMax || box->gMin < box->gMax || box->bMin < box->bMax;
}

// Counts the pixels of a box along one axis and reports the range it occupies
static void boxAxisCounts(const ColorBox *box, int axis, u32 counts[COLOR_LEVELS], int *first, int *last) {
	memset(counts, 0, COLOR_LEVELS * sizeof(u32));

	for(int r = box->rMin; r <= box->rMax; r++) {
		for(int g = box->gMin; g <= box->gMax; g++) {
			for(int b = box->bMin; b <= box->bMax; b++) {
				u32 count = colorHistogram[COLOR_INDEX(r, g, b)];

				if(count != 0)
					counts[axis == 0 ? r : axis == 1 ? g : b] += count;
			}
		}
	}

	*first = -1;
	*last = -1;

	for(int level = 0; level < COLOR_LEVELS; level++) {
		if(counts[level] == 0)
			continue;
		if(*first < 0)
			*first = level;
		*last = level;
	}
}

// Splits the box along the median of its longest axis. Returns false when the box
// holds a single color, collapsing it so it is not picked again.
static bool splitBox(int index) {
	ColorBox box = colorBoxes[index];
	int extents[3] = {box.rMax - box.rMin, box.gMax - box.gMin, box.bMax - box.bMin};
	u32 counts[3][COLOR_LEVELS];
	int first[3];
	int last[3];
	int axis = -1;
	int split = 0;

	for(int candidate = 0; candidate < 3; candidate++) {
		boxAxisCounts(&box, candidate, counts[candidate], &first[candidate], &last[candidate]);

		if(last[candidate] <= first[candidate])
			continue;

		if(axis < 0 || extents[candidate] > extents[axis])
			axis = candidate;
	}

	if(axis < 0) {
		// A single color: keep the palette entry, drop the empty space around it
		for(int r = box.rMin; r <= box.rMax; r++) {
			for(int g = box.gMin; g <= box.gMax; g++) {
				for(int b = box.bMin; b <= box.bMax; b++) {
					if(colorHistogram[COLOR_INDEX(r, g, b)] == 0)
						continue;

					box.rMin = box.rMax = r;
					box.gMin = box.gMax = g;
					box.bMin = box.bMax = b;
					colorBoxes[index] = box;
					return false;
				}
			}
		}

		box.rMax = box.rMin;
		box.gMax = box.gMin;
		box.bMax = box.bMin;
		colorBoxes[index] = box;
		return false;
	}

	u32 half = box.count / 2;
	u32 cumulative = 0;

	for(int level = first[axis]; level < last[axis]; level++) {
		cumulative += counts[axis][level];
		split = level;
		if(cumulative > half)
			break;
	}

	ColorBox right = box;

	switch(axis) {
	case 0:
		box.rMax = split;
		right.rMin = split + 1;
		break;
	case 1:
		box.gMax = split;
		right.gMin = split + 1;
		break;
	default:
		box.bMax = split;
		right.bMin = split + 1;
		break;
	}

	u32 leftCount = 0;

	for(int level = first[axis]; level <= split; level++)
		leftCount += counts[axis][level];

	box.count = leftCount;
	right.count -= leftCount;

	colorBoxes[index] = box;
	colorBoxes[colorBoxCount++] = right;
	return true;
}

// Builds a shared palette and the RGB555 to palette index lookup table
static void buildPalette(void) {
	u32 total = 0;

	for(int i = 0; i < COLOR_COUNT; i++)
		total += colorHistogram[i];

	colorBoxCount = 1;
	colorBoxes[0] = (ColorBox){0, COLOR_LEVELS - 1, 0, COLOR_LEVELS - 1, 0, COLOR_LEVELS - 1, total};

	while(colorBoxCount < BIN_PALETTE_COLORS) {
		int best = -1;
		u32 bestCount = 0;

		for(int i = 0; i < colorBoxCount; i++) {
			if(colorBoxes[i].count <= bestCount || !boxIsSplittable(&colorBoxes[i]))
				continue;

			best = i;
			bestCount = colorBoxes[i].count;
		}

		if(best < 0)
			break;

		splitBox(best);
	}

	memset(colorLookup, 0xFF, sizeof(colorLookup));

	for(int i = 0; i < colorBoxCount; i++) {
		ColorBox box = colorBoxes[i];
		u64 rSum = 0;
		u64 gSum = 0;
		u64 bSum = 0;
		u32 count = 0;

		for(int r = box.rMin; r <= box.rMax; r++) {
			for(int g = box.gMin; g <= box.gMax; g++) {
				for(int b = box.bMin; b <= box.bMax; b++) {
					u32 binCount = colorHistogram[COLOR_INDEX(r, g, b)];

					colorLookup[COLOR_INDEX(r, g, b)] = i;
					rSum += (u64)binCount * r;
					gSum += (u64)binCount * g;
					bSum += (u64)binCount * b;
					count += binCount;
				}
			}
		}

		if(count != 0) {
			paletteLevels[i * 3 + 0] = rSum / count;
			paletteLevels[i * 3 + 1] = gSum / count;
			paletteLevels[i * 3 + 2] = bSum / count;
		} else {
			paletteLevels[i * 3 + 0] = (box.rMin + box.rMax) / 2;
			paletteLevels[i * 3 + 1] = (box.gMin + box.gMax) / 2;
			paletteLevels[i * 3 + 2] = (box.bMin + box.bMax) / 2;
		}
	}

	// Colors the sampled frames missed still have to map somewhere sensible: boxes
	// collapsed to a single color leave holes in the lookup table.
	for(int bin = 0; bin < COLOR_COUNT; bin++) {
		if(colorLookup[bin] != 0xFF)
			continue;

		int r = bin & 31;
		int g = (bin >> 5) & 31;
		int b = (bin >> 10) & 31;
		int nearest = 0;
		u32 nearestDistance = 0xFFFFFFFF;

		for(int i = 0; i < colorBoxCount; i++) {
			int dr = r - paletteLevels[i * 3 + 0];
			int dg = g - paletteLevels[i * 3 + 1];
			int db = b - paletteLevels[i * 3 + 2];
			u32 distance = dr * dr + dg * dg + db * db;

			if(distance < nearestDistance) {
				nearest = i;
				nearestDistance = distance;
			}
		}

		colorLookup[bin] = nearest;
	}

	for(int i = 0; i < BIN_PALETTE_COLORS; i++) {
		framePalette[i * 3 + 0] = expandColor(i < colorBoxCount ? paletteLevels[i * 3 + 0] : 0);
		framePalette[i * 3 + 1] = expandColor(i < colorBoxCount ? paletteLevels[i * 3 + 1] : 0);
		framePalette[i * 3 + 2] = expandColor(i < colorBoxCount ? paletteLevels[i * 3 + 2] : 0);
	}
}

BinConvertStatus binToGif(const char *binPath, const char *gifPath, BinConvertFrameFn onFrame, void *user, BinConvertInfo *info) {
	FILE *input;
	FILE *output;
	GifEncoder encoder;
	BinConvertStatus status = BIN_CONVERT_OK;
	bool recordedTiming = false;
	u64 delayUnits = 0;
	int delaysRead = 0;
	int totalFrames;
	long size;

	memset(info, 0, sizeof(*info));

	input = fopen(binPath, "rb");
	if(input == NULL)
		return BIN_CONVERT_OPEN_FAILED;

	if(fseek(input, 0, SEEK_END) != 0) {
		fclose(input);
		return BIN_CONVERT_READ_ERROR;
	}

	size = ftell(input);
	totalFrames = size > 0 ? (int)(size / BIN_FRAME_BYTES) : 0;

	if(totalFrames <= 0) {
		fclose(input);
		return BIN_CONVERT_EMPTY;
	}

	info->totalFrames = totalFrames;

	// Prefer the delays stored with the recording over any fixed frame rate: they
	// are what the camera actually managed to keep up with.
	for(int frame = 0; frame < totalFrames; frame++) {
		u16 raw[2];

		if(fseek(input, (long)frame * BIN_FRAME_BYTES + BIN_FRAME_PIXEL_BYTES, SEEK_SET) != 0)
			break;
		if(fread(raw, 1, BIN_FRAME_DELAY_BYTES, input) != (size_t)BIN_FRAME_DELAY_BYTES)
			break;

		delayUnits += raw[0] | ((u32)raw[1] << 16);
		delaysRead++;
	}

	if(delaysRead > 0 && delayUnits > 0) {
		double averageUnits = (double)delayUnits / delaysRead;
		double fps = TIMING_UNITS_PER_SECOND / averageUnits;

		if(fps >= 1.0 && fps <= 60.0) {
			recordedTiming = true;
			info->playbackFps = (int)(fps + 0.5);
		}
	}

	if(!recordedTiming)
		info->playbackFps = BIN_FALLBACK_FPS;

	info->recordedTiming = recordedTiming;

	// One palette for the whole animation, sampled from a spread of frames
	memset(colorHistogram, 0, sizeof(colorHistogram));

	int samples = totalFrames < BIN_PALETTE_SAMPLES ? totalFrames : BIN_PALETTE_SAMPLES;

	for(int i = 0; i < samples; i++) {
		int frame = samples > 1 ? (int)((u64)i * (totalFrames - 1) / (samples - 1)) : 0;

		if(fseek(input, (long)frame * BIN_FRAME_BYTES, SEEK_SET) != 0)
			break;
		if(fread(frameBuffer, 1, BIN_FRAME_PIXEL_BYTES, input) != (size_t)BIN_FRAME_PIXEL_BYTES)
			break;

		addFrameToHistogram(frameBuffer);
	}

	buildPalette();

	output = fopen(gifPath, "wb");
	if(output == NULL) {
		fclose(input);
		return BIN_CONVERT_OPEN_FAILED;
	}

	if(!gifEncoderInit(&encoder, output, BIN_FRAME_WIDTH, BIN_FRAME_HEIGHT)) {
		status = BIN_CONVERT_WRITE_ERROR;
	} else if(fseek(input, 0, SEEK_SET) != 0) {
		status = BIN_CONVERT_READ_ERROR;
	} else {
		for(int frame = 0; frame < totalFrames; frame++) {
			if(fread(frameBuffer, 1, BIN_FRAME_BYTES, input) != (size_t)BIN_FRAME_BYTES) {
				status = BIN_CONVERT_READ_ERROR;
				break;
			}

			for(int i = 0; i < BIN_FRAME_PIXELS; i++)
				frameIndices[i] = colorLookup[frameBuffer[i] & 0x7FFF];

			if(onFrame != NULL && !onFrame(user, frameBuffer, frame, totalFrames)) {
				status = BIN_CONVERT_ABORTED;
				break;
			}

			if(!gifEncoderWriteFrame(&encoder, frameIndices, framePalette, delayToCentiseconds(readDelay(frameBuffer), recordedTiming))) {
				status = BIN_CONVERT_WRITE_ERROR;
				break;
			}

			info->convertedFrames = frame + 1;
		}

		if(status == BIN_CONVERT_OK && !gifEncoderFinish(&encoder))
			status = BIN_CONVERT_WRITE_ERROR;
	}

	if(status == BIN_CONVERT_OK) {
		long written = ftell(output);

		info->outputBytes = written > 0 ? (u32)written : 0;
	}

	fclose(output);
	fclose(input);

	if(status != BIN_CONVERT_OK)
		remove(gifPath);

	return status;
}
