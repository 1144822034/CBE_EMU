#include <assert.h>
#include "config.h"
#include "mystd.h"
#include "fileIoEngine.h"
#include "lcd.h"
#include "mystd.h"

typedef struct fontChar
{
    u16 unicode;
    u32 offset;
} fontChar;

void InitFontEngine();
int getFontWidth();
int getFontCellWidth();
int getFontAsciiDrawWidth();
int getFontHeight();
u8 getFontBitMap(u16 unicode, char *bitmapData);
void drawFontChar(u16 unicode, int x, int y, u16 color);
void drawFontCharWithWidth(u16 unicode, int x, int y, u16 color, int drawWidth);
int mesureStringWidth(char *str);
int mesureStringWidthWithGbkWidth(char *str, int gbkWidth);
int mesureStringRenderWidthWithGbkWidth(char *str, int gbkWidth);
void drawFontString(u8 *unicodeStr, int x, int y, u16 color);
void drawFontStringWithGbkWidth(u8 *unicodeStr, int x, int y, u16 color, int gbkWidth);
