#pragma once
#include "config.h"
#ifdef CBE_PLATFORM_ANDROID
#include "android_compat.h"
#else
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"
#endif


extern SDL_Window *window;
extern u8* Lcd_Cache_Buffer;
#ifdef CBE_PLATFORM_ANDROID
extern int finalLayerBuffer[LCD_WIDTH * LCD_HEIGHT];
#endif

typedef enum
{
    VM_LCD_ROTATE_0 = 0,
    VM_LCD_ROTATE_90_CW = 1,
    VM_LCD_ROTATE_180 = 2,
    VM_LCD_ROTATE_90_CCW = 3
} vm_lcd_rotation;

void InitLcd();
void UpdateLcd();
void LcdSetRotation(vm_lcd_rotation rotation);
vm_lcd_rotation LcdGetRotation(void);
void LcdCycleRotation(void);
const char *LcdRotationName(vm_lcd_rotation rotation);
int LcdGetViewWidth(void);
int LcdGetViewHeight(void);
int LcdGetWindowWidth(void);
int LcdGetWindowHeight(void);
int LcdGetToolbarHeight(void);
void LcdApplyWindowSize(void);
void LcdWindowPointToVm(int windowX, int windowY, int *vmX, int *vmY);
void LcdVmRectToWindowRect(int x, int y, int w, int h, SDL_Rect *rect);
int LcdWindowPointInToolbar(int windowX, int windowY);
int LcdHandleToolbarMouseDown(int windowX, int windowY);
