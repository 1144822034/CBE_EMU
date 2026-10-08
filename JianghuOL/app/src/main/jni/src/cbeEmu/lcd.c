#include "lcd.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL_syswm.h"
#endif

/* Match PixiJS_web rgb565ToRgba: full 0..255 range (not shift-only). */
#define PIXEL565R(v) ((u32)((((v) >> 11) & 0x1f) * 255 / 31))
#define PIXEL565G(v) ((u32)((((v) >> 5) & 0x3f) * 255 / 63))
#define PIXEL565B(v) ((u32)(((v) & 0x1f) * 255 / 31))

#define PIXEL888R(v) ((((u32)v >> 16)) & 0xff)
#define PIXEL888G(v) ((((u32)v >> 8)) & 0xff)
#define PIXEL888B(v) (v & 0xff)

u8 *Lcd_Cache_Buffer;
#ifdef CBE_PLATFORM_ANDROID
int finalLayerBuffer[LCD_WIDTH * LCD_HEIGHT];
#endif

static vm_lcd_rotation g_lcdRotation = VM_LCD_ROTATE_0;
/* Game-frame upscale into the host window (1 = native pixels). */
static int g_lcdScale = 1;
/* Active design resolution (may be smaller than framebuffer capacity).
 * 封神演义V2 paints a classic 176x220 frame inside the 240x400 buffer. */
static int g_lcdNativeW = LCD_WIDTH;
static int g_lcdNativeH = LCD_HEIGHT;
static int g_lcdDstX = 0;
static int g_lcdDstY = 0;
static int g_lcdDstW = 0;
static int g_lcdDstH = 0;
static int g_lcdOutW = 0;
static int g_lcdOutH = 0;
/* Present size = view * scale * dpi/96 * 5/4 (Pixi-like fractional nearest). */
static int g_lcdPresentW = 0;
static int g_lcdPresentTbH = 0;

#define LCD_TOOLBAR_HEIGHT 32
#define LCD_TOOLBAR_BUTTON_X 4
#define LCD_TOOLBAR_BUTTON_Y 3
#define LCD_TOOLBAR_BUTTON_SIZE 26
#define LCD_SCALE_MIN 1
#define LCD_SCALE_MAX 8

static int LcdWinDpi(void)
{
#ifdef _WIN32
    if (window)
    {
        SDL_SysWMinfo wm;
        SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(window, &wm) && wm.subsystem == SDL_SYSWM_WINDOWS)
        {
            HWND hwnd = wm.info.win.window;
            HMODULE user32 = GetModuleHandleA("user32.dll");
            typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
            GetDpiForWindowFn getDpi =
                user32 ? (GetDpiForWindowFn)GetProcAddress(user32, "GetDpiForWindow")
                       : NULL;
            if (getDpi)
            {
                UINT dpi = getDpi(hwnd);
                if (dpi >= 72 && dpi <= 384)
                    return (int)dpi;
            }
            {
                HDC hdc = GetDC(hwnd);
                if (hdc)
                {
                    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
                    ReleaseDC(hwnd, hdc);
                    if (dpi >= 72 && dpi <= 384)
                        return dpi;
                }
            }
        }
    }
#endif
    return 96;
}

/* PixiJS: resolution + CSS pixelated often yields a non-integer scale, which
 * breaks ordered-dither moiré on outdoor sand (m_mount). Pure 2×/3× block
 * replicate keeps that moiré — match Pixi with *5/4 present size. */
static void LcdComputePresentSize(int viewW, int viewH, int *outW, int *outTbH,
                                  int *outGameH)
{
    int dpi = LcdWinDpi();
    int scale = g_lcdScale > 0 ? g_lcdScale : 2;
    /* want = view * scale * (dpi/96) * (5/4) */
    int w = (viewW * scale * dpi * 5) / (96 * 4);
    int gameH = (viewH * scale * dpi * 5) / (96 * 4);
    int tbH = (LCD_TOOLBAR_HEIGHT * scale * dpi * 5) / (96 * 4);
    if (w < viewW)
        w = viewW;
    if (gameH < viewH)
        gameH = viewH;
    if (tbH < 16)
        tbH = 16;
    if (outW)
        *outW = w;
    if (outTbH)
        *outTbH = tbH;
    if (outGameH)
        *outGameH = gameH;
}

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

int LcdGetNativeWidth(void)
{
    return g_lcdNativeW;
}

int LcdGetNativeHeight(void)
{
    return g_lcdNativeH;
}

void LcdSetNativeSize(int width, int height)
{
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    if (width > LCD_WIDTH)
        width = LCD_WIDTH;
    if (height > LCD_HEIGHT)
        height = LCD_HEIGHT;
    if (g_lcdNativeW == width && g_lcdNativeH == height)
        return;
    g_lcdNativeW = width;
    g_lcdNativeH = height;
    LcdApplyWindowSize();
}

