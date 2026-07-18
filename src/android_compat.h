#pragma once

/* Small SDL surface used by the JNI build.  Android owns the window, input
 * widgets and final presentation; the emulator only needs timing, events and
 * allocation names shared with the desktop sources. */
#include <android/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define CBE_LOG_TAG "CBE_EMU"

typedef int SDL_Keycode;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Surface SDL_Surface;
typedef struct SDL_PixelFormat SDL_PixelFormat;

typedef struct SDL_Rect
{
    int x;
    int y;
    int w;
    int h;
} SDL_Rect;

typedef struct SDL_Event
{
    int type;
    struct { struct { SDL_Keycode sym; } keysym; } key;
    struct { int x; int y; } motion;
    struct { int x; int y; } button;
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

#define SDLK_UNKNOWN 0
#define SDLK_RETURN 13
#define SDLK_KP_ENTER 13
#define SDLK_ESCAPE 27
#define SDLK_BACKSPACE 8
#define SDLK_F12 0x40000045

#define SDL_malloc malloc
#define SDL_free free
#define SDL_min(a, b) ((a) < (b) ? (a) : (b))
#define SDL_max(a, b) ((a) > (b) ? (a) : (b))
#define _stricmp strcasecmp

int cbe_android_printf(const char *fmt, ...);
const char *cbe_android_get_print_buffer(void);

#define printf(...) cbe_android_printf(__VA_ARGS__)

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

static inline SDL_Surface *SDL_GetWindowSurface(SDL_Window *window)
{
    (void)window;
    return NULL;
}

static inline const char *SDL_GetError(void)
{
    return "SDL window backend is owned by Android";
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
