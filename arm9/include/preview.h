#ifndef PREVIEW_H
#define PREVIEW_H

#include <nds/ndstypes.h>

// Camera frames shown on the top screen: the preview the camera is streaming into
// while recording, or the frame that is currently being converted.

void previewInit(void);

// Graphics memory of the top screen background the camera streams into
u16 *previewGraphics(void);

// Shows a 256x192 RGB555 frame, the same format the camera and recordings use
void previewBitmap(const u16 *frame);

// Blanks the preview
void previewClear(void);

// Shows the first frame of a recording, false when it could not be read
bool previewFile(const char *binPath);

#endif // PREVIEW_H
