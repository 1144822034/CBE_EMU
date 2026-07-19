#include "lcd.h"
#include <string.h>

#define PIXEL565R(v) ((((u32)v >> 11) << 3) & 0xff) // 5位红色
#define PIXEL565G(v) ((((u32)v >> 5) << 2) & 0xff)  // 6位绿色
#define PIXEL565B(v) (((u32)v << 3) & 0xff)         // 5位蓝色

#define PIXEL888R(v) ((((u32)v >> 16)) & 0xff) // 5位红色
#define PIXEL888G(v) ((((u32)v >> 8)) & 0xff)  // 6位绿色
#define PIXEL888B(v) (v & 0xff)                // 5位蓝色

u8 *Lcd_Cache_Buffer;
#ifdef CBE_PLATFORM_ANDROID
int finalLayerBuffer[LCD_WIDTH * LCD_HEIGHT];
#endif
static vm_lcd_rotation g_lcdRotation = VM_LCD_ROTATE_0;

#define LCD_TOOLBAR_HEIGHT 32
#define LCD_TOOLBAR_BUTTON_X 4
#define LCD_TOOLBAR_BUTTON_Y 3
#define LCD_TOOLBAR_BUTTON_SIZE 26

static vm_lcd_rotation LcdNormalizeRotation(vm_lcd_rotation rotation)
{
    switch (rotation)
    {
    case VM_LCD_ROTATE_90_CW:
    case VM_LCD_ROTATE_180:
    case VM_LCD_ROTATE_90_CCW:
        return rotation;
    default:
        return VM_LCD_ROTATE_0;
    }
}

void LcdSetRotation(vm_lcd_rotation rotation)
{
    g_lcdRotation = LcdNormalizeRotation(rotation);
    LcdApplyWindowSize();
}

vm_lcd_rotation LcdGetRotation(void)
{
    return g_lcdRotation;
}

void LcdCycleRotation(void)
{
    LcdSetRotation((vm_lcd_rotation)(((int)g_lcdRotation + 1) & 3));
}

const char *LcdRotationName(vm_lcd_rotation rotation)
{
    switch (LcdNormalizeRotation(rotation))
    {
    case VM_LCD_ROTATE_90_CW:
        return "right";
    case VM_LCD_ROTATE_180:
        return "180";
    case VM_LCD_ROTATE_90_CCW:
        return "left";
    default:
        return "0";
    }
}

int LcdGetViewWidth(void)
{
    return (g_lcdRotation == VM_LCD_ROTATE_90_CW || g_lcdRotation == VM_LCD_ROTATE_90_CCW) ? LCD_HEIGHT : LCD_WIDTH;
}

int LcdGetViewHeight(void)
{
    return (g_lcdRotation == VM_LCD_ROTATE_90_CW || g_lcdRotation == VM_LCD_ROTATE_90_CCW) ? LCD_WIDTH : LCD_HEIGHT;
}

int LcdGetWindowWidth(void)
{
    return LcdGetViewWidth();
}

int LcdGetWindowHeight(void)
{
    return LcdGetToolbarHeight() + LcdGetViewHeight();
}

int LcdGetToolbarHeight(void)
{
#ifdef CBE_PLATFORM_NO_WINDOW
    return 0;
#else
    return LCD_TOOLBAR_HEIGHT;
#endif
}

void LcdApplyWindowSize(void)
{
    if (!window)
        return;

    int viewW = LcdGetWindowWidth();
    int viewH = LcdGetWindowHeight();
    int winW = 0;
    int winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    if (winW != viewW || winH != viewH)
        SDL_SetWindowSize(window, viewW, viewH);
}

void LcdWindowPointToVm(int windowX, int windowY, int *vmX, int *vmY)
{
    int x = windowX;
    int viewY = windowY - LcdGetToolbarHeight();
    int y = viewY;

    switch (g_lcdRotation)
    {
    case VM_LCD_ROTATE_90_CW:
        x = viewY;
        y = LCD_HEIGHT - 1 - windowX;
        break;
    case VM_LCD_ROTATE_180:
        x = LCD_WIDTH - 1 - windowX;
        y = LCD_HEIGHT - 1 - viewY;
        break;
    case VM_LCD_ROTATE_90_CCW:
        x = LCD_WIDTH - 1 - viewY;
        y = windowX;
        break;
    default:
        break;
    }

    if (vmX)
        *vmX = x;
    if (vmY)
        *vmY = y;
}

int LcdWindowPointInToolbar(int windowX, int windowY)
{
    (void)windowX;
    return windowY >= 0 && windowY < LcdGetToolbarHeight();
}

