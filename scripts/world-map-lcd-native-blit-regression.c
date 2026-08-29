/* Regression for the optional host-owned ARM LCD blit routines.  The normal
 * CBE manager table remains on the established host LCD callbacks; this test
 * first asserts that default ABI, then explicitly opts in and verifies the
 * experiment's generic and map-shape copies, transparent pixels, register
 * preservation, and SVC fallback for an out-of-bounds rectangle. */

#include <stdio.h>
#include <string.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

enum
{
    TEST_SOURCE_INFO = VM_Memory_Pool_ADDRESS + 0x1000,
    TEST_SOURCE_PIXELS = VM_Memory_Pool_ADDRESS + 0x2000,
    TEST_DEST_INFO = VM_Memory_Pool_ADDRESS + 0x4000,
    TEST_DEST_PIXELS = VM_Memory_Pool_ADDRESS + 0x5000,
};

static int test_check(uc_err err, const char *operation)
{
    if (err == UC_ERR_OK)
        return 0;
    fprintf(stderr, "%s: %s\n", operation, uc_strerror(err));
    return 1;
}

static int test_write_image_header(u32 info, u32 pixels, u16 width, u16 height)
{
    if (test_check(uc_mem_write(MTK, info, &pixels, sizeof(pixels)), "write image pixels") ||
        test_check(uc_mem_write(MTK, info + 4, &width, sizeof(width)), "write image width") ||
        test_check(uc_mem_write(MTK, info + 6, &height, sizeof(height)), "write image height"))
    {
        return 1;
    }
    return 0;
}

static int test_set_call(u32 entry, u32 dst, u32 src, u32 sx, u32 sy,
                         u32 width, u32 height, u32 dx, u32 dy,
                         const u32 expected[13])
{
    u32 sp = STACK_ADDRESS + 0x800;
    u32 args[4] = {width, height, dx, dy};
    u32 lr = PROGRAM_EXIT_ADDR;
    u32 registers[13];
    uc_err err;

    if (test_check(uc_mem_write(MTK, sp, args, sizeof(args)), "write blit stack") ||
        test_check(uc_reg_write(MTK, UC_ARM_REG_SP, &sp), "set stack") ||
        test_check(uc_reg_write(MTK, UC_ARM_REG_LR, &lr), "set lr"))
    {
        return 1;
    }
    registers[0] = dst;
    registers[1] = src;
    registers[2] = sx;
    registers[3] = sy;
    for (u32 i = 4; i < 13; ++i)
        registers[i] = expected[i];
    for (u32 i = 0; i < 13; ++i)
    {
        if (test_check(uc_reg_write(MTK, (uc_arm_reg)(UC_ARM_REG_R0 + i), &registers[i]),
                       "set blit register"))
            return 1;
    }
    err = uc_emu_start(MTK, entry, PROGRAM_EXIT_ADDR, 0, 0);
    if (err != UC_ERR_OK)
    {
        u32 pc = 0, r0 = 0, r11 = 0;
        uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
        uc_reg_read(MTK, UC_ARM_REG_R0, &r0);
        uc_reg_read(MTK, UC_ARM_REG_R11, &r11);
        fprintf(stderr, "run lcd blit entry=%08x src=%08x sx=%u sy=%u w=%u h=%u dx=%u dy=%u pc=%08x r0=%08x r11=%08x: %s\n",
                entry, src, sx, sy, width, height, dx, dy, pc, r0, r11, uc_strerror(err));
        return 1;
    }
    for (u32 i = 0; i < 13; ++i)
    {
        u32 actual = 0;
        u32 want = i < 4 ? registers[i] : expected[i];
        if (test_check(uc_reg_read(MTK, (uc_arm_reg)(UC_ARM_REG_R0 + i), &actual),
                       "read blit register") ||
            actual != want)
        {
            fprintf(stderr, "register r%u changed: got=%08x expected=%08x\n", i, actual, want);
            return 1;
        }
    }
    return 0;
}

