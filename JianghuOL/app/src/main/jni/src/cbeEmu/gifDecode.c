#include "gifDecode.h"

#define GIF_FAIL(fmt, ...)                                      \
    do                                                          \
    {                                                           \
        snprintf(g_gifLastError, sizeof(g_gifLastError), fmt,   \
                 ##__VA_ARGS__);                                \
        return 0;                                               \
    } while (0)

#define ASSERT(cond)                                      \
    do                                                    \
    {                                                     \
        if (!(cond))                                      \
            GIF_FAIL("assert line=%d", __LINE__);         \
    } while (0)

/* ==================== LKV2 解码器全局状态 ==================== */
static uint16_t *LKV2_Palette;        /* 调色板, 512字节(256色, 每色2字节 RGB565) */
static int32_t *LKV2_Prefix;          /* LZW 前缀表, 4 * dictSize */
static uint8_t *LKV2_Suffix;          /* LZW 后缀表, dictSize */
static uint8_t *LKV2_OutCode;         /* LZW 输出栈, dictSize */
static uint8_t *LKV2_GIFArray;        /* GIF 数据流当前位置指针 */
static uint16_t *LKV2_PixelArray;     /* 像素输出缓冲区当前位置指针 */
static int16_t LKV2_TransparentIndex; /* 透明色索引 */
static int32_t LKV2_CurCodeSize;      /* 当前 LZW 码长 */
static int32_t LKV2_CurMaxCode;       /* 当前 LZW 最大有效码 = 1 << CurCodeSize */
static int32_t LKV2_CurBit;           /* 当前位偏移, -1 表示未初始化 */
static char g_gifLastError[128];

const char *gifDecodeGetLastError(void)
{
    return g_gifLastError;
}

/* ==================== GAME_ReadCode 静态状态 ==================== */
/*
 * 原始全局变量 (基址 0xD22ED0 + 偏移):
 *   lblk_GAME_ReadCode_2      - 当前子块剩余字节数
 *   CurByte_GAME_ReadCode_1   - 当前已读取到的字节索引
 *   以及独立全局 (基址 0xF035F8):
 *   b3_GAME_ReadCode_0        - 3字节窗口 [最早]
 *   b3_GAME_ReadCode          - 3字节窗口 [中间]
 *   b3_GAME_ReadCode_1        - 3字节窗口 [最新]
 */
static int32_t g_blk_remaining; /* lblk_GAME_ReadCode_2: 当前子块剩余字节数 */
static int32_t g_byte_idx;      /* CurByte_GAME_ReadCode_1: 当前字节索引 */
static uint8_t g_byte_win0;     /* b3_GAME_ReadCode_0: 3字节窗口最早字节 */
static uint8_t g_byte_win1;     /* b3_GAME_ReadCode:   3字节窗口中间字节 */
static uint8_t g_byte_win2;     /* b3_GAME_ReadCode_1: 3字节窗口最新字节 */

/* ==================== 内存分配封装 ==================== */
static void *alloc_mem(size_t size)
{
    void *ptr = malloc(size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

void free_mem(void *ptr)
{
    free(ptr);
}

/* ==================== LKV2 初始化 ==================== */
/*
 * 原始地址: 0x3d805c  gif_lkv2_Init
 * 功能: 分配并清零 512 字节的调色板 (仅首次调用分配)
 */
static void gif_lkv2_Init(void)
{
    if (LKV2_Palette == NULL)
    {
        LKV2_Palette = (uint16_t *)alloc_mem(512);
    }
}

/* ==================== LZW 码读取器 ==================== */
/*
 * 原始地址: 0x3d8098  GAME_ReadCode
 * 功能: 从 GIF 压缩数据流中读取一个 LZW 变长码
 *
 * GIF 子块格式:
 *   +0: 块大小 (1 字节, 0 表示数据结束)
 *   +1: 数据 (块大小字节)
 *   重复以上直到块大小为 0
 *
 * 位提取方式: GIF LSB-first
 *   用 3 字节滑动窗口 (byte_win0/1/2) 缓存数据
 *   将 24 位值右移 shift 位后与 mask 按位与得到码值
 *   shift = (CurBit % 8) + 17 - CurCodeSize
 *   mask  = CurMaxCode - 1
 */
static int32_t GAME_ReadCode(void)
{
    int32_t result = -1;
    int32_t old_byte_idx;
    int32_t new_byte_idx;
    int32_t bytes_to_read;
    int32_t bit_rem;
    int32_t shift;
    int32_t mask;
    int32_t i;
    int32_t raw;

    if (LKV2_CurBit == -1)
    {
        g_blk_remaining = 0;
        g_byte_idx = -1;
    }

    /* 捕获旧的字节索引, 然后更新 CurBit 和 byte_idx */
    old_byte_idx = g_byte_idx;

    LKV2_CurBit += LKV2_CurCodeSize;
    new_byte_idx = LKV2_CurBit / 8;

    g_byte_idx = new_byte_idx;
    bytes_to_read = new_byte_idx - old_byte_idx;

    bit_rem = LKV2_CurBit % 8;
    shift = bit_rem + 17 - LKV2_CurCodeSize;
    mask = LKV2_CurMaxCode - 1;

    /* 从 GIF 数据流读取所需字节到 3 字节窗口 */
    i = bytes_to_read;
    while (i > 0)
    {
        if (g_blk_remaining == 0)
        {
            g_blk_remaining = *LKV2_GIFArray++;
            if (g_blk_remaining == 0)
            {
                return result;
            }
        }
        g_byte_win0 = g_byte_win1;
        g_byte_win1 = g_byte_win2;
        g_byte_win2 = *LKV2_GIFArray++;
        g_blk_remaining--;
        i--;
    }

    /* 组合 24 位值, 右移并对齐 */
    raw = ((int32_t)g_byte_win2 << 16) |
          ((int32_t)g_byte_win1 << 8) |
          (int32_t)g_byte_win0;
    result = (raw >> shift) & mask;

    return result;
}

/* ==================== GIF 扩展解码主函数 ==================== */
/*
 * 原始地址: 0x3d811a  gifDecodeExt
 * 功能: 解码固件自定义 GIF 扩展数据, 输出 RGB565 像素缓冲
 *
 * 参数:
 *   data      - 指向 GIF 扩展数据头的指针
 *   output    - 输出结构体指针 (当 alloc_new=0 时需预置 pixels/width/height)
 *   alloc_new - 1: 内部分配像素缓冲区; 0: 使用 output 中已有缓冲区
 *
 * 返回值: 成功返回 1 (output 被填充), 失败返回 0
 *
 * 数据格式 (固件自定义封装, 非标准 GIF):
 *   data[0..3]: dictSize   大端序 32 位, 用于 LZW 字典分配
 *   data[4]:    flags      第 7 位 = 有局部调色板; 位 0-2 = 颜色深度
 *   data[5]:    保留
 *   data[6]:    控制字节 (0 表示进入解码循环)
 *   若 flags & 0x80:
 *     data[7..7+2*(2^(depth+1))-1]: 局部调色板 RGB565
 *   后跟图形控制扩展 (6 字节, 含透明色标志)
 *   接着是图像描述符块 (以 0x2C 分隔) 和 LZW 压缩子块
 */
int gifDecodeExt(uint8_t *data, GifOutput *output, int alloc_new, int *mallocSize)
{
    uint8_t flags;
    uint8_t *ptr;
    uint8_t first_char;
    int32_t color_count;
    int32_t max_root_code;
    int32_t loop_cond;
    char *tmp_ptr;
    int16_t trans_idx;
    int32_t img_width;
    int32_t img_height;
    int32_t img_width_pad;
    int32_t total_pixels;
    int32_t clear_code;
    int32_t next_code;
    int32_t cur_code;
    int32_t prev_code;
    int32_t saved_code;
    int32_t end_code;
    int32_t stack_idx;
    uint16_t *pixel_buf;
    int32_t dict_size;
    int32_t code_size;
    uint8_t separator;
    uint8_t packed;
    int32_t i;
    int32_t loop_cnt;
    uint8_t *src_ptr;

    g_gifLastError[0] = 0;
    gif_lkv2_Init();
    *mallocSize = 0;
    prev_code = 0;
    dict_size = ((int32_t)data[0] << 24) |
                ((int32_t)data[1] << 16) |
                ((int32_t)data[2] << 8) |
                (int32_t)data[3];
    if (dict_size <= 0 || dict_size > 1024 * 1024)
    {
        GIF_FAIL("bad dict_size=%d", dict_size);
    }
    flags = data[4];
    ptr = data + 5;
    first_char = 0;

    LKV2_GIFArray = ptr;

    if ((flags & 0x80) == 0)
    {
        GIF_FAIL("missing local palette flags=%02x dict_size=%d", flags, dict_size);
    }

    color_count = 1 << ((flags & 7) + 1);
    max_root_code = color_count - 1;
    if (color_count <= 0 || color_count > 256 || color_count > dict_size)
    {
        GIF_FAIL("bad color_count=%d flags=%02x dict_size=%d", color_count, flags, dict_size);
    }

    /* src 用于指向局部调色板源数据 (ptr + 2) */
    src_ptr = ptr + 2;
    loop_cond = (ptr[1] == 0);
    LKV2_GIFArray = ptr + 2;

    while (loop_cond)
    {
        /* 复制局部调色板 (每项 2 字节 RGB565) */
        memcpy((char *)LKV2_Palette, src_ptr, 2 * color_count);

        /* 调色板字节序转换: 大端转小端 (高字节 <-> 低字节) */
        tmp_ptr = (char *)LKV2_GIFArray + 2 * color_count;
        LKV2_GIFArray = (uint8_t *)tmp_ptr;
        int16_t *pal = LKV2_Palette;
        for (i = 0; i < color_count; i++)
        {
            int16_t val = pal[i];
            pal[i] = (((val & 0xFF) << 8) | ((val >> 8) & 0xFF));
        }

        /* 读取图形控制扩展 (Graphic Control Extension) */
        /* tmp_ptr 指向调色板后的数据 */
        /* tmp_ptr[3] 的最高位标识是否有透明色 */
        LKV2_GIFArray = (uint8_t *)(tmp_ptr + 3);
        if (tmp_ptr[3] & 1)// << 31
        {
            LKV2_GIFArray = (uint8_t *)(tmp_ptr + 6);
            trans_idx = (int16_t)tmp_ptr[6];
        }
        else
        {
            LKV2_GIFArray = (uint8_t *)(tmp_ptr + 6);
            trans_idx = (int16_t)256;
        }
        LKV2_TransparentIndex = trans_idx;

        /*
         * 检查下一个块是否为图像描述符 (0x2C = GIF Image Separator)
         * 原始代码:
         *   src = *(LKV2_GIFArray + 2);        // 读取偏移 2 处的字节
         *   v14 = LKV2_GIFArray + 3;            // 保存指针用于后续
         *   v9  = (src == 0x2C);                // 检查是否为 0x2C
         *   LKV2_GIFArray += 3;                 // 指针前进 3
         */
        separator = LKV2_GIFArray[2];
        tmp_ptr = (char *)(LKV2_GIFArray + 3);
        loop_cond = (separator == (uint8_t)0x2C);
        LKV2_GIFArray = (uint8_t *)tmp_ptr;
        if (loop_cond)
        {
            /*
             * 解析图像描述符
             * 原始代码:
             *   v15 = v14 + 4                  // tmp_ptr + 4 = 原始 LKV2_GIFArray + 7
             *
             * GIF Image Descriptor 格式 (标准):
             *   0x2C, left(2), top(2), width(2), height(2), packed(1)
             * 这里从 offset 7 开始读取, 跳过了 left(2) 和 top(2)
             *
             *   offset 7,8:   width  (小端序)
             *   offset 9,10:  height (小端序)
             *   offset 11:    packed field
             *   offset 12:    LZW minimum code size
             */
            src_ptr = (uint8_t *)(tmp_ptr + 4);

            img_width = ((int32_t)src_ptr[1] << 8) | src_ptr[0];
            img_height = ((int32_t)src_ptr[3] << 8) | src_ptr[2];
            if (img_width <= 0 || img_height <= 0 || img_width > 1024 || img_height > 1024)
            {
                GIF_FAIL("bad image_size=%dx%d dict_size=%d", img_width, img_height, dict_size);
            }
            LKV2_GIFArray = src_ptr + 4;

            /* 检查 packed field 的位 6-7 (原始: v20 >> 6) */
            packed = *LKV2_GIFArray;
            LKV2_GIFArray++;
            if ((packed >> 6) != 0)
            {
                GIF_FAIL("unsupported packed=%02x size=%dx%d", packed, img_width, img_height);
            }

            /* 读取 LZW 最小码长 */
            code_size = (int32_t)*LKV2_GIFArray;
            LKV2_GIFArray++;
            if (code_size <= 0 || code_size > 8)
            {
                GIF_FAIL("bad code_size=%d size=%dx%d", code_size, img_width, img_height);
            }

            /*
             * 初始化 LZW 解码参数
             * CurCodeSize = code_size + 1 (初始码长, GIF 标准规定)
             * CurMaxCode  = 1 << CurCodeSize
             * clear_code  = 1 << code_size   (清除码)
             * next_code   = clear_code + 2   (下一个可用码; clear_code+1 是 EOI)
             */
            LKV2_CurCodeSize = code_size + 1;
            LKV2_CurMaxCode = 1 << (code_size + 1);
            clear_code = 1 << code_size;
            end_code = clear_code + 1;
            next_code = clear_code + 2;
            if (next_code >= dict_size)
            {
                GIF_FAIL("dict too small dict_size=%d next_code=%d", dict_size, next_code);
            }

            /*
             * 计算行对齐填充
             * 原始代码:
             *   v24 = 4 - (v37 - 4 * (v37 / 4))  = 4 - (v37 % 4)
             *   v33 = v24 - 4 * (v24 / 4)        = v24 % 4
             * 由于 v37 是正数, 最终 img_width_pad = (4 - (v37 % 4)) % 4
             */
            {
                int32_t v37_mod4 = img_width & 3;
                int32_t v24 = (v37_mod4 == 0) ? 4 : (4 - v37_mod4);
                img_width_pad = v24 & 3;
            }

            /* 分配输出像素缓冲区 (每像素 2 字节 RGB565) */
            if (alloc_new == 1)
            {
                size_t tn = (size_t)(2 * (img_width + img_width_pad) * img_height);
                *mallocSize = tn;
                pixel_buf = (uint16_t *)alloc_mem(tn);
                if (!pixel_buf)
                {
                    ASSERT(0);
                    return 0;
                }
            }
            else
            {
                ASSERT(output->pixels != NULL);
                ASSERT((int32_t)output->width >= img_width);
                ASSERT((int32_t)output->height >= img_height);
                pixel_buf = output->pixels;
            }

            /* 分配 LZW 字典表 */
            LKV2_Prefix = (int32_t *)alloc_mem((size_t)(4 * dict_size));
            LKV2_Suffix = (uint8_t *)alloc_mem((size_t)dict_size);
            LKV2_OutCode = (uint8_t *)alloc_mem((size_t)dict_size);

            if (!LKV2_Prefix || !LKV2_Suffix || !LKV2_OutCode)
            {
                if (alloc_new == 1)
                    free_mem(pixel_buf);
                ASSERT(0);
                return 0;
            }

            LKV2_PixelArray = pixel_buf;
            stack_idx = 0;
            total_pixels = img_width * img_height;
            LKV2_CurBit = -1;
            loop_cnt = 0;
            while (1)
            {
                cur_code = GAME_ReadCode();
                loop_cnt++;
                if (total_pixels <= 0)
                {
                    break;
                }

                if (cur_code == clear_code)
                {
                    /* 清除码: 重置字典状态 */
                    LKV2_CurCodeSize = code_size + 1;
                    LKV2_CurMaxCode = 1 << (code_size + 1);
                    next_code = clear_code + 2;

                    prev_code = GAME_ReadCode();
                    if (prev_code < 0 || prev_code == end_code)
                        break;
                    if (prev_code > max_root_code)
                    {
                        free_mem(LKV2_OutCode);
                        free_mem(LKV2_Suffix);
                        free_mem(LKV2_Prefix);
                        if (alloc_new == 1)
                            free_mem(pixel_buf);
                        GIF_FAIL("bad prev_code=%d max_root=%d", prev_code, max_root_code);
                    }
                    first_char = (uint8_t)prev_code;

                    *LKV2_PixelArray = LKV2_Palette[first_char];
                    total_pixels--;
                    if ((total_pixels % img_width) == 0)
                    {
                        LKV2_PixelArray += img_width_pad;
                    }
                    LKV2_PixelArray++;
                }
                else
                {
                    if (cur_code < 0 || cur_code == end_code)
                        break;
                    saved_code = cur_code;

                    if (cur_code >= next_code)
                    {
                        if (prev_code < 0)
                        {
                            free_mem(LKV2_OutCode);
                            free_mem(LKV2_Suffix);
                            free_mem(LKV2_Prefix);
                            if (alloc_new == 1)
                                free_mem(pixel_buf);
                            GIF_FAIL("bad kwkwk cur=%d next=%d prev=%d", cur_code, next_code, prev_code);
                        }
                        /* 固件在 cur_code >= next_code 时统一按 KwKwK 分支处理。 */
                        cur_code = prev_code;
                        LKV2_OutCode[0] = first_char;
                        stack_idx = 1;
                    }

                    /* 沿前缀链回溯, 将字节推入输出栈 */
                    while (cur_code > max_root_code)
                    {
                        if (cur_code < 0 || cur_code >= dict_size || cur_code >= next_code || stack_idx >= dict_size)
                        {
                            free_mem(LKV2_OutCode);
                            free_mem(LKV2_Suffix);
                            free_mem(LKV2_Prefix);
                            if (alloc_new == 1)
                                free_mem(pixel_buf);
                            GIF_FAIL("bad prefix cur=%d next=%d stack=%d dict=%d", cur_code, next_code, stack_idx, dict_size);
                        }
                        LKV2_OutCode[stack_idx] = LKV2_Suffix[cur_code];
                        stack_idx++;
                        cur_code = LKV2_Prefix[cur_code];
                    }
                    if (cur_code < 0 || cur_code > max_root_code || stack_idx >= dict_size)
                    {
                        free_mem(LKV2_OutCode);
                        free_mem(LKV2_Suffix);
                        free_mem(LKV2_Prefix);
                        if (alloc_new == 1)
                            free_mem(pixel_buf);
                        GIF_FAIL("bad root cur=%d max_root=%d stack=%d dict=%d", cur_code, max_root_code, stack_idx, dict_size);
                    }

                    first_char = (uint8_t)cur_code;
                    LKV2_OutCode[stack_idx] = (uint8_t)cur_code;

                    /* 从栈中弹出并输出像素 */
                    while (stack_idx >= 0 && total_pixels > 0)
                    {
                        *LKV2_PixelArray =
                            LKV2_Palette[LKV2_OutCode[stack_idx]];
                        total_pixels--;
                        if ((total_pixels % img_width) == 0)
                        {
                            LKV2_PixelArray += img_width_pad;
                        }
                        LKV2_PixelArray++;
                        stack_idx--;
                    }

                    /* 将新串加入字典 */
                    if (next_code < dict_size)
                    {
                        LKV2_Prefix[next_code] = prev_code;
                        LKV2_Suffix[next_code] = first_char;
                    }
                    stack_idx = 0;
                    prev_code = saved_code;
                    if (next_code < dict_size)
                        next_code++;

                    /* 检查是否需要增加码长 */
                    if (next_code >= LKV2_CurMaxCode && LKV2_CurCodeSize < 12)
                    {
                        LKV2_CurMaxCode *= 2;
                        LKV2_CurCodeSize++;
                    }
                }
            }

            /* 释放 LZW 字典内存 */
            free_mem(LKV2_OutCode);
            free_mem(LKV2_Suffix);
            free_mem(LKV2_Prefix);
            LKV2_OutCode = NULL;
            LKV2_Suffix = NULL;
            LKV2_Prefix = NULL;

            ASSERT(output != NULL);
            output->pixels = pixel_buf;
            output->width = (uint16_t)img_width;
            output->height = (uint16_t)img_height;
            output->owned = (uint8_t)(alloc_new == 1);

            return 1;
        }
    }
    GIF_FAIL("no image separator dict_size=%d flags=%02x", dict_size, flags);
}
/* ==================== 资源释放 ==================== */
void gifReleaseOutput(GifOutput *output)
{
    if (output && output->owned && output->pixels)
    {
        free_mem(output->pixels);
        output->pixels = NULL;
        output->owned = 0;
    }
}

void gifCleanup(void)
{
    if (LKV2_Palette)
    {
        free_mem(LKV2_Palette);
        LKV2_Palette = NULL;
    }
    if (LKV2_Prefix)
    {
        free_mem(LKV2_Prefix);
        LKV2_Prefix = NULL;
    }
    if (LKV2_Suffix)
    {
        free_mem(LKV2_Suffix);
        LKV2_Suffix = NULL;
    }
    if (LKV2_OutCode)
    {
        free_mem(LKV2_OutCode);
        LKV2_OutCode = NULL;
    }
}
