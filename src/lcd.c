#include "lcd.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL_syswm.h"
#endif

#define PIXEL565R(v) ((((u32)v >> 11) << 3) & 0xff)
#define PIXEL565G(v) ((((u32)v >> 5) << 2) & 0xff)
#define PIXEL565B(v) (((u32)v << 3) & 0xff)

#define PIXEL888R(v) ((((u32)v >> 16)) & 0xff)
#define PIXEL888G(v) ((((u32)v >> 8)) & 0xff)
#define PIXEL888B(v) (v & 0xff)

u8 *Lcd_Cache_Buffer;
#ifdef CBE_PLATFORM_ANDROID
int finalLayerBuffer[LCD_WIDTH * LCD_HEIGHT];
#endif

static vm_lcd_rotation g_lcdRotation = VM_LCD_ROTATE_0;
/* Game-frame upscale into the host window (2x by request). */
static int g_lcdScale = 2;
static int g_lcdDstX = 0;
static int g_lcdDstY = 0;
static int g_lcdDstW = 0;
static int g_lcdDstH = 0;
static int g_lcdOutW = 0;
static int g_lcdOutH = 0;

#define LCD_TOOLBAR_HEIGHT 32
#define LCD_TOOLBAR_BUTTON_X 4
#define LCD_TOOLBAR_BUTTON_Y 3
#define LCD_TOOLBAR_BUTTON_SIZE 26
#define LCD_SCALE_MIN 1
#define LCD_SCALE_MAX 8

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
    return (g_lcdRotation == VM_LCD_ROTATE_90_CW || g_lcdRotation == VM_LCD_ROTATE_90_CCW)
               ? LCD_HEIGHT
               : LCD_WIDTH;
}

int LcdGetViewHeight(void)
{
    return (g_lcdRotation == VM_LCD_ROTATE_90_CW || g_lcdRotation == VM_LCD_ROTATE_90_CCW)
               ? LCD_WIDTH
               : LCD_HEIGHT;
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
    return LcdGetViewWidth() * g_lcdScale;
}

