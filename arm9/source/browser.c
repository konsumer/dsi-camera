#include "browser.h"

#include "convert.h"
#include "preview.h"

#include <dirent.h>
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define MAX_ENTRIES 128
#define NAME_LENGTH 64
#define VISIBLE_ROWS 18
#define ROW_WIDTH 32

// Key repeat while holding up or down
#define HOLD_DELAY 20
#define HOLD_REPEAT 4

typedef struct {
	char name[NAME_LENGTH];
	bool directory;
} BrowserEntry;

static BrowserEntry entries[MAX_ENTRIES];
static char currentPath[BROWSER_PATH_LENGTH];

static bool isRecording(const char *name) {
	size_t length = strlen(name);

	return length > 4 && strcasecmp(name + length - 4, ".bin") == 0;
}

// Joins a directory and an entry name, false when the result does not fit
static bool joinPath(char *out, int outLen, const char *directory, const char *name) {
	bool root = strcmp(directory, "/") == 0;
	int directoryLength = root ? 1 : (int)strlen(directory);
	int nameLength = (int)strlen(name);
	int resultLength = (root ? 1 : directoryLength + 1) + nameLength;

	if(resultLength + 1 > outLen)
		return false;

	if(root) {
		out[0] = '/';
	} else {
		memmove(out, directory, directoryLength);
		out[directoryLength] = '/';
	}

	memmove(out + resultLength - nameLength, name, nameLength + 1);

	return true;
}

static int compareEntries(const void *left, const void *right) {
	const BrowserEntry *a = left;
	const BrowserEntry *b = right;

	if(a->directory != b->directory)
		return a->directory ? -1 : 1;

	return strcasecmp(a->name, b->name);
}

// Fills the entry list, returns the entry count or -1 if the directory is unreadable
static int readDirectory(const char *path) {
	DIR *dir = opendir(path);
	struct dirent *entry;
	int count = 0;

	if(dir == NULL)
		return -1;

	while(count < MAX_ENTRIES && (entry = readdir(dir)) != NULL) {
		char full[BROWSER_PATH_LENGTH];
		struct stat info;
		bool directory;

		if(strcmp(entry->d_name, ".") == 0 || strlen(entry->d_name) >= NAME_LENGTH)
			continue;

		if(!joinPath(full, sizeof(full), path, entry->d_name))
			continue;

		directory = stat(full, &info) == 0 && S_ISDIR(info.st_mode);

		if(!directory && !isRecording(entry->d_name))
			continue;

		strcpy(entries[count].name, entry->d_name);
		entries[count].directory = directory;
		count++;
	}

	closedir(dir);
	qsort(entries, count, sizeof(BrowserEntry), compareEntries);

	return count;
}

static void printEntry(const BrowserEntry *entry, bool selected) {
	int maxName = ROW_WIDTH - 2 - (entry->directory ? 1 : 0);
	int nameLength = strlen(entry->name);
	int printed = nameLength > maxName ? maxName : nameLength;

	if(selected)
		printf("\x1b[33m"); // highlight the entry under the cursor

	printf("%c%.*s%s", selected ? '>' : ' ', printed, entry->name, entry->directory ? "/" : "");
	printf("\x1b[39m");

	for(int i = printed + (entry->directory ? 2 : 1); i < ROW_WIDTH - 1; i++)
		printf(" ");

	printf("\n");
}

static void drawScreen(const char *path, int count, int selected, int top) {
	int pathLength = strlen(path);
	int tail = pathLength > ROW_WIDTH - 1 ? ROW_WIDTH - 1 : pathLength;

	consoleClear();
	printf("Pick a recording\n");
	printf("%.*s\n", tail, path + pathLength - tail);

	// One column is left free so the console never wraps a row
	for(int i = 0; i < ROW_WIDTH - 1; i++)
		printf("-");

	printf("\n");

	for(int row = 0; row < VISIBLE_ROWS; row++) {
		int index = top + row;

		if(index >= count) {
			printf("\n");
			continue;
		}

		printEntry(&entries[index], index == selected);
	}

	printf("\n");

	if(count <= 0) {
		printf("no recordings here\n");
	} else if(entries[selected].directory) {
		printf("directory\n");
	} else {
		char full[BROWSER_PATH_LENGTH];
		struct stat info;

		if(joinPath(full, sizeof(full), path, entries[selected].name) && stat(full, &info) == 0)
			printf("%d frames, %dKB\n", (int)(info.st_size / BIN_FRAME_BYTES), (int)(info.st_size / 1024));
	}

	printf("A open  B up  START back");
}

bool browserPickBin(char *outPath, int outPathLen, const char *startDirectory) {
	int selected = 0;
	int top = 0;
	int previewed = -1;
	int count;
	int hold = 0;
	bool redraw = true;

	int startLength = strlen(startDirectory);

	if(startLength >= (int)sizeof(currentPath))
		startLength = sizeof(currentPath) - 1;

	memcpy(currentPath, startDirectory, startLength);
	currentPath[startLength] = '\0';

	count = readDirectory(currentPath);

	while(true) {
		if(redraw) {
			if(selected < top)
				top = selected;
			else if(selected >= top + VISIBLE_ROWS)
				top = selected - VISIBLE_ROWS + 1;

			drawScreen(currentPath, count, selected, top);

			if(previewed != selected) {
				previewed = selected;

				if(selected < count && !entries[selected].directory) {
					char full[BROWSER_PATH_LENGTH];

					if(joinPath(full, sizeof(full), currentPath, entries[selected].name))
						previewFile(full);
					else
						previewClear();
				} else {
					previewClear();
				}
			}

			redraw = false;
		}

		swiWaitForVBlank();
		scanKeys();

		u32 down = keysDown();
		u32 held = keysHeld();
		bool moveUp = (down & KEY_UP) != 0;
		bool moveDown = (down & KEY_DOWN) != 0;
		int delta = 0;

		if(down & KEY_START) {
			previewClear();
			return false;
		}

		if(moveUp || moveDown) {
			hold = HOLD_DELAY;
		} else if(held & (KEY_UP | KEY_DOWN)) {
			if(hold > 0) {
				hold--;
			} else {
				moveUp = (held & KEY_UP) != 0;
				moveDown = (held & KEY_DOWN) != 0;
				hold = HOLD_REPEAT;
			}
		} else {
			hold = 0;
		}

		if(moveUp != moveDown)
			delta = moveUp ? -1 : 1;

		if(delta != 0 && selected + delta >= 0 && selected + delta < count) {
			selected += delta;
			redraw = true;
			continue;
		}

		if(down & KEY_B) {
			if(strcmp(currentPath, "/") != 0) {
				char *slash = strrchr(currentPath, '/');

				if(slash == currentPath)
					currentPath[1] = '\0';
				else if(slash != NULL)
					*slash = '\0';

				count = readDirectory(currentPath);
				selected = 0;
				top = 0;
				previewed = -1;
				redraw = true;
			}
			continue;
		}

		if(down & KEY_A) {
			if(count <= 0)
				continue;

			if(entries[selected].directory) {
				if(!joinPath(currentPath, sizeof(currentPath), currentPath, entries[selected].name))
					continue;

				count = readDirectory(currentPath);
				selected = 0;
				top = 0;
				previewed = -1;
				redraw = true;
			} else if(joinPath(outPath, outPathLen, currentPath, entries[selected].name)) {
				return true;
			}
		}
	}
}
