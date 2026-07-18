#include <stdio.h>
#include <assert.h>
#include "config.h"
#ifdef CBE_PLATFORM_ANDROID
#include "android_compat.h"
#else
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"
#endif

typedef struct VMBlock
{
    u32 addr; // VM地址
    u32 size;
    u32 req_size;
    int used;
    struct VMBlock *next;
} VMBlock;

void InitVmMalloc();
void vm_free(u32 addr);
u32 vm_malloc(u32 size);
int vm_malloc_reserve_range(u32 addr, u32 size);
u32 vm_malloc_user_size(u32 addr);