static int test_read_pixels(u32 pixels, u32 offset, const u16 *expected, u32 count,
                            const char *name)
{
    u16 actual[8] = {0};
    if (count > sizeof(actual) / sizeof(actual[0]) ||
        test_check(uc_mem_read(MTK, pixels + offset * 2, actual, count * 2), name))
    {
        return 1;
    }
    if (memcmp(actual, expected, count * sizeof(u16)) != 0)
    {
        fprintf(stderr, "%s pixels differ\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    uc_hook intrHook = 0;
    u32 tableEntry = 0;
    u32 preserved[13] = {0};
    u16 sourceOpaque[] = {0x0101, 0x0202, 0x0303, 0x0404,
                          0x0505, 0x0606, 0x0707, 0x0808};
    u16 sourceAlpha[] = {0x0000, 0x1111, 0x2222, 0x0000,
                         0x3333, 0x0000, 0x4444, 0x5555};
    u16 expectedOpaque[] = {0x0202, 0x0303, 0x0606, 0x0707};
    u16 expectedAlpha[] = {0xaaaa, 0x1111, 0x2222, 0xaaaa,
                           0x3333, 0xaaaa, 0x4444, 0x5555};
    u16 fallbackExpected[] = {0x0404, 0x0000};
    u16 clearPixels[8] = {0};
    u16 opaqueFast[24];
    u16 opaqueFastHead[8];
    u16 opaqueFastTail[4];
    u16 alphaFast[48];
    u16 alphaFastDest[24];
    u16 alphaFastExpected[48];
    u16 alphaGenericWide[72];
    u16 alphaGenericWideDest[35];
    u16 alphaGenericWideExpected[70];
    uc_err err;

    for (u32 i = 4; i < 13; ++i)
        preserved[i] = 0xa5000000u + i;
    err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &MTK);
    if (test_check(err, "open unicorn"))
        return 1;
    if (test_check(uc_mem_map(MTK, VM_Memory_Pool_ADDRESS, VM_MEMPOOL_TOTAL_SIZE, UC_PROT_ALL),
                   "map vm memory") ||
        test_check(uc_mem_map(MTK, VM_Manager_Table_ADDRESS, 0x100000, UC_PROT_ALL),
                   "map manager tables") ||
        test_check(uc_mem_map(MTK, VM_FUNC_HK_TABLE_ADDRESS, 0x100000, UC_PROT_ALL),
                   "map firmware routines") ||
        test_check(uc_mem_map(MTK, STACK_ADDRESS, 0x1000, UC_PROT_ALL), "map stack") ||
        test_check(uc_mem_map(MTK, PROGRAM_EXIT_ADDR, 0x1000, UC_PROT_ALL), "map exit") ||
        test_check(uc_hook_add(MTK, &intrHook, UC_HOOK_INTR, hookCpuIntr, NULL, 1, 0),
                   "install svc fallback"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    if (test_write_image_header(VM_screenImageStruct_ADDRESS, VM_screenImage_ADDRESS,
                                LCD_WIDTH, LCD_HEIGHT) ||
        test_write_image_header(TEST_SOURCE_INFO, TEST_SOURCE_PIXELS, 4, 2) ||
        test_write_image_header(TEST_DEST_INFO, TEST_DEST_PIXELS, 4, 2) ||
        test_check(uc_mem_write(MTK, TEST_SOURCE_PIXELS, sourceOpaque, sizeof(sourceOpaque)),
                   "write opaque source") ||
        test_check(uc_mem_write(MTK, VM_screenImage_ADDRESS, clearPixels, sizeof(clearPixels)),
                   "clear screen") ||
        test_check(uc_mem_write(MTK, TEST_DEST_PIXELS, clearPixels, sizeof(clearPixels)),
                   "clear fallback destination"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    SDL_setenv("CBE_ENABLE_EXPERIMENTAL_NATIVE_LCD_BLITS", "0", 1);
    vm_configManagerTable(VM_MANAGER_LCD_TABLE_ADDRESS, VM_MANAGER_LCD_FUNC_LIST_ADDRESS);
    if (test_check(uc_mem_read(MTK, VM_MANAGER_LCD_TABLE_ADDRESS + 24 * 4,
                               &tableEntry, sizeof(tableEntry)), "read default clip table entry") ||
        tableEntry != VM_MANAGER_LCD_FUNC_LIST_ADDRESS + 24 * 4)
    {
        fprintf(stderr, "default LCD manager unexpectedly selected native clip blitter\n");
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    SDL_setenv("CBE_ENABLE_EXPERIMENTAL_NATIVE_LCD_BLITS", "1", 1);
    vm_configManagerTable(VM_MANAGER_LCD_TABLE_ADDRESS, VM_MANAGER_LCD_FUNC_LIST_ADDRESS);
    if (test_check(uc_mem_read(MTK, VM_MANAGER_LCD_TABLE_ADDRESS + 24 * 4,
                               &tableEntry, sizeof(tableEntry)), "read clip table entry") ||
        tableEntry != VM_NATIVE_LCD_DRAW_IMAGE_CLIP_GUEST_ADDRESS ||
        test_set_call(tableEntry, VM_screenImageStruct_ADDRESS, TEST_SOURCE_INFO, 1, 0,
                      2, 2, 5, 6, preserved) ||
        test_read_pixels(VM_screenImage_ADDRESS, 6 * LCD_WIDTH + 5,
                         expectedOpaque, 2, "opaque row 0") ||
        test_read_pixels(VM_screenImage_ADDRESS, 7 * LCD_WIDTH + 5,
                         expectedOpaque + 2, 2, "opaque row 1"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    if (test_check(uc_mem_write(MTK, TEST_SOURCE_PIXELS, sourceAlpha, sizeof(sourceAlpha)),
                   "write alpha source"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    /* A 12x2 word-aligned opaque rectangle selects the map tile bulk path.
     * Check both the first and final pixels so all six loaded words per row
     * are covered. */
    for (u32 i = 0; i < 24; ++i)
        opaqueFast[i] = (u16)(0x5000u + i);
    memcpy(opaqueFastHead, opaqueFast, sizeof(opaqueFastHead));
    memcpy(opaqueFastTail, opaqueFast + 8, sizeof(opaqueFastTail));
    if (test_write_image_header(TEST_SOURCE_INFO, TEST_SOURCE_PIXELS, 12, 2) ||
        test_check(uc_mem_write(MTK, TEST_SOURCE_PIXELS, opaqueFast, sizeof(opaqueFast)),
                   "write fast opaque source") ||
        test_set_call(VM_NATIVE_LCD_DRAW_IMAGE_CLIP_GUEST_ADDRESS,
                      VM_screenImageStruct_ADDRESS, TEST_SOURCE_INFO, 0, 0,
                      12, 2, 4, 20, preserved) ||
        test_read_pixels(VM_screenImage_ADDRESS, 20 * LCD_WIDTH + 4,
                         opaqueFastHead, 8, "fast opaque row 0 head") ||
        test_read_pixels(VM_screenImage_ADDRESS, 20 * LCD_WIDTH + 12,
                         opaqueFastTail, 4, "fast opaque row 0 tail") ||
        test_read_pixels(VM_screenImage_ADDRESS, 21 * LCD_WIDTH + 4,
                         opaqueFast + 12, 8, "fast opaque row 1 head") ||
        test_read_pixels(VM_screenImage_ADDRESS, 21 * LCD_WIDTH + 12,
                         opaqueFast + 20, 4, "fast opaque row 1 tail"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    /* Restore the small source contract used by the generic alpha case. */
    if (test_write_image_header(TEST_SOURCE_INFO, TEST_SOURCE_PIXELS, 4, 2) ||
        test_check(uc_mem_write(MTK, TEST_SOURCE_PIXELS, sourceAlpha, sizeof(sourceAlpha)),
                   "restore alpha source"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }
    u16 alphaDest[] = {0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa,
                       0xaaaa, 0xaaaa, 0xaaaa, 0xaaaa};
    if (test_check(uc_mem_write(MTK, VM_screenImage_ADDRESS + (10 * LCD_WIDTH + 10) * 2,
                                alphaDest, 4 * sizeof(u16)), "seed alpha row 0") ||
        test_check(uc_mem_write(MTK, VM_screenImage_ADDRESS + (11 * LCD_WIDTH + 10) * 2,
                                alphaDest + 4, 4 * sizeof(u16)), "seed alpha row 1") ||
        test_check(uc_mem_read(MTK, VM_MANAGER_LCD_TABLE_ADDRESS + 25 * 4,
                               &tableEntry, sizeof(tableEntry)), "read alpha table entry") ||
        tableEntry != VM_NATIVE_LCD_DRAW_IMAGE_ALPHA_GUEST_ADDRESS ||
        test_set_call(tableEntry, VM_screenImageStruct_ADDRESS, TEST_SOURCE_INFO, 0, 0,
                      4, 2, 10, 10, preserved) ||
        test_read_pixels(VM_screenImage_ADDRESS, 10 * LCD_WIDTH + 10,
                         expectedAlpha, 4, "alpha row 0") ||
        test_read_pixels(VM_screenImage_ADDRESS, 11 * LCD_WIDTH + 10,
                         expectedAlpha + 4, 4, "alpha row 1"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    /* A 24x2 transparent rectangle is the 96x24 map-atlas shape.  Its
     * aligned destination must still retain every zero source pixel. */
    for (u32 i = 0; i < 48; ++i)
    {
        alphaFast[i] = (i % 5u) == 0 ? 0 : (u16)(0x6000u + i);
        alphaFastExpected[i] = alphaFast[i] == 0 ? 0xaaaau : alphaFast[i];
        if (i < 24)
            alphaFastDest[i] = 0xaaaau;
    }
    if (test_write_image_header(TEST_SOURCE_INFO, TEST_SOURCE_PIXELS, 24, 2) ||
        test_check(uc_mem_write(MTK, TEST_SOURCE_PIXELS, alphaFast, sizeof(alphaFast)),
                   "write fast alpha source") ||
        test_check(uc_mem_write(MTK, VM_screenImage_ADDRESS + (30 * LCD_WIDTH + 20) * 2,
                                alphaFastDest, sizeof(alphaFastDest)), "seed fast alpha row 0") ||
        test_check(uc_mem_write(MTK, VM_screenImage_ADDRESS + (31 * LCD_WIDTH + 20) * 2,
                                alphaFastDest, sizeof(alphaFastDest)), "seed fast alpha row 1") ||
        test_set_call(VM_NATIVE_LCD_DRAW_IMAGE_ALPHA_GUEST_ADDRESS,
                      VM_screenImageStruct_ADDRESS, TEST_SOURCE_INFO, 0, 0,
                      24, 2, 20, 30, preserved) ||
        test_read_pixels(VM_screenImage_ADDRESS, 30 * LCD_WIDTH + 20,
                         alphaFastExpected, 8, "fast alpha row 0 head") ||
        test_read_pixels(VM_screenImage_ADDRESS, 30 * LCD_WIDTH + 36,
                         alphaFastExpected + 16, 8, "fast alpha row 0 tail") ||
        test_read_pixels(VM_screenImage_ADDRESS, 31 * LCD_WIDTH + 20,
                         alphaFastExpected + 24, 8, "fast alpha row 1 head") ||
        test_read_pixels(VM_screenImage_ADDRESS, 31 * LCD_WIDTH + 36,
                         alphaFastExpected + 40, 8, "fast alpha row 1 tail"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    /* The active world-map trace is dominated by 35x19 transparent cards.
     * Width 35 has a 36-pixel source pitch, so this covers both the eight
     * pixel generic blocks and their final three-pixel tail on two rows. */
    for (u32 row = 0; row < 2; ++row)
    {
        for (u32 column = 0; column < 36; ++column)
        {
            u32 sourceIndex = row * 36 + column;
            alphaGenericWide[sourceIndex] = column == 35 ? 0x7fffu :
                ((column % 6u) == 0 ? 0 : (u16)(0x7000u + row * 35 + column));
            if (column < 35)
            {
                u32 outputIndex = row * 35 + column;
                alphaGenericWideDest[column] = 0xaaaau;
                alphaGenericWideExpected[outputIndex] = alphaGenericWide[sourceIndex] == 0 ?
                    0xaaaau : alphaGenericWide[sourceIndex];
            }
        }
    }
    if (test_write_image_header(TEST_SOURCE_INFO, TEST_SOURCE_PIXELS, 35, 2) ||
        test_check(uc_mem_write(MTK, TEST_SOURCE_PIXELS, alphaGenericWide,
                                sizeof(alphaGenericWide)), "write generic alpha source") ||
        test_check(uc_mem_write(MTK, VM_screenImage_ADDRESS + (50 * LCD_WIDTH + 30) * 2,
                                alphaGenericWideDest, sizeof(alphaGenericWideDest)),
                   "seed generic alpha row 0") ||
        test_check(uc_mem_write(MTK, VM_screenImage_ADDRESS + (51 * LCD_WIDTH + 30) * 2,
                                alphaGenericWideDest, sizeof(alphaGenericWideDest)),
                   "seed generic alpha row 1") ||
        test_set_call(VM_NATIVE_LCD_DRAW_IMAGE_ALPHA_GUEST_ADDRESS,
                      VM_screenImageStruct_ADDRESS, TEST_SOURCE_INFO, 0, 0,
                      35, 2, 30, 50, preserved) ||
        test_read_pixels(VM_screenImage_ADDRESS, 50 * LCD_WIDTH + 30,
                         alphaGenericWideExpected, 8, "generic alpha row 0 head") ||
        test_read_pixels(VM_screenImage_ADDRESS, 50 * LCD_WIDTH + 57,
                         alphaGenericWideExpected + 27, 8, "generic alpha row 0 tail") ||
        test_read_pixels(VM_screenImage_ADDRESS, 51 * LCD_WIDTH + 30,
                         alphaGenericWideExpected + 35, 8, "generic alpha row 1 head") ||
        test_read_pixels(VM_screenImage_ADDRESS, 51 * LCD_WIDTH + 57,
                         alphaGenericWideExpected + 62, 8, "generic alpha row 1 tail"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    /* srcX=3/w=2 is outside a 4-pixel source.  The ARM routine must restore
     * the CBE call frame and use the pre-existing SVC implementation, whose
     * clipping contract copies only the final source pixel. */
    if (test_write_image_header(TEST_SOURCE_INFO, TEST_SOURCE_PIXELS, 4, 2) ||
        test_check(uc_mem_write(MTK, TEST_SOURCE_PIXELS, sourceOpaque, sizeof(sourceOpaque)),
                   "restore fallback source") ||
        test_set_call(VM_NATIVE_LCD_DRAW_IMAGE_CLIP_GUEST_ADDRESS, TEST_DEST_INFO,
                      TEST_SOURCE_INFO, 3, 0, 2, 1, 0, 0, preserved) ||
        test_read_pixels(TEST_DEST_PIXELS, 0, fallbackExpected, 2, "fallback clip"))
    {
        uc_close(MTK);
        MTK = NULL;
        return 1;
    }

    uc_close(MTK);
    MTK = NULL;
    puts("world map LCD native blit regression passed");
    return 0;
}
