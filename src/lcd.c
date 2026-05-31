#include "lcd.h"

#define PIXEL565R(v) ((((u32)v >> 11) << 3) & 0xff) // 5位红色
#define PIXEL565G(v) ((((u32)v >> 5) << 2) & 0xff)  // 6位绿色
#define PIXEL565B(v) (((u32)v << 3) & 0xff)         // 5位蓝色

#define PIXEL888R(v) ((((u32)v >> 16)) & 0xff) // 5位红色
#define PIXEL888G(v) ((((u32)v >> 8)) & 0xff)  // 6位绿色
#define PIXEL888B(v) (v & 0xff)                // 5位蓝色

u8 *Lcd_Cache_Buffer;

void InitLcd()
{
    Lcd_Cache_Buffer = SDL_malloc(LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
    memset(Lcd_Cache_Buffer, 0, LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
}

void UpdateLcd()
{
    SDL_Surface *sfc = SDL_GetWindowSurface(window);
    if (!sfc)
        return;

    if (SDL_MUSTLOCK(sfc) && SDL_LockSurface(sfc) != 0)
        return;

    for (int i = 0; i < LCD_HEIGHT; i++)
    {
        u32 *dstRow = (u32 *)((u8 *)sfc->pixels + i * sfc->pitch);
        for (int j = 0; j < LCD_WIDTH; j++)
        {
            u32 offset = j + i * LCD_WIDTH;
            u16 color = ((u16 *)Lcd_Cache_Buffer)[offset];
            dstRow[j] = SDL_MapRGB(sfc->format, PIXEL565R(color), PIXEL565G(color), PIXEL565B(color));
        }
    }

    if (SDL_MUSTLOCK(sfc))
        SDL_UnlockSurface(sfc);
    SDL_UpdateWindowSurface(window);
}
