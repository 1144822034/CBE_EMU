#include <stdio.h>
#include <sys/stat.h>
#include "config.h"
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"

int writeFile(const char *filename, void *buff, u32 size);
u8 *readFile(const char *filename, u32 *size);