int LcdHandleToolbarMouseDown(int windowX, int windowY)
{
    if (!LcdWindowPointInToolbar(windowX, windowY))
        return 0;

    if (windowX >= LCD_TOOLBAR_BUTTON_X &&
        windowX < LCD_TOOLBAR_BUTTON_X + LCD_TOOLBAR_BUTTON_SIZE &&
        windowY >= LCD_TOOLBAR_BUTTON_Y &&
        windowY < LCD_TOOLBAR_BUTTON_Y + LCD_TOOLBAR_BUTTON_SIZE)
    {
        LcdCycleRotation();
        return 2;
    }
    return 1;
}

void LcdVmRectToWindowRect(int x, int y, int w, int h, SDL_Rect *rect)
{
    int x0 = x;
    int y0 = y;
    int x1 = x + w;
    int y1 = y + h;

    if (!rect)
        return;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > LCD_WIDTH)
        x1 = LCD_WIDTH;
    if (y1 > LCD_HEIGHT)
        y1 = LCD_HEIGHT;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;

    switch (g_lcdRotation)
    {
    case VM_LCD_ROTATE_90_CW:
        rect->x = LCD_HEIGHT - y1;
        rect->y = x0;
        rect->w = y1 - y0;
        rect->h = x1 - x0;
        break;
    case VM_LCD_ROTATE_180:
        rect->x = LCD_WIDTH - x1;
        rect->y = LCD_HEIGHT - y1;
        rect->w = x1 - x0;
        rect->h = y1 - y0;
        break;
    case VM_LCD_ROTATE_90_CCW:
        rect->x = y0;
        rect->y = LCD_WIDTH - x1;
        rect->w = y1 - y0;
        rect->h = x1 - x0;
        break;
    default:
        rect->x = x0;
        rect->y = y0;
        rect->w = x1 - x0;
        rect->h = y1 - y0;
        break;
    }
}

void InitLcd()
{
    Lcd_Cache_Buffer = SDL_malloc(LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
    memset(Lcd_Cache_Buffer, 0, LCD_WIDTH * LCD_HEIGHT * PIXEL_PER_BYTE);
#ifdef CBE_PLATFORM_ANDROID
    memset(finalLayerBuffer, 0, sizeof(finalLayerBuffer));
#endif
}

#ifndef CBE_PLATFORM_NO_WINDOW
static void LcdSurfacePutPixel(SDL_Surface *sfc, int x, int y, u32 color)
{
    if (!sfc || x < 0 || y < 0 || x >= sfc->w || y >= sfc->h)
        return;
    u32 *dstRow = (u32 *)((u8 *)sfc->pixels + y * sfc->pitch);
    dstRow[x] = SDL_MapRGB(sfc->format, PIXEL888R(color), PIXEL888G(color), PIXEL888B(color));
}

static void LcdSurfaceFillRect(SDL_Surface *sfc, int x, int y, int w, int h, u32 color)
{
    if (!sfc || w <= 0 || h <= 0)
        return;
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w > sfc->w)
        w = sfc->w - x;
    if (y + h > sfc->h)
        h = sfc->h - y;
    if (w <= 0 || h <= 0)
        return;

    u32 mapped = SDL_MapRGB(sfc->format, PIXEL888R(color), PIXEL888G(color), PIXEL888B(color));
    for (int row = 0; row < h; ++row)
    {
        u32 *dstRow = (u32 *)((u8 *)sfc->pixels + (y + row) * sfc->pitch);
        for (int col = 0; col < w; ++col)
            dstRow[x + col] = mapped;
    }
}