int LcdGetWindowHeight(void)
{
    return LcdGetToolbarHeight() + LcdGetViewHeight() * g_lcdScale;
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
        y = LCD_HEIGHT - 1 - logicalX;
        break;
    case VM_LCD_ROTATE_180:
        x = LCD_WIDTH - 1 - logicalX;
        y = LCD_HEIGHT - 1 - viewY;
        break;
    case VM_LCD_ROTATE_90_CCW:
        x = LCD_WIDTH - 1 - viewY;
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
    int scale = g_lcdScale > 0 ? g_lcdScale : 1;

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

    rect->x = g_lcdDstX + rect->x * scale;
    rect->y = g_lcdDstY + rect->y * scale;
    rect->w *= scale;
    rect->h *= scale;
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

static void LcdDrawToolbar(SDL_Surface *sfc)
{
    int h = LcdGetToolbarHeight();
    int x = LCD_TOOLBAR_BUTTON_X;
    int y = LCD_TOOLBAR_BUTTON_Y;
    int size = LCD_TOOLBAR_BUTTON_SIZE;
    int indicatorX = x + size + 10;
    int indicatorY = y + 9;
    int segmentW = 7;

    LcdSurfaceFillRect(sfc, 0, 0, sfc->w, h, 0x202329);
    LcdSurfaceFillRect(sfc, 0, h - 1, sfc->w, 1, 0x4b515c);
    LcdSurfaceFillRect(sfc, x, y, size, size, 0x343a44);
    for (int i = 0; i < 4; ++i)
    {
        u32 color = ((int)LcdGetRotation() == i) ? 0xf2c94c : 0x676e79;
        LcdSurfaceFillRect(sfc, indicatorX + i * (segmentW + 3), indicatorY,
                           segmentW, 8, color);
    }
}

#ifdef _WIN32
/* Present the already-scaled SDL surface into the real HWND client with
 * StretchBlt.  Fixes the case where SDL's surface is logical-sized but the
 * Win32 client is larger (DPI), which otherwise leaves the game in the corner. */
static void LcdPresentStretchToClient(SDL_Surface *sfc)
{
    SDL_SysWMinfo wm;
    HWND hwnd;
    HDC hdcWnd;
    HDC hdcMem;
    HBITMAP hbmp;
    HBITMAP hold;
    BITMAPINFO bmi;
    void *bits = NULL;
    RECT rc;
    int clientW;
    int clientH;
    int y;
    u32 *src;
    u32 *dst;

    if (!sfc || sfc->format->BytesPerPixel != 4)
        return;

    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window, &wm) || wm.subsystem != SDL_SYSWM_WINDOWS)
        return;
    hwnd = wm.info.win.window;
    if (!hwnd || !GetClientRect(hwnd, &rc))
        return;

    clientW = (int)(rc.right - rc.left);
    clientH = (int)(rc.bottom - rc.top);
    if (clientW < 1 || clientH < 1)
        return;

    /* Same size: SDL_UpdateWindowSurface is enough. */
    if (clientW == sfc->w && clientH == sfc->h)
        return;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sfc->w;
    bmi.bmiHeader.biHeight = -sfc->h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hdcWnd = GetDC(hwnd);
    if (!hdcWnd)
        return;
    hdcMem = CreateCompatibleDC(hdcWnd);
    if (!hdcMem)
    {
        ReleaseDC(hwnd, hdcWnd);
        return;
    }
    hbmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!hbmp || !bits)
    {
        if (hbmp)
            DeleteObject(hbmp);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWnd);
        return;
    }

    hold = (HBITMAP)SelectObject(hdcMem, hbmp);
    for (y = 0; y < sfc->h; ++y)
    {
        src = (u32 *)((u8 *)sfc->pixels + y * sfc->pitch);
        dst = (u32 *)((u8 *)bits + y * sfc->w * 4);
        memcpy(dst, src, (size_t)sfc->w * 4);
    }

    SetStretchBltMode(hdcWnd, COLORONCOLOR);
    StretchBlt(hdcWnd, 0, 0, clientW, clientH, hdcMem, 0, 0, sfc->w, sfc->h, SRCCOPY);

    SelectObject(hdcMem, hold);
    DeleteObject(hbmp);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWnd);

    g_lcdOutW = clientW;
    g_lcdOutH = clientH;
}
#endif
#endif /* !CBE_PLATFORM_NO_WINDOW */

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
    int viewW;
    int viewH;
    int toolbarH;
    int wantW;
    int wantH;
    int preferScale;
    int drawScale;
    int fitX;
    int fitY;
    int offX;
    int offY;
    int availH;
    SDL_Surface *sfc;
    static int s_loggedPresent = 0;

    if (!window || !Lcd_Cache_Buffer)
        return;

    preferScale = g_lcdScale > 0 ? g_lcdScale : 2;
    viewW = LcdGetViewWidth();
    viewH = LcdGetViewHeight();
    toolbarH = LcdGetToolbarHeight();
    wantW = viewW * preferScale;
    wantH = toolbarH + viewH * preferScale;

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

    /* CRITICAL: scale to the *actual* surface size. On Win DPI the surface can
     * be 2x the requested window; painting only preferScale leaves the game in
     * the top-left corner of a larger black buffer. */
    availH = sfc->h - toolbarH;
    if (availH < 1)
        availH = 1;
    fitX = sfc->w / viewW;
    fitY = availH / viewH;
    drawScale = fitX < fitY ? fitX : fitY;
    if (drawScale < 1)
        drawScale = 1;
    offX = (sfc->w - viewW * drawScale) / 2;
    offY = toolbarH + (availH - viewH * drawScale) / 2;

    g_lcdDstX = offX;
    g_lcdDstY = offY;
    g_lcdDstW = viewW * drawScale;
    g_lcdDstH = viewH * drawScale;
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
    LcdDrawToolbar(sfc);

    for (int i = 0; i < LCD_HEIGHT; i++)
    {
        for (int j = 0; j < LCD_WIDTH; j++)
        {
            int srcX = j;
            int srcY = i;
            u16 color = ((u16 *)Lcd_Cache_Buffer)[j + i * LCD_WIDTH];
            u32 mapped = SDL_MapRGB(sfc->format, PIXEL565R(color), PIXEL565G(color),
                                    PIXEL565B(color));
            int baseX;
            int baseY;

            switch (g_lcdRotation)
            {
            case VM_LCD_ROTATE_90_CW:
                srcX = LCD_HEIGHT - 1 - i;
                srcY = j;
                break;
            case VM_LCD_ROTATE_180:
                srcX = LCD_WIDTH - 1 - j;
                srcY = LCD_HEIGHT - 1 - i;
                break;
            case VM_LCD_ROTATE_90_CCW:
                srcX = i;
                srcY = LCD_WIDTH - 1 - j;
                break;
            default:
                break;
            }

            if (srcX < 0 || srcY < 0 || srcX >= viewW || srcY >= viewH)
                continue;

            baseX = offX + srcX * drawScale;
            baseY = offY + srcY * drawScale;
            for (int sy = 0; sy < drawScale; ++sy)
            {
                int py = baseY + sy;
                u32 *dstRow;
                if (py < 0 || py >= sfc->h)
                    continue;
                dstRow = (u32 *)((u8 *)sfc->pixels + py * sfc->pitch);
                for (int sx = 0; sx < drawScale; ++sx)
                {
                    int px = baseX + sx;
                    if (px >= 0 && px < sfc->w)
                        dstRow[px] = mapped;
                }
            }
        }
    }

    if (SDL_MUSTLOCK(sfc))
        SDL_UnlockSurface(sfc);

    if (!s_loggedPresent)
    {
        printf("[info][lcd] game_frame_blit prefer=%d draw=%d view=%dx%d "
               "surface=%dx%d dst=(%d,%d %dx%d)\n",
               preferScale, drawScale, viewW, viewH, sfc->w, sfc->h, offX, offY,
               g_lcdDstW, g_lcdDstH);
        s_loggedPresent = 1;
    }

#ifdef _WIN32
    {
        SDL_SysWMinfo wm;
        RECT rc;
        int clientW = 0;
        int clientH = 0;
        SDL_VERSION(&wm.version);
        if (SDL_GetWindowWMInfo(window, &wm) && wm.subsystem == SDL_SYSWM_WINDOWS &&
            GetClientRect(wm.info.win.window, &rc))
        {
            clientW = (int)(rc.right - rc.left);
            clientH = (int)(rc.bottom - rc.top);
        }
        if (clientW > 0 && clientH > 0 &&
            (clientW != sfc->w || clientH != sfc->h))
        {
            LcdPresentStretchToClient(sfc);
        }
        else
        {
            SDL_UpdateWindowSurface(window);
        }
    }
#else
    SDL_UpdateWindowSurface(window);
#endif
#endif
}