int LcdGetScreenPitchPixels(void)
{
    /* Match host DrawImage / DF image pitch: pad width up to 4 pixels. */
    return ((4 - g_lcdNativeW) & 3) + g_lcdNativeW;
}

int LcdGetViewWidth(void)
{
    return (g_lcdRotation == VM_LCD_ROTATE_90_CW || g_lcdRotation == VM_LCD_ROTATE_90_CCW)
               ? g_lcdNativeH
               : g_lcdNativeW;
}

int LcdGetViewHeight(void)
{
    return (g_lcdRotation == VM_LCD_ROTATE_90_CW || g_lcdRotation == VM_LCD_ROTATE_90_CCW)
               ? g_lcdNativeW
               : g_lcdNativeH;
}

int LcdGetScale(void)
{
    return g_lcdScale;
}

void LcdSetScale(int scale)
{
    if (scale < LCD_SCALE_MIN)
        scale = LCD_SCALE_MIN;
    if (scale > LCD_SCALE_MAX)
        scale = LCD_SCALE_MAX;
    if (g_lcdScale == scale)
        return;
    g_lcdScale = scale;
    LcdApplyWindowSize();
}

int LcdGetWindowWidth(void)
{
    int w = 0;
    int tb = 0;
    int gh = 0;
    LcdComputePresentSize(LcdGetViewWidth(), LcdGetViewHeight(), &w, &tb, &gh);
    (void)tb;
    (void)gh;
    return w;
}

int LcdGetWindowHeight(void)
{
    int w = 0;
    int tb = 0;
    int gh = 0;
    LcdComputePresentSize(LcdGetViewWidth(), LcdGetViewHeight(), &w, &tb, &gh);
    (void)w;
    return tb + gh;
}

int LcdGetToolbarHeight(void)
{
#ifdef CBE_PLATFORM_NO_WINDOW
    return 0;
#else
    /* Present-space toolbar height (scaled). Falls back to logical. */
    if (g_lcdPresentTbH > 0)
        return g_lcdPresentTbH;
    {
        int w = 0;
        int tb = 0;
        int gh = 0;
        LcdComputePresentSize(LcdGetViewWidth(), LcdGetViewHeight(), &w, &tb, &gh);
        (void)w;
        (void)gh;
        return tb;
    }
#endif
}

void LcdApplyWindowSize(void)
{
#ifndef CBE_PLATFORM_NO_WINDOW
    if (!window)
        return;

    int wantW = LcdGetWindowWidth();
    int wantH = LcdGetWindowHeight();
    int winW = 0;
    int winH = 0;
    Uint32 flags = SDL_GetWindowFlags(window);

    if (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN |
                 SDL_WINDOW_FULLSCREEN_DESKTOP))
        SDL_RestoreWindow(window);

    SDL_GetWindowSize(window, &winW, &winH);
    if (winW != wantW || winH != wantH)
        SDL_SetWindowSize(window, wantW, wantH);
#endif
}

void LcdReleasePresentBackend(void)
{
    /* Surface present path keeps no persistent GL/D3D backend. */
}

