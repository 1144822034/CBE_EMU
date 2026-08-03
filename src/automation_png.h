#pragma once

#include "config.h"

/* Writes an RGB565 LCD snapshot as a self-contained RGB PNG.  The caller
 * owns the source buffer and this routine never touches the emulator window
 * or guest memory. */
int automation_png_write_rgb565(const char *path, const u8 *pixels,
                                u32 width, u32 height);