static void LcdSurfaceDrawLine(SDL_Surface *sfc, int x0, int y0, int x1, int y1, u32 color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1)
    {
        LcdSurfacePutPixel(sfc, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void LcdSurfaceDrawRect(SDL_Surface *sfc, int x, int y, int w, int h, u32 color)
{
    LcdSurfaceDrawLine(sfc, x, y, x + w - 1, y, color);
    LcdSurfaceDrawLine(sfc, x, y + h - 1, x + w - 1, y + h - 1, color);
    LcdSurfaceDrawLine(sfc, x, y, x, y + h - 1, color);
    LcdSurfaceDrawLine(sfc, x + w - 1, y, x + w - 1, y + h - 1, color);
}

static void LcdDrawRotateGlyph(SDL_Surface *sfc, int x, int y, u32 color)
{
    LcdSurfaceDrawLine(sfc, x + 8, y + 8, x + 17, y + 8, color);
    LcdSurfaceDrawLine(sfc, x + 17, y + 8, x + 17, y + 15, color);
    LcdSurfaceDrawLine(sfc, x + 17, y + 15, x + 11, y + 15, color);
    LcdSurfaceDrawLine(sfc, x + 11, y + 15, x + 11, y + 20, color);
    LcdSurfaceDrawLine(sfc, x + 11, y + 20, x + 20, y + 20, color);
    LcdSurfaceDrawLine(sfc, x + 20, y + 20, x + 20, y + 13, color);
    LcdSurfaceDrawLine(sfc, x + 17, y + 8, x + 13, y + 4, color);
    LcdSurfaceDrawLine(sfc, x + 17, y + 8, x + 13, y + 12, color);
    LcdSurfaceDrawLine(sfc, x + 11, y + 20, x + 15, y + 16, color);
    LcdSurfaceDrawLine(sfc, x + 11, y + 20, x + 15, y + 24, color);
}

static void LcdDrawToolbar(SDL_Surface *sfc)
{
    int h = LcdGetToolbarHeight();
    LcdSurfaceFillRect(sfc, 0, 0, sfc->w, h, 0x202329);
    LcdSurfaceFillRect(sfc, 0, h - 1, sfc->w, 1, 0x4b515c);

    int x = LCD_TOOLBAR_BUTTON_X;
    int y = LCD_TOOLBAR_BUTTON_Y;
    int size = LCD_TOOLBAR_BUTTON_SIZE;
    LcdSurfaceFillRect(sfc, x, y, size, size, 0x343a44);
    LcdSurfaceDrawRect(sfc, x, y, size, size, 0x707987);
    LcdSurfaceDrawRect(sfc, x + 1, y + 1, size - 2, size - 2, 0x171a1f);
    LcdDrawRotateGlyph(sfc, x, y, 0xf4f7fb);

    int indicatorX = x + size + 10;
    int indicatorY = y + 9;
    int indicatorW = 38;
    int indicatorH = 8;
    int segmentW = 7;
    for (int i = 0; i < 4; ++i)
    {
        u32 color = ((int)LcdGetRotation() == i) ? 0xf2c94c : 0x676e79;
        LcdSurfaceFillRect(sfc, indicatorX + i * (segmentW + 3), indicatorY,
                           segmentW, indicatorH, color);
    }
    LcdSurfaceDrawRect(sfc, indicatorX - 2, indicatorY - 2,
                       indicatorW, indicatorH + 4, 0x4b515c);
}
#endif

void UpdateLcd()
{
#ifdef CBE_PLATFORM_ANDROID
    if (Lcd_Cache_Buffer == NULL)
        return;
    for (int i = 0; i < LCD_HEIGHT; ++i)
    {
        for (int j = 0; j < LCD_WIDTH; ++j)
        {
            u32 offset = (u32)j + (u32)i * LCD_WIDTH;
            u16 color = ((u16 *)Lcd_Cache_Buffer)[offset];
            finalLayerBuffer[offset] = (int)(0xff000000u |
                                             ((u32)PIXEL565R(color) << 16) |
                                             ((u32)PIXEL565G(color) << 8) |
                                             (u32)PIXEL565B(color));
        }
    }
#elif defined(CBE_PLATFORM_HEADLESS)
    return;
#else
    LcdApplyWindowSize();

    SDL_Surface *sfc = SDL_GetWindowSurface(window);
    if (!sfc)
        return;

    int viewW = LcdGetViewWidth();
    int viewH = LcdGetViewHeight();
    int toolbarH = LcdGetToolbarHeight();
    int windowW = LcdGetWindowWidth();
    int windowH = LcdGetWindowHeight();
    if (sfc->w != windowW || sfc->h != windowH)
    {
        SDL_SetWindowSize(window, windowW, windowH);
        sfc = SDL_GetWindowSurface(window);
        if (!sfc)
            return;
    }

    if (SDL_MUSTLOCK(sfc) && SDL_LockSurface(sfc) != 0)
        return;

    LcdDrawToolbar(sfc);

    for (int i = 0; i < LCD_HEIGHT; i++)
    {
        for (int j = 0; j < LCD_WIDTH; j++)
        {
            int dstX = j;
            int dstY = i;
            u32 offset = j + i * LCD_WIDTH;
            u16 color = ((u16 *)Lcd_Cache_Buffer)[offset];

            switch (g_lcdRotation)
            {
            case VM_LCD_ROTATE_90_CW:
                dstX = LCD_HEIGHT - 1 - i;
                dstY = j;
                break;
            case VM_LCD_ROTATE_180:
                dstX = LCD_WIDTH - 1 - j;
                dstY = LCD_HEIGHT - 1 - i;
                break;
            case VM_LCD_ROTATE_90_CCW:
                dstX = i;
                dstY = LCD_WIDTH - 1 - j;
                break;
            default:
                break;
            }

            if (dstX >= 0 && dstX < sfc->w && dstY >= 0 && dstY < sfc->h)
            {
                u32 *dstRow = (u32 *)((u8 *)sfc->pixels + (dstY + toolbarH) * sfc->pitch);
                dstRow[dstX] = SDL_MapRGB(sfc->format, PIXEL565R(color), PIXEL565G(color), PIXEL565B(color));
            }
        }
    }

    if (SDL_MUSTLOCK(sfc))
        SDL_UnlockSurface(sfc);
    SDL_UpdateWindowSurface(window);
#endif
}
