#include <stdio.h>
#include <sys/stat.h>
#include "config.h"
#ifdef CBE_PLATFORM_NO_WINDOW
#include "android_compat.h"
#else
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"
#endif

int writeFile(const char *filename, void *buff, u32 size);
u8 *readFile(const char *filename, u32 *size);
int dirExists(char *path);
