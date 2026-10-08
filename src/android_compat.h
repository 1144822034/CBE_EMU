#pragma once

/* Small SDL-compatible surface used by no-window builds. */
#ifdef CBE_PLATFORM_ANDROID
#include <android/log.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifdef CBE_PLATFORM_ANDROID
#define CBE_LOG_TAG "CBE_EMU"
#endif

typedef int SDL_Keycode;
typedef struct SDL_Window SDL_Window;

typedef struct SDL_Rect
{
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;

typedef struct SDL_PixelFormat
{
    uint32_t format;
} SDL_PixelFormat;

typedef struct SDL_Surface
{
    int w;
    int h;
    int pitch;
    void *pixels;
    SDL_PixelFormat *format;
    SDL_Rect clip_rect;
} SDL_Surface;

typedef struct SDL_Event
{
    int type;
    struct { struct { SDL_Keycode sym; } keysym; } key;
    struct { int x; int y; } motion;
    struct { int x; int y; int button; } button;
    struct { int y; } wheel;
    struct { char text[32]; } text;
    struct { char text[32]; } edit;
} SDL_Event;

#define SDL_QUIT 0x100
#define SDL_KEYDOWN 0x300
#define SDL_KEYUP 0x301
#define SDL_TEXTEDITING 0x302
#define SDL_TEXTINPUT 0x303
#define SDL_MOUSEMOTION 0x400
#define SDL_MOUSEBUTTONDOWN 0x401
#define SDL_MOUSEBUTTONUP 0x402
#define SDL_MOUSEWHEEL 0x403

#define SDLK_UNKNOWN 0
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 0x40000058
#define SDLK_ESCAPE 27
#define SDLK_BACKSPACE 8
#define SDLK_UP 0x40000052
#define SDLK_DOWN 0x40000051
#define SDLK_PAGEUP 0x4000004B
#define SDLK_PAGEDOWN 0x4000004E
#define SDLK_HOME 0x4000004A
#define SDLK_END 0x4000004D
#define SDLK_w 'w'
#define SDLK_s 's'
#define SDLK_q 'q'
#define SDLK_f 'f'
#define SDLK_F12 0x40000045

#define SDL_malloc malloc
#define SDL_free free
#define SDL_min(a, b) ((a) < (b) ? (a) : (b))
#define SDL_max(a, b) ((a) > (b) ? (a) : (b))
#define _stricmp strcasecmp

#ifdef CBE_PLATFORM_ANDROID
#include "cbe_log.h"
int cbe_android_printf(const char *fmt, ...);
const char *cbe_android_get_print_buffer(void);
#define printf(...) cbe_android_printf(__VA_ARGS__)
extern int finalLayerBuffer[];
#endif

static inline uint32_t SDL_GetTicks(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static inline void SDL_Delay(uint32_t ms)
{
    usleep(ms * 1000u);
}

static inline int SDL_PollEvent(SDL_Event *event)
{
    (void)event;
    return 0;
}

/* ARGB8888 for Android Bitmap.setPixels. */
static inline uint32_t SDL_MapRGB(SDL_PixelFormat *format, uint8_t r, uint8_t g,
                                  uint8_t b)
{
    (void)format;
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline int SDL_FillRect(SDL_Surface *sfc, const SDL_Rect *rect,
                               uint32_t color)
{
    int x0, y0, x1, y1, x, y;
    if (sfc == NULL || sfc->pixels == NULL)
        return -1;
    if (rect == NULL)
    {
        x0 = 0;
        y0 = 0;
        x1 = sfc->w;
        y1 = sfc->h;
    }
    else
    {
        x0 = rect->x;
        y0 = rect->y;
        x1 = rect->x + rect->w;
        y1 = rect->y + rect->h;
    }
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > sfc->w)
        x1 = sfc->w;
    if (y1 > sfc->h)
        y1 = sfc->h;
    for (y = y0; y < y1; ++y)
    {
        uint32_t *row = (uint32_t *)((uint8_t *)sfc->pixels + y * sfc->pitch);
        for (x = x0; x < x1; ++x)
            row[x] = color;
    }
    return 0;
}

static inline int SDL_SetClipRect(SDL_Surface *sfc, const SDL_Rect *rect)
{
    if (sfc == NULL)
        return 0;
    if (rect == NULL)
    {
        sfc->clip_rect.x = 0;
        sfc->clip_rect.y = 0;
        sfc->clip_rect.w = sfc->w;
        sfc->clip_rect.h = sfc->h;
        return 1;
    }
    sfc->clip_rect = *rect;
    return 1;
}

static inline SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
    static SDL_PixelFormat fmt;
    static SDL_Surface surface;
#ifdef CBE_PLATFORM_ANDROID
    (void)window;
    fmt.format = 0;
    surface.w = 240;
    surface.h = 400;
    surface.pitch = 240 * 4;
    surface.pixels = finalLayerBuffer;
    surface.format = &fmt;
    surface.clip_rect.x = 0;
    surface.clip_rect.y = 0;
    surface.clip_rect.w = surface.w;
    surface.clip_rect.h = surface.h;
    return &surface;
#else
    (void)window;
    (void)fmt;
    (void)surface;
    return NULL;
#endif
}

static inline void SDL_UpdateWindowSurface(SDL_Window *window)
{
    (void)window;
}

static inline const char *SDL_GetError(void)
{
    return "SDL window backend is disabled";
}

static inline int SDL_SaveBMP(SDL_Surface *surface, const char *path)
{
    (void)surface;
    (void)path;
    return -1;
}

static inline void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    (void)window;
    if (w) *w = 240;
    if (h) *h = 400;
}

static inline void SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
    (void)window;
    (void)w;
    (void)h;
}

static inline void SDL_SetTextInputRect(SDL_Rect *rect) { (void)rect; }
static inline void SDL_StartTextInput(void) {}
static inline void SDL_StopTextInput(void) {}
