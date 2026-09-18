#include "gif.h"

#include <string.h>

// LZW with 8 bit symbols: code 256 clears the dictionary, 257 ends the image and
// codes from 258 up are dictionary entries. Codes grow from 9 to 12 bits.
#define LZW_MIN_CODE_SIZE 8
#define LZW_CLEAR_CODE (1 << LZW_MIN_CODE_SIZE)
#define LZW_END_CODE (LZW_CLEAR_CODE + 1)
#define LZW_MAX_CODES 4096

// Dictionary of (prefix code, pixel) pairs, open addressed. Four entries are
// always free because the table is twice the size of the code space.
#define DICT_BITS 13
#define DICT_SIZE (1 << DICT_BITS)
#define DICT_MASK (DICT_SIZE - 1)
#define DICT_EMPTY 0xFFFFFFFF

static u32 dictKeys[DICT_SIZE];
static u16 dictCodes[DICT_SIZE];

static u32 dictHash(u32 key) {
	return (key * 2654435761u) >> (32 - DICT_BITS);
}

static bool dictLookup(u32 key, u16 *code) {
	u32 index = dictHash(key);

	while(dictKeys[index] != DICT_EMPTY) {
		if(dictKeys[index] == key) {
			*code = dictCodes[index];
			return true;
		}
		index = (index + 1) & DICT_MASK;
	}

	return false;
}

static void dictInsert(u32 key, u16 code) {
	u32 index = dictHash(key);

	while(dictKeys[index] != DICT_EMPTY) {
		if(dictKeys[index] == key) {
			dictCodes[index] = code;
			return;
		}
		index = (index + 1) & DICT_MASK;
	}

	dictKeys[index] = key;
	dictCodes[index] = code;
}

static void dictClear(void) {
	memset(dictKeys, 0xFF, sizeof(dictKeys));
}

static bool writeBlock(GifEncoder *encoder) {
	if(encoder->blockLen == 0)
		return true;

	bool ok = fputc(encoder->blockLen, encoder->file) != EOF &&
	          fwrite(encoder->block, 1, encoder->blockLen, encoder->file) == (size_t)encoder->blockLen;

	encoder->blockLen = 0;
	return ok;
}

// Sub blocks hold at most 255 bytes
static bool writeByte(GifEncoder *encoder, u8 byte) {
	encoder->block[encoder->blockLen++] = byte;

	if(encoder->blockLen == sizeof(encoder->block))
		return writeBlock(encoder);

	return true;
}

// Codes are packed least significant bit first
static bool writeCode(GifEncoder *encoder, u16 code, int bits) {
	encoder->bitBuffer |= (u32)code << encoder->bitCount;
	encoder->bitCount += bits;

	while(encoder->bitCount >= 8) {
		if(!writeByte(encoder, encoder->bitBuffer & 0xFF))
			return false;
		encoder->bitBuffer >>= 8;
		encoder->bitCount -= 8;
	}

	return true;
}

static bool encodeIndices(GifEncoder *encoder, const u8 *indices, int count) {
	int codeSize = LZW_MIN_CODE_SIZE + 1;
	int nextCode = LZW_END_CODE + 1;
	u16 prefix;

	if(count <= 0)
		return true;

	dictClear();

	if(!writeCode(encoder, LZW_CLEAR_CODE, codeSize))
		return false;

	prefix = indices[0];

	for(int i = 1; i < count; i++) {
		u32 key = ((u32)prefix << 8) | indices[i];
		u16 code;

		if(dictLookup(key, &code)) {
			prefix = code;
			continue;
		}

		if(!writeCode(encoder, prefix, codeSize))
			return false;

		dictInsert(key, nextCode);
		nextCode++;

		if(nextCode == LZW_MAX_CODES) {
			// The dictionary cannot grow past 12 bit codes, start over
			if(!writeCode(encoder, LZW_CLEAR_CODE, codeSize))
				return false;

			dictClear();
			codeSize = LZW_MIN_CODE_SIZE + 1;
			nextCode = LZW_END_CODE + 1;
		} else if(nextCode > (1 << codeSize)) {
			codeSize++;
		}

		prefix = indices[i];
	}

	if(!writeCode(encoder, prefix, codeSize))
		return false;

	if(!writeCode(encoder, LZW_END_CODE, codeSize))
		return false;

	// Pad the last partial byte, then terminate the sub block chain
	if(encoder->bitCount > 0) {
		if(!writeByte(encoder, encoder->bitBuffer & 0xFF))
			return false;
		encoder->bitBuffer = 0;
		encoder->bitCount = 0;
	}

	if(!writeBlock(encoder))
		return false;

	return fputc(0, encoder->file) != EOF;
}

bool gifEncoderInit(GifEncoder *encoder, FILE *file, int width, int height) {
	const u8 signature[] = {'G', 'I', 'F', '8', '9', 'a'};
	u8 screen[] = {0, 0, 0, 0, 0, 0, 0};
	const u8 loop[] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
	                   0x03, 0x01, 0x00, 0x00, 0x00};

	encoder->file = file;
	encoder->width = width;
	encoder->height = height;
	encoder->bitBuffer = 0;
	encoder->bitCount = 0;
	encoder->blockLen = 0;

	screen[0] = width & 0xFF;
	screen[1] = (width >> 8) & 0xFF;
	screen[2] = height & 0xFF;
	screen[3] = (height >> 8) & 0xFF;
	// No global color table, no background color, square pixels

	return fwrite(signature, 1, sizeof(signature), file) == sizeof(signature) &&
	       fwrite(screen, 1, sizeof(screen), file) == sizeof(screen) &&
	       fwrite(loop, 1, sizeof(loop), file) == sizeof(loop);
}

bool gifEncoderWriteFrame(GifEncoder *encoder, const u8 *indices, const u8 *palette, u16 delay) {
	// Graphic control extension: disposal method 1 (leave the frame in place)
	u8 control[] = {0x21, 0xF9, 0x04, 0x04, delay & 0xFF, (delay >> 8) & 0xFF, 0x00, 0x00};
	// Image descriptor: full canvas, local color table with 256 entries
	u8 descriptor[] = {0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87};

	if(encoder->file == NULL)
		return false;

	descriptor[5] = encoder->width & 0xFF;
	descriptor[6] = (encoder->width >> 8) & 0xFF;
	descriptor[7] = encoder->height & 0xFF;
	descriptor[8] = (encoder->height >> 8) & 0xFF;

	if(fwrite(control, 1, sizeof(control), encoder->file) != sizeof(control))
		return false;

	if(fwrite(descriptor, 1, sizeof(descriptor), encoder->file) != sizeof(descriptor))
		return false;

	if(fwrite(palette, 1, 256 * 3, encoder->file) != 256 * 3)
		return false;

	if(fputc(LZW_MIN_CODE_SIZE, encoder->file) == EOF)
		return false;

	return encodeIndices(encoder, indices, encoder->width * encoder->height);
}

bool gifEncoderFinish(GifEncoder *encoder) {
	if(encoder->file == NULL)
		return false;

	return fputc(0x3B, encoder->file) != EOF;
}
