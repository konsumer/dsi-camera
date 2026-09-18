#ifndef GIF_H
#define GIF_H

#include <nds/ndstypes.h>
#include <stdio.h>

// Small GIF89a encoder, enough to turn camera recordings into animated GIFs
// without a computer. Every frame is written with its own 256 entry local color
// table, so the caller can either share one palette or pick a new one per frame.

typedef struct {
	FILE *file;
	int width;
	int height;
	u32 bitBuffer;
	int bitCount;
	u8 block[255];
	int blockLen;
} GifEncoder;

// Writes the GIF89a header, the logical screen descriptor and the endless loop
// extension. No global color table is emitted, frames carry their own.
bool gifEncoderInit(GifEncoder *encoder, FILE *file, int width, int height);

// Writes one frame covering the whole canvas. `indices` holds width * height
// palette indices, `palette` 256 RGB triplets and `delay` the frame time in
// hundredths of a second, the unit GIF stores delays in.
bool gifEncoderWriteFrame(GifEncoder *encoder, const u8 *indices, const u8 *palette, u16 delay);

// Writes the trailer that terminates the file
bool gifEncoderFinish(GifEncoder *encoder);

#endif // GIF_H
