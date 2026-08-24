#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ==================== 输出结构体 ==================== */
/*
 * 对应原始代码中 a2 指向的结构体:
 *   offset 0: uint16_t* pixels  (像素缓冲区指针)
 *   offset 4: uint16_t width   (图像宽度)
 *   offset 6: uint16_t height  (图像高度)
 *   offset 8: uint8_t  owned   (缓冲区是否由本函数分配)
 */
typedef struct
{
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint8_t owned;
} GifOutput;

typedef struct vm_img_result
{
    int pixelsPtr; // RGB565 数据指针
    short width;
    short height;
    char need_free;
} vm_img_result;


int gifDecodeExt(uint8_t *data, GifOutput *output, int alloc_new, int *mallocSize);
const char *gifDecodeGetLastError(void);
void free_mem(void *ptr);
