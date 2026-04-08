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
int getFontHeight();
u8 getFontBitMap(u16 unicode, char *bitmapData);
void drawFontChar(u16 unicode, int x, int y, u16 color);
int mesureStringWidth(char *str);
void drawFontString(u8 *unicodeStr, int x, int y, u16 color);