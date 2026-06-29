#include "lcd.h"
#include <string.h>

#define PIXEL565R(v) ((((u32)v >> 11) << 3) & 0xff) // 5位红色
#define PIXEL565G(v) ((((u32)v >> 5) << 2) & 0xff)  // 6位绿色
#define PIXEL565B(v) (((u32)v << 3) & 0xff)         // 5位蓝色

#define PIXEL888R(v) ((((u32)v >> 16)) & 0xff) // 5位红色
#define PIXEL888G(v) ((((u32)v >> 8)) & 0xff)  // 6位绿色
#define PIXEL888B(v) (v & 0xff)                // 5位蓝色

u8 *Lcd_Cache_Buffer;
static u32 *Lcd_Color_Table;
static Uint32 Lcd_Color_Table_Format;

static void BuildLcdColorTable(SDL_PixelFormat *format)
{
    if (format == NULL)
        return;
    if (Lcd_Color_Table == NULL)
        Lcd_Color_Table = (u32 *)SDL_malloc(65536 * sizeof(u32));
    if (Lcd_Color_Table == NULL)
        return;
    for (u32 color = 0; color <= 0xffff; ++color)
        Lcd_Color_Table[color] = SDL_MapRGB(format, PIXEL565R(color), PIXEL565G(color), PIXEL565B(color));
    Lcd_Color_Table_Format = format->format;
}

void InitLcd()
{
    Lcd_Cache_Buffer = SDL_malloc(LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
    memset(Lcd_Cache_Buffer, 0, LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
    Lcd_Color_Table = NULL;
    Lcd_Color_Table_Format = 0;
}

void UpdateLcd()
{
    SDL_Surface *sfc = SDL_GetWindowSurface(window);
    if (!sfc)
        return;

    if (SDL_MUSTLOCK(sfc) && SDL_LockSurface(sfc) != 0)
        return;

    if (sfc->format->BytesPerPixel == 2 && sfc->format->Rmask == 0xf800 &&
        sfc->format->Gmask == 0x07e0 && sfc->format->Bmask == 0x001f)
    {
        for (int i = 0; i < LCD_HEIGHT; i++)
            memcpy((u8 *)sfc->pixels + i * sfc->pitch,
                   Lcd_Cache_Buffer + i * LCD_WIDTH * sizeof(u16),
                   LCD_WIDTH * sizeof(u16));
    }
    else
    {
        if (Lcd_Color_Table == NULL || Lcd_Color_Table_Format != sfc->format->format)
            BuildLcdColorTable(sfc->format);

        if (sfc->format->BytesPerPixel == 4 && Lcd_Color_Table != NULL)
        {
            for (int i = 0; i < LCD_HEIGHT; i++)
            {
                const u16 *srcRow = (const u16 *)(Lcd_Cache_Buffer + i * LCD_WIDTH * sizeof(u16));
                u32 *dstRow = (u32 *)((u8 *)sfc->pixels + i * sfc->pitch);
                for (int j = 0; j < LCD_WIDTH; j++)
                    dstRow[j] = Lcd_Color_Table[srcRow[j]];
            }
        }
        else
        {
            for (int i = 0; i < LCD_HEIGHT; i++)
            {
                const u16 *srcRow = (const u16 *)(Lcd_Cache_Buffer + i * LCD_WIDTH * sizeof(u16));
                u8 *dstRow = (u8 *)sfc->pixels + i * sfc->pitch;
                for (int j = 0; j < LCD_WIDTH; j++)
                {
                    u16 color = srcRow[j];
                    u32 mapped = Lcd_Color_Table ? Lcd_Color_Table[color] :
                                 SDL_MapRGB(sfc->format, PIXEL565R(color), PIXEL565G(color), PIXEL565B(color));
                    memcpy(dstRow + j * sfc->format->BytesPerPixel, &mapped, sfc->format->BytesPerPixel);
                }
            }
        }
    }

    if (SDL_MUSTLOCK(sfc))
        SDL_UnlockSurface(sfc);
    SDL_UpdateWindowSurface(window);
}
