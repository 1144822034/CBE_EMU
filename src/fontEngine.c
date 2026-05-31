#include "fontEngine.h"

int fontWidth = 0;
int fontHeight = 0;
int fontCount = 0;
char *fontFileData;
char *fontCharsStartPtr;
// 一个字符的bitmap大小
int bitmapDataSize = 0;
int linePitch = 0;

void InitFontEngine()
{
    // 字体引擎初始化（如果有需要的话）

    int fileSize;
    fontFileData = readFile("font_gb.uc3", &fileSize);
    if (fontFileData == NULL)
    {
        printf("Failed to load font_gb.uc3\n");
        assert(0);
    }
    fontWidth = ((int *)fontFileData)[1];
    fontHeight = ((int *)fontFileData)[2];
    fontCount = ((int *)fontFileData)[3];
    fontCharsStartPtr = (char *)(fontFileData + 16);
    // fixme 这里是理想情况，如果字体宽度不是8的倍数，可能会有问题
    bitmapDataSize = (fontWidth * fontHeight) / 8;
    linePitch = fontWidth / 8;
}
inline int getFontWidth()
{
    return fontWidth;
}

inline int getFontHeight()
{
    return fontHeight;
}

inline int getFontCellWidth()
{
    return fontWidth / 2;
}
// 一个字符32字节，16x16大小，一行16个像素点就是2字节，所以16x16就是32字节
u8 getFontBitMap(u16 gbCode, char *bitmapData)
{
    // todo 二分法查找优化
    for (int i = 0; i < fontCount; i++)
    {
        u16 *gb = (u16 *)(fontCharsStartPtr + i * 6);
        u32 *off = (u32 *)(fontCharsStartPtr + i * 6 + 2);
        if (gb[0] == gbCode)
        {
            u32 offset = off[0];
            my_memcpy(bitmapData, fontFileData + offset, bitmapDataSize);
            return 1;
        }
    }
    return 0;
}
void drawFontChar(u16 gbCode, int x, int y, u16 color)
{
    char *bitMapData = SDL_malloc(bitmapDataSize);
    if (bitMapData == NULL)
        return;
    if (getFontBitMap(gbCode, bitMapData))
    {
        for (int j = 0; j < fontHeight; j++)
        {
            int py = y + j;
            if (py < 0 || py >= LCD_HEIGHT)
                continue;
            for (int i = 0; i < fontWidth; i++)
            {
                int px = x + i;
                if (px < 0 || px >= LCD_WIDTH)
                    continue;
                int byteIndex = j * linePitch + i / 8;
                int bitIndex = 7 - (i % 8);
                if ((bitMapData[byteIndex] >> bitIndex) & 1)
                {
                    int offset = py * LCD_WIDTH + px;
                    ((u16 *)Lcd_Cache_Buffer)[offset] = color;
                }
            }
        }
    }
    else
    {
        // printf("字库中不存在字符%x，无法绘制\n", gbCode);
    }
    SDL_free(bitMapData);
}

void drawFontString(u8 *gbkStr, int x, int y, u16 color)
{
    u8 *start = gbkStr;
    u32 i = 0;
    u32 ri = 0;
    u16 c;
    while (1)
    {
        c = gbkStr[i];
        if (c == 0)
            break;
        else if (c < 0x80)
        {
            drawFontChar((c << 8), x + ri, y, color);
            ri += getFontCellWidth();
            i += 1;
        }
        else
        {
            c = (gbkStr[i] ) | (gbkStr[i + 1]<< 8);
            drawFontChar(c, x + ri, y, color);
            ri += fontWidth;
            i += 2;
        }
    }
}

int mesureStringWidth(char *gbkStr)
{
    if (gbkStr == NULL)
        return 0;
    int width = 0;
    for (u32 i = 0; gbkStr[i] != 0;)
    {
        u8 ch = (u8)gbkStr[i];
        if (ch < 0x80)
        {
            width += getFontCellWidth();
            i += 1;
        }
        else
        {
            width += getFontWidth();
            i += gbkStr[i + 1] ? 2 : 1;
        }
    }
    return width;
}