void LcdWindowPointToVm(int windowX, int windowY, int *vmX, int *vmY)
{
    int viewW = LcdGetViewWidth();
    int viewH = LcdGetViewHeight();
    int nativeW = g_lcdNativeW;
    int nativeH = g_lcdNativeH;
    int logicalX;
    int viewY;
    int x;
    int y;

    if (g_lcdDstW > 0 && g_lcdDstH > 0)
    {
        logicalX = (windowX - g_lcdDstX) * viewW / g_lcdDstW;
        viewY = (windowY - g_lcdDstY) * viewH / g_lcdDstH;
    }
    else
    {
        int scale = g_lcdScale > 0 ? g_lcdScale : 1;
        logicalX = (windowX - g_lcdDstX) / scale;
        viewY = (windowY - g_lcdDstY) / scale;
    }

    x = logicalX;
    y = viewY;
    switch (g_lcdRotation)
    {
    case VM_LCD_ROTATE_90_CW:
        x = viewY;
        y = nativeH - 1 - logicalX;
        break;
    case VM_LCD_ROTATE_180:
        x = nativeW - 1 - logicalX;
        y = nativeH - 1 - viewY;
        break;
    case VM_LCD_ROTATE_90_CCW:
        x = nativeW - 1 - viewY;
        y = logicalX;
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
    int tbH = LcdGetToolbarHeight();
    int scaleNum;
    int bx;
    int by;
    int bs;

    if (!LcdWindowPointInToolbar(windowX, windowY))
        return 0;

    /* Map present coords back to logical toolbar space. */
    scaleNum = tbH > 0 ? tbH : LCD_TOOLBAR_HEIGHT;
    bx = windowX * LCD_TOOLBAR_HEIGHT / scaleNum;
    by = windowY * LCD_TOOLBAR_HEIGHT / scaleNum;
    bs = LCD_TOOLBAR_BUTTON_SIZE;
    if (bx >= LCD_TOOLBAR_BUTTON_X && bx < LCD_TOOLBAR_BUTTON_X + bs &&
        by >= LCD_TOOLBAR_BUTTON_Y && by < LCD_TOOLBAR_BUTTON_Y + bs)
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
    int nativeW = g_lcdNativeW;
    int nativeH = g_lcdNativeH;
    int viewW = LcdGetViewWidth();
    int viewH = LcdGetViewHeight();
    int dstW = g_lcdDstW > 0 ? g_lcdDstW : viewW;
    int dstH = g_lcdDstH > 0 ? g_lcdDstH : viewH;

    if (!rect)
        return;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > nativeW)
        x1 = nativeW;
    if (y1 > nativeH)
        y1 = nativeH;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;

    switch (g_lcdRotation)
    {
    case VM_LCD_ROTATE_90_CW:
        rect->x = nativeH - y1;
        rect->y = x0;
        rect->w = y1 - y0;
        rect->h = x1 - x0;
        break;
    case VM_LCD_ROTATE_180:
        rect->x = nativeW - x1;
        rect->y = nativeH - y1;
        rect->w = x1 - x0;
        rect->h = y1 - y0;
        break;
    case VM_LCD_ROTATE_90_CCW:
        rect->x = y0;
        rect->y = nativeW - x1;
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

    rect->x = g_lcdDstX + (rect->x * dstW) / (viewW > 0 ? viewW : 1);
    rect->y = g_lcdDstY + (rect->y * dstH) / (viewH > 0 ? viewH : 1);
    rect->w = (rect->w * dstW) / (viewW > 0 ? viewW : 1);
    rect->h = (rect->h * dstH) / (viewH > 0 ? viewH : 1);
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

#endif /* !CBE_PLATFORM_NO_WINDOW */

void UpdateLcd()
{
#ifdef CBE_PLATFORM_ANDROID
    if (Lcd_Cache_Buffer == NULL)
        return;
    {
        int pitch = LcdGetScreenPitchPixels();
        for (int i = 0; i < g_lcdNativeH; ++i)
        {
            for (int j = 0; j < g_lcdNativeW; ++j)
            {
                u32 offset = (u32)j + (u32)i * (u32)pitch;
                u16 color = ((u16 *)Lcd_Cache_Buffer)[offset];
                finalLayerBuffer[j + i * LCD_WIDTH] = (int)(0xff000000u |
                                                             ((u32)PIXEL565R(color) << 16) |
                                                             ((u32)PIXEL565G(color) << 8) |
                                                             (u32)PIXEL565B(color));
            }
        }
    }
#elif defined(CBE_PLATFORM_HEADLESS)
    return;
#else
    /* PixiJS-like present: nearest sample each output pixel from the 240×400
     * LCD (css pixelated). Avoids integer-block moiré on m_mount dither. */
    int viewW;
    int viewH;
    int wantW;
    int wantTbH;
    int wantGameH;
    int wantH;
    int pitch;
    int dx;
    int dy;
    SDL_Surface *sfc;
    static int s_loggedW = -1;
    static int s_loggedH = -1;
    static int s_loggedDpi = -1;

    if (!window || !Lcd_Cache_Buffer)
        return;

    viewW = LcdGetViewWidth();
    viewH = LcdGetViewHeight();
    LcdComputePresentSize(viewW, viewH, &wantW, &wantTbH, &wantGameH);
    wantH = wantTbH + wantGameH;
    g_lcdPresentW = wantW;
    g_lcdPresentTbH = wantTbH;

    LcdApplyWindowSize();
    sfc = SDL_GetWindowSurface(window);
    if (!sfc)
        return;
    if (sfc->w != wantW || sfc->h != wantH)
    {
        SDL_SetWindowSize(window, wantW, wantH);
        sfc = SDL_GetWindowSurface(window);
        if (!sfc)
            return;
    }

    /* If DPI/WM still resized us, fill the real surface with nearest. */
    if (sfc->w > 0 && sfc->h > wantTbH)
    {
        wantW = sfc->w;
        wantTbH = (LCD_TOOLBAR_HEIGHT * sfc->h) / (LCD_TOOLBAR_HEIGHT + viewH);
        if (wantTbH < 16)
            wantTbH = 16;
        if (wantTbH > sfc->h / 2)
            wantTbH = sfc->h / 4;
        wantGameH = sfc->h - wantTbH;
        g_lcdPresentW = wantW;
        g_lcdPresentTbH = wantTbH;
    }

    g_lcdDstX = 0;
    g_lcdDstY = wantTbH;
    g_lcdDstW = wantW;
    g_lcdDstH = wantGameH;
    g_lcdOutW = sfc->w;
    g_lcdOutH = sfc->h;

    if (SDL_MUSTLOCK(sfc) && SDL_LockSurface(sfc) != 0)
        return;
    if (sfc->format->BytesPerPixel != 4)
    {
        if (SDL_MUSTLOCK(sfc))
            SDL_UnlockSurface(sfc);
        printf("[error][lcd] unsupported surface bpp=%u\n",
               (unsigned)sfc->format->BitsPerPixel);
        return;
    }

    SDL_FillRect(sfc, NULL, SDL_MapRGB(sfc->format, 0, 0, 0));
    /* Scaled toolbar strip (logical 32px → wantTbH). */
    LcdSurfaceFillRect(sfc, 0, 0, wantW, wantTbH, 0x202329);
    LcdSurfaceFillRect(sfc, 0, wantTbH - 1, wantW, 1, 0x4b515c);
    {
        int bx = LCD_TOOLBAR_BUTTON_X * wantTbH / LCD_TOOLBAR_HEIGHT;
        int by = LCD_TOOLBAR_BUTTON_Y * wantTbH / LCD_TOOLBAR_HEIGHT;
        int bs = LCD_TOOLBAR_BUTTON_SIZE * wantTbH / LCD_TOOLBAR_HEIGHT;
        int indicatorX;
        int indicatorY;
        int segmentW;
        if (bs < 8)
            bs = 8;
        LcdSurfaceFillRect(sfc, bx, by, bs, bs, 0x343a44);
        indicatorX = bx + bs + 10 * wantTbH / LCD_TOOLBAR_HEIGHT;
        indicatorY = by + 9 * wantTbH / LCD_TOOLBAR_HEIGHT;
        segmentW = 7 * wantTbH / LCD_TOOLBAR_HEIGHT;
        if (segmentW < 2)
            segmentW = 2;
        for (int i = 0; i < 4; ++i)
        {
            u32 color = ((int)LcdGetRotation() == i) ? 0xf2c94c : 0x676e79;
            LcdSurfaceFillRect(sfc, indicatorX + i * (segmentW + 3), indicatorY,
                               segmentW, 8 * wantTbH / LCD_TOOLBAR_HEIGHT, color);
        }
    }

    pitch = LcdGetScreenPitchPixels();
    for (dy = 0; dy < wantGameH; ++dy)
    {
        int viewY = dy * viewH / wantGameH;
        u32 *dstRow =
            (u32 *)((u8 *)sfc->pixels + (dy + wantTbH) * sfc->pitch);
        if (viewY < 0)
            viewY = 0;
        if (viewY >= viewH)
            viewY = viewH - 1;
        for (dx = 0; dx < wantW; ++dx)
        {
            int viewX = dx * viewW / wantW;
            int sampleX;
            int sampleY;
            u16 color;
            if (viewX < 0)
                viewX = 0;
            if (viewX >= viewW)
                viewX = viewW - 1;
            /* Inverse of UpdateLcd rotation used when blitting native→view. */
            switch (g_lcdRotation)
            {
            case VM_LCD_ROTATE_90_CW:
                sampleX = viewY;
                sampleY = g_lcdNativeH - 1 - viewX;
                break;
            case VM_LCD_ROTATE_180:
                sampleX = g_lcdNativeW - 1 - viewX;
                sampleY = g_lcdNativeH - 1 - viewY;
                break;
            case VM_LCD_ROTATE_90_CCW:
                sampleX = g_lcdNativeW - 1 - viewY;
                sampleY = viewX;
                break;
            default:
                sampleX = viewX;
                sampleY = viewY;
                break;
            }
            if (sampleX < 0 || sampleY < 0 || sampleX >= g_lcdNativeW ||
                sampleY >= g_lcdNativeH)
                continue;
            color = ((u16 *)Lcd_Cache_Buffer)[sampleX + sampleY * pitch];
            dstRow[dx] = SDL_MapRGB(sfc->format, PIXEL565R(color), PIXEL565G(color),
                                    PIXEL565B(color));
        }
    }

    if (SDL_MUSTLOCK(sfc))
        SDL_UnlockSurface(sfc);

    if (s_loggedW != wantW || s_loggedH != wantGameH || s_loggedDpi != LcdWinDpi())
    {
        printf("[info][lcd] pixi_nearest_present scale=%d dpi=%d view=%dx%d "
               "out=%dx%d tb=%d surface=%dx%d\n",
               g_lcdScale, LcdWinDpi(), viewW, viewH, wantW, wantGameH, wantTbH,
               sfc->w, sfc->h);
        s_loggedW = wantW;
        s_loggedH = wantGameH;
        s_loggedDpi = LcdWinDpi();
    }

    SDL_UpdateWindowSurface(window);
#endif
}
