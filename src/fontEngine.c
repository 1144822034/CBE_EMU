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
    fontFileData = readFile("gb16.uc2.uc3", &fileSize);
    if (fontFileData == NULL)
    {
        printf("Failed to load gb16.uc2.uc3\n");
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
int getFontWidth()
{
    return fontWidth;
}

int getFontHeight()
{
    return fontHeight;
}
// 一个字符32字节，16x16大小，一行16个像素点就是2字节，所以16x16就是32字节
u8 getFontBitMap(u16 unicode, char *bitmapData)
{
    for (int i = 0; i < fontCount; i++)
    {
        u16 *uni = (u16 *)(fontCharsStartPtr + i * 6);
        u32 *off = (u32 *)(fontCharsStartPtr + i * 6 + 2);
        if (uni[0] == unicode)
        {
            u32 offset = off[0];
            my_memcpy(bitmapData, fontFileData + offset, bitmapDataSize);
            return 1;
        }
    }
    return 0;
}
void drawFontChar(u16 unicode, int x, int y, u16 color)
{
    char *bitMapData = SDL_malloc(bitmapDataSize);
    if (getFontBitMap(unicode, bitMapData))
    {
        for (int j = 0; j < fontHeight; j++)
        {
            for (int i = 0; i < fontWidth; i++)
            {
                int byteIndex = j * linePitch + i / 8;
                int bitIndex = 7 - (i % 8);
                if ((bitMapData[byteIndex] >> bitIndex) & 1)
                {
                    // 设置像素点
                    int offset = (y + j) * LCD_WIDTH + (x + i);
                    ((u16 *)Lcd_Cache_Buffer)[offset] = color;
                }
            }
        }
    }
    else
    {
        printf("字库中不存在字符%x，无法绘制", unicode);
    }
    SDL_free(bitMapData);
}

void drawFontString(u16 *gbkStr, int x, int y, u16 color)
{
    u8 strBuff[256];
    gbk_to_unicode((u8 *)gbkStr, (u8 *)strBuff, 256);
    int i = strlen_utf16(strBuff);
    // fixme s=1时取值与内存真实值对不上?
    for (int s = 1; s < i; s++) // 去掉utf16的ffeff头标识
    {
        u16 unicode23 = (strBuff[2 * s] << 8) | (strBuff[2 * s + 1]);
        drawFontChar(unicode23, x + (s - 1) * fontWidth, y, color);
    }
}

int mesureStringWidth(char *gbkStr)
{
    int len = strlen_gbk(gbkStr);
    return len * fontWidth;
}
