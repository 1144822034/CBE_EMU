#include "main.h"
#include "imgDecodeUtil.c"

FILE *openFileList[16];

u32 vm_malloc_var();
u32 vm_get_var(u32 addr);
u8 vm_get_var_byte(u32 addr);
void vm_set_var_byte(u32 addr, u8 v);
void vm_set_var(u32 addr, u32 v);
u16 vm_get_var_short(u32 addr);
void vm_set_var_short(u32 addr, u16 v);
void vm_free_var(u32 addr);

u32 vm_malloc(u32 size);
void vm_free(u32 addr);
int vm_strlen(int addr);
int vm_memcpy(int dstAddr, int srcAddr, int len);
u8 vm_DF_DataPackage_GetPackageIndex(int a1);
void vm_DF_DataPackage_LoadPackage(int a1, int a2);
int vm_DF_DataPackage_InitTxt(int a1, int a2);

u16 vm_DF_ReadShort(u32 bufPtr, u32 offsetPtr);
void vm_DF_WriteShort(bufPtr, offsetPtr, value);
int vm_DF_ReadInt(int a1, int a2);
void vm_DF_WriteInt(bufPtr, offsetPtr, value);

int vm_DF_Malloc_IN(int a1, int a2);
int vm_DF_ReadStringEx(int a1, int a2, int a3);
bool vm_DF_String_Equal(int a1, int a2);
int vm_DF_DataPackage_LocateDataPackage(int a1, int a2);
int vm_DF_DataPackage_GetPackageID(int a1, int a2);
int vm_DF_Free(int p);
int vm_DF_DataPackage_LoadFromTResource(int a1, int a2);
int vm_DF_File_ReadToBuffer(int fileHandle, int buffer, int size);
int VM_DF_DataPackage_DoLoading(int a1, int a2, int a3);
int vm_DF_DataPackage_LoadFormTCard(int a1);
int vm_DF_DataPackage_LoadFormTCardEx(int a1, int path, int offset_base);
int vm_DF_File_ReadInt(int fileHandle);

int vm_cbfs_vm_file_open(int openMode, int namePtr, int rwPtr);
int vm_cbfs_vm_file_read(int buffer, int size, int handle);
int vm_cbfs_vm_file_write(int bufferPtr, int size, int fileHandle);
int vm_cbfs_vm_file_tell(int fileHandle);
int vm_cbfs_vm_file_close(int fileHandle);
void vm_initDFDataPackage(u32 a1, u32 a2);
void vm_sprintf();
void vm_DF_GetFormatString();
void vm_DF_GetMemoryBlock();
u32 vm_set_call_result(u32 r);

// 真机
// int off = 0
// 虚拟
// int off = vm_malloc_var()
// vm_set_var(off,0)

// 真机
// int p = off
// 虚拟
// int p = vm_get_var(off)
//

// 真机
// my_func(&off)
// 虚拟
// my_func(off)

inline u32 vm_malloc_var()
{
    return vm_malloc(4);
}

inline u32 vm_get_var(u32 addr)
{
    u32 a;
    uc_mem_read(MTK, addr, &a, 4);
    return a;
}
inline u8 vm_get_var_byte(u32 addr)
{
    u8 a;
    uc_mem_read(MTK, addr, &a, 1);
    return a;
}
inline void vm_set_var_byte(u32 addr, u8 v)
{
    uc_mem_write(MTK, addr, &v, 1);
}

inline void vm_set_var(u32 addr, u32 v)
{
    uc_mem_write(MTK, addr, &v, 4);
}
inline u16 vm_get_var_short(u32 addr)
{
    u16 a;
    uc_mem_read(MTK, addr, &a, 2);
    return a;
}

inline void vm_set_var_short(u32 addr, u16 v)
{
    uc_mem_write(MTK, addr, &v, 2);
}

inline void vm_free_var(u32 addr)
{
    vm_free(addr);
}

inline u32 vm_set_call_result(u32 r)
{
    uc_reg_write(MTK, UC_ARM_REG_R0, &r);
    return r;
}

// ok
int vm_strlen(int addr)
{
    char buff[1024];
    vm_readStringByPtr(addr, buff);
    int len = strlen(buff);
    return vm_set_call_result(len);
}
// ok
int vm_memcpy(int dstAddr, int srcAddr, int len)
{
    u8 buff[len];
    uc_mem_read(MTK, srcAddr, &buff, len);
    uc_mem_write(MTK, dstAddr, &buff, len);
}
int vm_get_file_handle(char *nameBuf)
{
    int handle = -1;
    for (int i = 0; i < 16; i++)
    {
        if (openFileList[i] == NULL)
        {
            handle = i;
            openFileList[i] = fopen(nameBuf, "rb");
            return handle;
        }
    }
    return -1;
}
// ok
int vm_cbfs_vm_file_open(int openMode, int namePtr, int rwPtr)
{
    char rwBuff[128];
    char nameBuff[128];
    uc_mem_read(MTK, namePtr, &nameBuff, 128);
    printf("[vm_cbfs_vm_file_open] name=%s\n", nameBuff);
    uc_mem_read(MTK, rwPtr, &rwBuff, 128);
    int handle = vm_get_file_handle(nameBuff);
    return vm_set_call_result(handle);
}
// ok
int vm_cbfs_vm_file_read(int bufferPtr, int size, int handle)
{
    char *tmp = SDL_malloc(size);
    int readed = fread(tmp, 1, size, openFileList[handle]);

    if (readed > 0)
        uc_mem_write(MTK, bufferPtr, tmp, readed);
    SDL_free(tmp);
    return readed;
}
// ok
int vm_cbfs_vm_file_write(int bufferPtr, int size, int fileHandle)
{
    void *buffer = SDL_malloc(size);
    uc_mem_read(MTK, bufferPtr, &buffer, size);
    int r = fwrite(buffer, 1, size, openFileList[fileHandle]);
    SDL_free(buffer);
    return vm_set_call_result(r);
}
// ok
int vm_cbfs_vm_file_seek(int handle, int pos, int type)
{
    int r = fseek(openFileList[handle], pos, type);
    // printf("vm_cbfs_vm_file_seek:%x,%d\n", pos, type);
    return vm_set_call_result(r);
}
// ok
int vm_cbfs_vm_file_tell(int fileHandle)
{
    int r = ftell(openFileList[fileHandle]);
    return vm_set_call_result(r);
}

int vm_cbfs_vm_file_exists(int disk, int namePtr)
{
    vm_readStringByPtr(namePtr, cbeTextString);
    FILE *f = fopen(cbeTextString, "rb");
    u32 r = 0;
    if (f != NULL)
        r = 1;
    fclose(f);
    return vm_set_call_result(r);
}

int vm_cbfs_vm_file_delete(int disk, int namePtr)
{
    uc_mem_read(MTK, namePtr, cbeTextString, 128);
    FILE *f = fopen(cbeTextString, "rb");
    u32 r = 0;
    if (f != NULL)
        r = 1;
    fclose(f);
    if (r = 1)
        unlink(cbeTextString);
    return vm_set_call_result(r);
}
int vm_cbfs_vm_file_getfilesize(int fileHandle)
{
    FILE *f = openFileList[fileHandle];
    fseek(f, 0, SEEK_END);
    int r = ftell(f);
    fseek(f, 0, SEEK_SET);
    return vm_set_call_result(r);
}
// ok
int vm_cbfs_vm_file_close(int fileHandle)
{
    int r = fclose(openFileList[fileHandle]);
    openFileList[fileHandle] = NULL;
    return vm_set_call_result(r);
}
u8 vm_DF_DataPackage_GetPackageIndex(int a1)
{
    u16 v1; // r2
    int i;  // r1
    int v3; // r3
    int v5; // r1
    u16 len;
    int tmp;

    v1 = 1;
LABEL_8:
    v5 = v1;
    v1 = (v1 + 1);
    if (!v5)
        return 0;
    uc_mem_read(MTK, a1 + 10, &len, 2);
    for (i = 0; len > i; i++)
    {
        uc_mem_read(MTK, a1 + 28, &tmp, 4);
        uc_mem_read(MTK, tmp + 4 * i, &v3, 4);
        uc_mem_read(MTK, v3 + 1, &tmp, 1);
        if (v3 && tmp == v1)
            goto LABEL_8;
    }
    return (u8)v1;
}

void vm_DF_DataPackage_LoadPackage(int a1, int a2)
{
    int16_t count;
    int i = 0;
    int result = 0;
    int base, entry;

    if (!a2)
    {
        vm_set_var_byte(a1, 0);
        return vm_set_call_result(0);
    }

    count = vm_get_var_short(a1 + 10);
    base = vm_get_var(a1 + 28);

    for (i = 0; i < count; i++)
    {
        entry = vm_get_var(base + 4 * i);

        if (!entry)
        {
            // alloc entry
            vm_DF_Malloc_IN(base + 4 * i, 108);
            u32 nextEntry = vm_get_var(base + 4 * i);

            int16_t len = (int16_t)vm_strlen(a2);

            int buf = vm_malloc(len + 1);
            vm_set_var(nextEntry + 4, buf);

            vm_memcpy(buf, a2, len);

            vm_set_var_byte(buf + len, 0);
            vm_set_var_short(nextEntry + 10, 0);
            vm_set_var_byte(buf, 0);

            result = vm_DF_DataPackage_GetPackageIndex(a1);

            u8 idx8 = result;
            uc_mem_write(MTK, nextEntry + 1, &idx8, 1);
            return vm_set_call_result(result);
        }
    }

    // 扩容路径（略，和上面一样逻辑）
    vm_DF_Malloc_IN(base + 4 * i, 108);
    u32 nextEntry = vm_get_var(base + 4 * i);

    int16_t len = (int16_t)vm_strlen(a2);

    int buf = vm_malloc(len + 1);
    vm_set_var(nextEntry + 4, buf);

    vm_memcpy(buf, a2, len);

    vm_set_var_byte(buf + len, 0);
    vm_set_var_short(nextEntry + 10, 0);
    vm_set_var_byte(buf, 0);

    result = vm_DF_DataPackage_GetPackageIndex(a1);

    u8 idx8 = result;
    uc_mem_write(MTK, nextEntry + 1, &idx8, 1);
    return vm_set_call_result(result);
}

int vm_DF_DataPackage_InitTxt(int a1, int a2)
{
    int fileHandle;
    int index;
    int16_t count;
    int offset_table_ptr;
    int base_offset;
    int total_size;
    int txt_buffer;
    int offset;
    int len;
    int bufferPtr;
    uc_engine *uc = MTK;

    // 读取结构字段
    uc_mem_read(uc, a1 + 92, &fileHandle, 4);
    uc_mem_read(uc, a1 + 104, &index, 4);
    uc_mem_read(uc, a1 + 8, &count, 2);
    uc_mem_read(uc, a1 + 100, &txt_buffer, 4);

    printf("DF_DataPackage_InitTxt 222:%x,%x\n", a2, count);

    // 条件判断
    if (fileHandle < 0 || count <= index || txt_buffer)
    {
        printf("DF_DataPackage_InitTxt error :%x\n", fileHandle);
        return 0;
    }

    // 读取 offset_table 指针
    uc_mem_read(uc, a1 + 16, &offset_table_ptr, 4);

    // 读取 offset = offset_table[index]
    uc_mem_read(uc, offset_table_ptr + index * 4, &offset, 4);

    // 读取 base_offset
    uc_mem_read(uc, a1 + 88, &base_offset, 4);

    // seek
    vm_cbfs_vm_file_seek(fileHandle, offset + base_offset, 0);

    // 读取 total_size
    uc_mem_read(uc, a1 + 96, &total_size, 4);

    // 计算长度
    len = total_size - offset;

    // 分配 VM 内存
    bufferPtr = vm_malloc(len + 1);

    // 写回 buffer 指针
    uc_mem_write(uc, a1 + 100, &bufferPtr, 4);

    // 读取文件内容到 VM 内存
    return vm_cbfs_vm_file_read(bufferPtr, len, fileHandle);
}
u16 vm_DF_ReadShort(u32 bufPtr, u32 offsetPtr)
{
    u32 offset, ret;
    uc_mem_read(MTK, offsetPtr, &offset, 4);

    uc_mem_read(MTK, bufPtr + offset, &ret, 2);
    offset += 2;
    uc_mem_write(MTK, offsetPtr, &offset, 4);
    return vm_set_call_result(ret);
}

void vm_DF_WriteShort(bufPtr, offsetPtr, value)
{
    u32 offset, ret;
    uc_mem_read(MTK, offsetPtr, &offset, 4);

    uc_mem_write(MTK, bufPtr + offset, &value, 2);
    offset += 2;
    uc_mem_write(MTK, offsetPtr, &offset, 4);
    return vm_set_call_result(offset);
}
// ok
// buffPtr offsetPtr
int vm_DF_ReadInt(int a1, int a2)
{
    u32 offset;
    u8 arr[4];
    int result;

    offset = vm_get_var(a2);
    result = vm_get_var(a1 + offset);

    uc_mem_read(MTK, a1 + offset, &arr, 4);
    result = arr[0] | (arr[1] << 8) | (arr[2] << 16) | (arr[3] << 24);

    offset += 4;
    vm_set_var(a2, offset);
    return vm_set_call_result(result);
}
void vm_DF_WriteInt(a1, a2, value)
{
    u32 offset, ret;
    u8 arr[4];
    offset = vm_get_var(a2);
    arr[0] = value & 0xff;
    arr[1] = (value >> 8) & 0xff;
    arr[2] = (value >> 16) & 0xff;
    arr[3] = (value >> 24) & 0xff;
    uc_mem_write(MTK, a1 + offset, arr, 4);

    offset += 4;
    vm_set_var(a2, offset);
    return vm_set_call_result(offset);
}
// ok
// buffPtr,size
int vm_DF_Malloc_IN(int a1, int a2)
{
    int ptr = vm_malloc(a2);
    vm_set_var(a1, ptr);
    return vm_set_call_result(1);
}
int vm_DF_ReadStringEx(int a1, int a2, int a3)
{
    u32 offset;
    u8 len;
    int buf_ptr;
    offset = vm_get_var(a3);
    len = vm_get_var(a2 + offset);

    offset++;

    buf_ptr = vm_malloc(len + 1);
    vm_set_var(a1, buf_ptr);

    // 一次性 memcpy（核心优化）
    vm_memcpy(buf_ptr, a2 + offset, len);

    offset += len;
    vm_set_var(a3, offset);

    vm_set_var(buf_ptr + len, 0);

    return vm_set_call_result(0);
}

bool vm_DF_String_Equal(int a1, int a2)
{

    int i = 0;
    u8 c1, c2;

    while (1)
    {
        // 读 a1[i]
        uc_mem_read(MTK, a1 + i, &c1, 1);

        if (c1 == 0)
            break;

        // 读 a2[i]
        uc_mem_read(MTK, a2 + i, &c2, 1);

        if (c1 != c2)
        {
            return vm_set_call_result(0);
        }

        i++;
    }

    // a1结束，检查a2是否也结束
    c2 = vm_get_var_byte(a2 + i);
    return vm_set_call_result(c2 == 0);
}
// ptr,ptr
int vm_DF_DataPackage_LocateDataPackage(int a1, int namePtr)
{
    int16_t count;
    int16_t i;
    int base;

    // 如果 a2 == NULL，直接返回自身
    if (namePtr == 0)
        return a1;

    // 读取 count
    count = vm_get_var_short(a1 + 10);
    base = vm_get_var(a1 + 28);

    for (i = 0; i < count; i++)
    {
        // entry = list[i]
        u32 entry = vm_get_var(base + 4 * i);
        if (entry)
        {
            // entry->name
            int str_ptr = vm_get_var(entry + 4);

            if (vm_DF_String_Equal(str_ptr, namePtr))
            {
                return vm_set_call_result(entry);
            }
        }
    }

    return vm_set_call_result(0);
}
int vm_DF_DataPackage_GetPackageID(int a1, int a2)
{
    int16_t count, i;
    int base, entry, str_ptr;
    int r;

    if (!a2)
        return 0;

    uc_mem_read(MTK, a1 + 10, &count, 2);
    uc_mem_read(MTK, a1 + 28, &base, 4);

    for (i = 0; i < count; i++)
    {
        uc_mem_read(MTK, base + 4 * i, &entry, 4);

        if (!entry)
            continue;

        uc_mem_read(MTK, entry + 4, &str_ptr, 4);

        if (vm_DF_String_Equal(str_ptr, a2))
        {
            return vm_set_call_result(i + 1);
        }
    }
    return vm_set_call_result(0);
}
// ok
int vm_DF_Free(int p)
{
    if (p)
    {
        // 先写0（防止UAF崩）
        u32 zero = 0;
        uc_mem_write(MTK, p, &zero, 4);

        // 再free
        vm_free(p);
    }
    return vm_set_call_result(p);
}
int vm_DF_DataPackage_LoadFromTResource(int a1, int a2)
{
    int n4, result = 0;
    int v22 = VM_Str_Tmp_ADDRESS;     // offset
    int v17 = VM_Str_Tmp_ADDRESS + 4; // name ptr
    int v19, v20;
    int v18;
    int v27 = a2;

    uc_mem_write(MTK, v17, &result, 4); //*v17 = 0
    // header
    uc_mem_write(MTK, v22, &result, 4); //*v22 = 0
    n4 = vm_DF_ReadInt(v27, v22);

    int tmp = -1;
    uc_mem_write(MTK, a1 + 26 * 4, &tmp, 4);

    if (n4 > 4)
    {
        int val = vm_DF_ReadInt(v27, v22);
        uc_mem_write(MTK, a1 + 26 * 4, &val, 4);
    }

    v20 = vm_DF_ReadInt(v27, v22);
    v19 = n4 + 4;

    while (1)
    {
        v18 = result;
        if (result >= v20)
            break;

        if (v18)
        {
            vm_DF_ReadStringEx(v17, v27, v22);
            v19 = vm_DF_ReadInt(v27, v22);
        }

        int entry = vm_DF_DataPackage_LocateDataPackage(a1, v17);

        if (entry)
        {
            u8 flag;
            uc_mem_read(MTK, entry, &flag, 1);

            if (!flag)
            {
                u8 idx_base;
                uc_mem_read(MTK, entry + 1, &idx_base, 1);
                int v23 = idx_base << 8;

                int v21 = v19;
                vm_DF_ReadInt(v27, v21);
                int v24 = vm_DF_ReadInt(v27, v21);
                int v7 = vm_DF_ReadInt(v27, v21);

                u16 cnt16 = (u16)v7;
                uc_mem_write(MTK, entry + 8, &cnt16, 2);

                int int_arr = 0, str_arr = 0, idx_arr = 0;

                if (v7)
                {
                    int pkg_id = vm_DF_DataPackage_GetPackageID(a1, v17);

                    if (pkg_id)
                    {
                        int base;
                        uc_mem_read(MTK, a1 + 7 * 4, &base, 4);

                        int item;
                        uc_mem_read(MTK, base + (pkg_id - 1) * 4, &item, 4);

                        int tmp_ptr;

                        tmp_ptr = item + 12;
                        vm_DF_Malloc_IN(tmp_ptr, 4 * v7);
                        uc_mem_read(MTK, tmp_ptr, &int_arr, 4);

                        tmp_ptr = item + 16;
                        vm_DF_Malloc_IN(tmp_ptr, 4 * v7);
                        uc_mem_read(MTK, tmp_ptr, &str_arr, 4);

                        tmp_ptr = item + 20;
                        vm_DF_Malloc_IN(tmp_ptr, 2 * v7);
                        uc_mem_read(MTK, tmp_ptr, &idx_arr, 4);
                    }
                    else
                    {
                        vm_DF_Malloc_IN(a1 + 3 * 4, 4 * v7);
                        uc_mem_read(MTK, a1 + 3 * 4, &int_arr, 4);

                        vm_DF_Malloc_IN(a1 + 4 * 4, 4 * v7);
                        uc_mem_read(MTK, a1 + 4 * 4, &str_arr, 4);

                        vm_DF_Malloc_IN(a1 + 5 * 4, 2 * v7);
                        uc_mem_read(MTK, a1 + 5 * 4, &idx_arr, 4);
                    }

                    uc_mem_write(MTK, entry + 16, &int_arr, 4);
                    uc_mem_write(MTK, entry + 12, &str_arr, 4);
                    uc_mem_write(MTK, entry + 20, &idx_arr, 4);

                    for (int i = 0; i < v7; i = (i + 1))
                    {
                        int val = vm_DF_ReadInt(v27, v21);
                        uc_mem_write(MTK, int_arr + 4 * i, &val, 4);
                    }

                    for (int j = 0; j < v7; j = (j + 1))
                    {
                        int str_ptr = str_arr + 4 * j;
                        vm_DF_ReadStringEx(str_ptr, v27, v21);

                        u16 idx = (u16)(v23++);
                        uc_mem_write(MTK, idx_arr + 2 * j, &idx, 2);
                    }
                }

                if (v24)
                {
                    int data_ptr = v27 + v21;
                    uc_mem_write(MTK, entry + 24, &data_ptr, 4);

                    int id = vm_DF_DataPackage_GetPackageID(a1, v17);
                    if (id)
                    {
                        int base;
                        uc_mem_read(MTK, a1 + 7 * 4, &base, 4);

                        int item;
                        uc_mem_read(MTK, base + (id - 1) * 4, &item, 4);

                        uc_mem_write(MTK, item + 96, &v24, 4);
                    }
                    else
                    {
                        uc_mem_write(MTK, a1 + 24 * 4, &v24, 4);
                    }
                }

                u8 one = 1;
                uc_mem_write(MTK, entry, &one, 1);
            }
        }

        if (v17)
        {
            vm_DF_Free(v17);
            v17 = 0;
        }

        result = (int16_t)(v18 + 1);
    }

    return result;
}
int vm_DF_DataPackage_LoadFormTCardEx(int a1, int pathPtr, int fileSeekPos)
{
    int fileHandle;

    int v34 = 0;

    int namePtr = 0;
    int count, i = 0;
    int pkg_id = 0;

    fileHandle = vm_get_var(a1 + 92);

    if (fileHandle == -1)
    {
        char rwBuffer[] = {"rb"}; // 注意，windows中要按二进制读取，真机中的r = windows中的rb
        u32 ptr = vm_malloc(4);
        // vm中新建"r"内存
        uc_mem_write(MTK, ptr, &rwBuffer, 2);
        fileHandle = vm_cbfs_vm_file_open(2, pathPtr, ptr);
        vm_free(ptr);
        if (fileHandle < 0)
            return fileHandle;
    }

    vm_set_var(a1 + 92, fileHandle);
    vm_cbfs_vm_file_seek(fileHandle, fileSeekPos, 0);

    int size = vm_DF_File_ReadInt(fileHandle);
    printf("DF_DataPackage_LoadFormTCard2:%d\n", size);

    int bufferPtr = 0;
    if (1)
    {
        u32 ptr = vm_malloc_var();
        vm_set_var(bufferPtr, 0);
        vm_DF_Malloc_IN(ptr, size); //&bufferPtr
        bufferPtr = vm_get_var(ptr);
        vm_free(ptr);
    }
    vm_DF_File_ReadToBuffer(fileHandle, bufferPtr, size);

    u32 offset = 0;
    int minus1 = -1;

    if (size > 4)
    {
        u32 ptr = vm_malloc_var();
        vm_set_var(ptr, offset);
        minus1 = vm_DF_ReadInt(bufferPtr, ptr); // ptr = &offset
        offset = vm_get_var(ptr);
        vm_free_var(ptr);
    }
    vm_set_var(a1 + 104, minus1);

    if (1)
    {
        u32 ptr = vm_malloc_var();
        vm_set_var(ptr, offset); // ptr = &offset
        count = (u16)vm_DF_ReadInt(bufferPtr, offset);
        offset = vm_get_var(ptr);
        vm_free_var(ptr);
    }

    // printf("DF_DataPackage_LoadFormTCard3333:%x,%x\n", count, minus1);
    while (i < count)
    {
        if (i)
        {
            u32 ptr = vm_malloc_var();
            u32 ptr2 = vm_malloc_var();
            vm_set_var(ptr, namePtr); // ptr = &namePtr
            vm_set_var(ptr2, offset); // ptr2 = &offset
            vm_DF_ReadStringEx(ptr, bufferPtr, ptr2);
            namePtr = vm_get_var(ptr);
            offset = vm_get_var(ptr2);

            int new_off = vm_DF_ReadInt(bufferPtr, ptr2);
            offset = vm_get_var(ptr2);
            vm_cbfs_vm_file_seek(fileHandle, new_off + fileSeekPos, 0);
            vm_free_var(ptr);
            vm_free_var(ptr2);
        }

        int entry = vm_DF_DataPackage_LocateDataPackage(a1, namePtr);

        if (entry && !vm_get_var_byte(entry))
        {

            if (1)
            {
                u32 pfBuffer = 0;
                // entry->fileHandle = handle
                vm_set_var(entry + 92, fileHandle);

                int idx = ((u32)vm_get_var_byte(entry + 1)) << 8;

                int block_size = vm_DF_File_ReadInt(fileHandle);

                if (1)
                {
                    u32 ptr = vm_malloc_var();
                    vm_set_var(ptr, pfBuffer);
                    vm_DF_Malloc_IN(ptr, block_size); // ptr = &bufferPtr2
                    pfBuffer = vm_get_var(ptr);
                    vm_free_var(ptr);
                }

                vm_DF_File_ReadToBuffer(fileHandle, pfBuffer, block_size);

                offset = 0;

                int arr_cnt = 0, data_size = 0;
                if (1)
                {
                    u32 ptr = vm_malloc_var();
                    vm_set_var(ptr, offset);
                    data_size = vm_DF_ReadInt(pfBuffer, ptr); //&offset
                    offset = vm_get_var(ptr);

                    arr_cnt = vm_DF_ReadInt(pfBuffer, ptr); //&offset
                    offset = vm_get_var(ptr);
                    vm_free_var(ptr);
                }

                vm_set_var_short(entry + 8, arr_cnt);
                int res_idxPtr = 0, res_stringPtr = 0, res_intPtr = 0;

                if (arr_cnt)
                {
                    pkg_id = vm_DF_DataPackage_GetPackageID(a1, namePtr);

                    if (pkg_id)
                    {
                        int base = vm_get_var(a1 + 28);

                        int item = vm_get_var(base + (pkg_id - 1) * 4);

                        vm_DF_Malloc_IN(item + 12, 4 * arr_cnt);
                        res_idxPtr = vm_get_var(item + 12);

                        vm_DF_Malloc_IN(item + 16, 4 * arr_cnt);
                        res_stringPtr = vm_get_var(item + 16);

                        vm_DF_Malloc_IN(item + 20, 2 * arr_cnt);
                        res_intPtr = vm_get_var(item + 20);
                    }
                    else
                    {
                        vm_DF_Malloc_IN(a1 + 12, 4 * arr_cnt);
                        res_idxPtr = vm_get_var(a1 + 12);

                        vm_DF_Malloc_IN(a1 + 16, 4 * arr_cnt);
                        res_stringPtr = vm_get_var(a1 + 16);

                        vm_DF_Malloc_IN(a1 + 20, 2 * arr_cnt);
                        res_intPtr = vm_get_var(a1 + 20);
                    }

                    vm_set_var(entry + 16, res_idxPtr);    // 资源ID
                    vm_set_var(entry + 12, res_stringPtr); // 资源ID
                    vm_set_var(entry + 20, res_intPtr);    // 资源排列序号

                    if (1)
                    {
                        u32 ptr = vm_malloc_var(4);
                        for (int k = 0; k < arr_cnt; k++)
                        {
                            vm_set_var(ptr, offset);
                            int val = vm_DF_ReadInt(pfBuffer, ptr);
                            offset = vm_get_var(ptr);
                            vm_set_var(res_idxPtr + 4 * k, val);
                        }

                        for (int j = 0; j < arr_cnt; j++)
                        {
                            vm_set_var(ptr, offset);
                            vm_DF_ReadStringEx((int *)(res_stringPtr + 4 * j), pfBuffer, ptr);
                            offset = vm_get_var(ptr);

                            vm_set_var_short(res_intPtr + 2 * j, idx++);
                        }

                        vm_free_var(ptr);
                    }
                }
                // DF
                if (1)
                {
                    u32 ptr = vm_malloc_var();
                    vm_set_var(ptr, pfBuffer);
                    vm_DF_Free(ptr);
                    pfBuffer = vm_get_var(ptr);
                    vm_free(ptr);
                }

                if (data_size)
                {
                    int tell = vm_cbfs_vm_file_tell(fileHandle);

                    if (pkg_id)
                    {
                        int base = vm_get_var(a1 + 28);
                        int item = vm_get_var(base + (pkg_id - 1) * 4);
                        vm_set_var_byte(item + 84, 1);
                        vm_set_var(item + 88, tell);
                        vm_set_var(item + 92, fileHandle);
                        vm_set_var(item + 96, data_size);
                    }
                    else
                    {
                        vm_set_var_byte(a1 + 84, 1);
                        vm_set_var(a1 + 88, tell);
                        vm_set_var(a1 + 92, fileHandle);
                        vm_set_var(a1 + 96, data_size);
                    }
                }

                vm_set_var_byte(entry, 1);
            }
        }

        if (namePtr)
        {
            u32 ptr = vm_malloc_var();
            vm_set_var(ptr, namePtr);
            vm_DF_Free(ptr); //&namePtr
            namePtr = 0;
            vm_free_var(ptr);
        }

        i++;
    }

    // vm_free(bufferPtr);

    int txt_id;
    uc_mem_read(MTK, a1 + 104, &txt_id, 4);
    // vm_free(offsetPtr);

    if (txt_id != -1)
        return vm_DF_DataPackage_InitTxt(a1, txt_id);

    return 0;
}

// ok
int vm_DF_File_ReadInt(int fileHandle)
{
    u8 bufferV[4];
    u32 bufferPtr = vm_malloc(4);
    vm_cbfs_vm_file_read(bufferPtr, 4, fileHandle);
    uc_mem_read(MTK, bufferPtr, &bufferV, 4);
    u32 r = bufferV[0] | (bufferV[1] << 8) | (bufferV[2] << 16) | (bufferV[3] << 24);
    vm_free(bufferPtr);
    return vm_set_call_result(r);
}
// ok
int vm_DF_File_ReadToBuffer(int fileHandle, int bufferPtr, int size)
{
    return vm_cbfs_vm_file_read(bufferPtr, size, fileHandle);
}

int vm_DF_DataPackage_LoadFormTCard(int a1)
{
    int offset;
    uc_mem_read(MTK, VM_DF_DataPackage_In_File_Offset_ADDRESS, &offset, 4);
    return vm_DF_DataPackage_LoadFormTCardEx(a1, VM_DF_DataPackage_FilePath_ADDRESS, offset);
}

int VM_DF_DataPackage_DoLoading(int a1, int a2, int a3)
{
    u8 n2;

    if (a3)
    {
        uc_mem_read(MTK, VM_DF_DataPackage_LoadType_ADDRESS, &n2, 1);

        if (n2 == 1)
        {
            // TF卡加载
            return vm_DF_DataPackage_LoadFormTCard(a1);
        }

        if (n2 == 2)
        {
            uc_mem_read(MTK, VM_DF_DataPackage_In_File_Offset_ADDRESS, &a2, 4);
        }
    }

    // 默认走资源加载
    return vm_DF_DataPackage_LoadFromTResource(a1, a2);
}

void vm_initDFDataPackage(u32 a1, u32 a2)
{
    u32 tmp2, tmp3, tmp4;
    tmp2 = 0;
    uc_mem_write(MTK, a1 + 4, &tmp2, 4);
    uc_mem_write(MTK, a1 + 8, &tmp2, 2);
    uc_mem_write(MTK, a1 + 10, &tmp2, 2);
    uc_mem_write(MTK, a1 + 12, &tmp2, 4);
    uc_mem_write(MTK, a1 + 16, &tmp2, 4);
    uc_mem_write(MTK, a1 + 24, &tmp2, 4);
    uc_mem_write(MTK, a1 + 28, &tmp2, 4);
    printf("vm_initDFDataPackage(%x,%x)\n", a1, a2); // debug
    //*a1 = 1
    tmp2 = 1;
    uc_mem_write(MTK, a1, &tmp2, 1);
    // DF_Malloc_IN((_DWORD *)(a1 + 28), 4 * a2);
    tmp4 = vm_malloc(a2 * 4);
    uc_mem_write(MTK, tmp4, emptyBuff, SDL_min(a2 * 4, 1024)); // memclr_w()
    uc_mem_write(MTK, a1 + 28, &tmp4, 4);
    tmp2 = (u32)-1;
    uc_mem_write(MTK, a1 + 92, &tmp2, 4);
    tmp2 = 0;
    uc_mem_write(MTK, a1 + 100, &tmp2, 4);
    tmp3 = VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS; // DF_DataPackage_LoadPackage
    uc_mem_write(MTK, a1 + 32, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_ReleasePackage
    uc_mem_write(MTK, a1 + 36, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_LoadFromTResource
    uc_mem_write(MTK, a1 + 40, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_LoadFormTCard
    uc_mem_write(MTK, a1 + 44, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_DoLoading
    uc_mem_write(MTK, a1 + 48, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_LocateDataPackage
    uc_mem_write(MTK, a1 + 52, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_GetFile
    uc_mem_write(MTK, a1 + 56, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_GetFileByID
    uc_mem_write(MTK, a1 + 60, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_GetFileNameByID
    uc_mem_write(MTK, a1 + 64, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_GetFileID
    uc_mem_write(MTK, a1 + 68, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_ShowFileList
    // return DF_DataPackage_ShowFileList
    u32 r = tmp3;
    uc_mem_write(MTK, a1 + 72, &tmp3, 4);
    tmp3 += 4; // DF_DataPackage_InitTxt
    uc_mem_write(MTK, a1 + 80, &tmp3, 4);
    return vm_set_call_result(r);
}

void vm_sprintf()
{
    int tmp1, tmp2, tmp3, tmp4, tmp5;
    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_mem_read(MTK, tmp1, globalSprintfBuff, 128);
    printf("[vm_sprintf]");
    tmp1 = 0;
    tmp2 = 0;
    tmp3 = 0;
    while (tmp1 < 128)
    {
        char c = globalSprintfBuff[tmp1++];
        if (c == '\0')
            break;
        else if (c == '%')
        {
            tmp3 = 1;
            continue;
        }
        else
        {
            if (tmp3 == 1)
            {
                if (c == 's')
                {
                    if (tmp2 < 3)
                    {
                        uc_reg_read(MTK, UC_ARM_REG_R1 + tmp2, &tmp5);
                    }
                    else
                    { // 从栈顶读取参数
                        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                        uc_mem_read(MTK, tmp4 + (tmp2 - 3) * 4, &tmp5, 4);
                    }
                    uc_mem_read(MTK, tmp5, sprintfBuff, 128);
                    printf(sprintfBuff);
                }
                else
                {
                    if (tmp2 < 3)
                    {
                        uc_reg_read(MTK, UC_ARM_REG_R1 + tmp2, &tmp5);
                    }
                    else
                    { // 从栈顶读取参数
                        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                        uc_mem_read(MTK, tmp4 + (tmp2 - 3) * 4, &tmp5, 4);
                    }
                    printf("%x", tmp5);
                }
                tmp3 = 0;
                tmp2++;
            }
            else
            {
                printf("%c", c);
            }
        }
    }
    printf("\n");
}

void vm_sprintf_return_buffer()
{
    int i, paramPos, needConvert, tmp4, currParam, inBufferPtr, strCount, strCnt2;
    uc_reg_read(MTK, UC_ARM_REG_R0, &inBufferPtr);
    vm_readStringByReg(UC_ARM_REG_R1, cbeTextString);
    strCount = strlen(cbeTextString);
    i = 0;
    paramPos = 0;
    needConvert = 0;
    u32 bufferPtr = inBufferPtr;
    while (1)
    {
        u8 c = cbeTextString[i++];
        if (c == 0)
        {
            uc_mem_write(MTK, inBufferPtr, &c, 1);
            break;
        }
        else if (c == '%')
        {
            needConvert = 1;
            continue;
        }
        else
        {
            if (needConvert)
            {
                if (paramPos < 2)
                {
                    uc_reg_read(MTK, UC_ARM_REG_R2 + paramPos, &currParam);
                }
                else
                { // 从栈顶读取参数
                    uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                    uc_mem_read(MTK, tmp4 + (paramPos - 2) * 4, &currParam, 4);
                }

                if (c == 's')
                {
                    vm_readStringByPtr(currParam, sprintfBuff);
                    strCnt2 = strlen(sprintfBuff);
                    uc_mem_write(MTK, inBufferPtr, sprintfBuff, strCnt2);
                    inBufferPtr += strCnt2;
                }
                else if (c == 'x')
                {
                    sprintf(sprintfBuff, "%x", currParam);
                    strCnt2 = strlen(sprintfBuff);
                    uc_mem_write(MTK, inBufferPtr, sprintfBuff, strCnt2);
                    inBufferPtr += strCnt2;
                }
                else if (c == 'd')
                {
                    sprintf(sprintfBuff, "%d", currParam);
                    strCnt2 = strlen(sprintfBuff);
                    uc_mem_write(MTK, inBufferPtr, sprintfBuff, strCnt2);
                    inBufferPtr += strCnt2;
                }
                else
                {
                    printf("未处理的格式化：%%%c\n", c);
                    assert(0);
                }
                needConvert = 0;
                paramPos++;
            }
            else
            {
                uc_mem_write(MTK, inBufferPtr++, &c, 1);
            }
        }
    }
    // vm_readStringByPtr(bufferPtr, cbeTextString);
    // printf("vm_sprintf(%s)(%d)\n", cbeTextString, strCount);
    // printf("\n");
}

// fixme 128溢出风险
void vm_DF_GetFormatString()
{
    int tmp1, tmp2, tmp3, tmp4, tmp5;
    char cacheBuff[128];
    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_mem_read(MTK, tmp1, globalSprintfBuff, 128);
    my_memset(sprintfBuff, 0, 128);
    printf("[vm_sprintf]");
    char *ptr = sprintfBuff;
    tmp1 = 0;
    tmp2 = 0;
    tmp3 = 0;
    while (tmp1 < 128)
    {
        char c = globalSprintfBuff[tmp1++];
        if (c == '\0')
            break;
        else if (c == '%')
        {
            tmp3 = 1;
            continue;
        }
        else
        {
            if (tmp3 == 1)
            {
                if (c == 's')
                {
                    if (tmp2 < 3)
                    {
                        uc_reg_read(MTK, UC_ARM_REG_R1 + tmp2, &tmp5);
                    }
                    else
                    { // 从栈顶读取参数
                        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                        uc_mem_read(MTK, tmp4 + (tmp2 - 3) * 4, &tmp5, 4);
                    }
                    uc_mem_read(MTK, tmp5, cacheBuff, 128);
                    strcpy(ptr, cacheBuff);
                    ptr += strlen(cacheBuff);
                }
                else
                {
                    if (tmp2 < 3)
                    {
                        uc_reg_read(MTK, UC_ARM_REG_R1 + tmp2, &tmp5);
                    }
                    else
                    { // 从栈顶读取参数
                        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                        uc_mem_read(MTK, tmp4 + (tmp2 - 3) * 4, &tmp5, 4);
                    }
                    sprintf(cacheBuff, "%x", tmp5);
                    strcpy(ptr, cacheBuff);
                    ptr += strlen(cacheBuff);
                }
                tmp3 = 0;
                tmp2++;
            }
            else
            {
                *ptr++ = c;
            }
        }
    }
    printf("[vm_DF_GetFormatString]%s\n", sprintfBuff);
    uc_mem_write(MTK, VM_DF_GetFormatStringBuffer_ADDRESS, sprintfBuff, 128);
}
// ok
void vm_DF_GetMemoryBlock()
{
    u32 tmp1;
    uc_mem_read(MTK, VM_DreamFactory_MemoryBlock_ADDRESS, &tmp1, 4);
    return vm_set_call_result(tmp1);
}
// ok
void vm_initMemoryBlock(p_g_memoryBlock, size)
{
    int tmp1, tmp2, tmp3, tmp4;
    // p_g_memoryBlock[0] = 新的内存块地址
    tmp1 = VM_MF_MemoryBlock_POOL_ADDRESS;
    uc_mem_write(MTK, p_g_memoryBlock, &tmp1, 4);
    // p_g_memoryBlock[1] = offset
    tmp1 = 0;
    uc_mem_write(MTK, p_g_memoryBlock + 4, &tmp1, 4);
    // p_g_memoryBlock[2] = size
    uc_mem_write(MTK, p_g_memoryBlock + 8, &size, 4);
    // p_g_memoryBlock[3] = MF_MemoryBlock_Malloc;
    // p_g_memoryBlock[4] = MF_MemoryBlock_Reset;
    // p_g_memoryBlock[5] = MF_MemoryBlock_Release;
    for (tmp3 = 3; tmp3 < 6; tmp3++)
    {
        tmp4 = VM_MF_MemoryBlock_FUNC_LIST_ADDRESS + (tmp3 - 3) * 4;
        uc_mem_write(MTK, p_g_memoryBlock + tmp3 * 4, &tmp4, 4);
    }
}
// ok
// a2允许为0
u32 vm_MF_MemoryBlock_Malloc(int a1, int a2)
{
    u32 baseAddr, offset, size, oldOffset;
    // mpcmdFreeShareMemIn
    uc_mem_read(MTK, a1, &baseAddr, 4);      //->base
    uc_mem_read(MTK, a1 + 4, &oldOffset, 4); //->offset
    uc_mem_read(MTK, a1 + 8, &size, 4);      //->offset
    offset = oldOffset + a2;
    if (offset > size)
    {
        printf("申请内存(%x)超过可用值(%x)\n", a2, size - oldOffset);
        dumpCpuInfo();
        assert(0);
    }
    uc_mem_write(MTK, a1 + 4, &offset, 4);
    oldOffset = baseAddr + offset;
    return vm_set_call_result(oldOffset);
    // printf("[call]MF_MemoryBlock_Malloc(%x,%x)(%x)\n", baseAddr, a2, oldOffset);
}

void vm_MF_MemoryBlock_Reset(int a1)
{
    int tmp;
    int size;
    u32 v;
    uc_mem_read(MTK, a1, &tmp, 4);
    uc_mem_read(MTK, a1 + 8, &size, 4);

    for (int i = 0; i < size; i++)
    {
        v = 0;
        uc_mem_write(MTK, tmp + i, &v, 1);
    }

    v = 0;
    uc_mem_write(MTK, a1 + 4, &v, 4); // offset重置为0
    return vm_set_call_result(v);
}
void vm_MF_MemoryBlock_Release(int a1)
{
    u32 v = 0;
    uc_reg_read(MTK, UC_ARM_REG_R0, &a1);
    // Game_Image_free(a1) = mpcmdFreeShareMemIn(a1)
    vm_free(a1);
    return vm_set_call_result(v);
}
u32 vm_DF_DataPackage_GetFileID(u32 a1, u32 namePtr)
{
    int16_t count1 = 0;
    int16_t count2 = 0;
    int16_t i = 0;
    int16_t j = 0;
    u32 base_ptr = 0;
    u32 str_ptr = 0;
    u32 id_base = 0;
    u32 child_base = 0;
    u32 v7 = 0;
    int result = -1;
    uc_engine *uc = MTK;
    // count1 = *(int16 *)(a1 + 8)
    uc_mem_read(uc, a1 + 8, &count1, 2); // 应该是0x112

    // base_ptr = *(uint32 *)(a1 + 12)
    uc_mem_read(uc, a1 + 12, &base_ptr, 4);

    // id_base = *(uint32 *)(a1 + 20)
    uc_mem_read(uc, a1 + 20, &id_base, 4);

    for (i = 0; i < count1; i++)
    {
        // str_ptr = *(uint32 *)(base_ptr + 4 * i)
        uc_mem_read(uc, base_ptr + 4 * i, &str_ptr, 4);
        // uc_mem_read(MTK,str_ptr,globalSprintfBuff,128);

        if (vm_DF_String_Equal(str_ptr, namePtr))
        {
            u32 file_id = 0; // 第一次应该返回0x1c
            uc_mem_read(uc, id_base + 2 * i, &file_id, 2);
            uc_reg_write(uc, UC_ARM_REG_R0, &file_id);
            return file_id;
        }
    }

    // count2 = *(int16 *)(a1 + 10)
    uc_mem_read(uc, a1 + 10, &count2, 2);

    // child_base = *(uint32 *)(a1 + 28)
    uc_mem_read(uc, a1 + 28, &child_base, 4);

    for (j = 0; j < count2; j++)
    {
        // v7 = *(uint32 *)(child_base + 4 * j)
        uc_mem_read(uc, child_base + 4 * j, &v7, 4);

        if (v7)
        {
            result = vm_DF_DataPackage_GetFileID(v7, namePtr);
            if (result >= 0)
            {
                uc_reg_write(uc, UC_ARM_REG_R0, &result);
                return result;
            }
        }
    }
    result = -1;
    uc_reg_write(uc, UC_ARM_REG_R0, &result);
    return result;
}

int vm_DF_DataPackage_DoReadData(int32_t a1, int32_t a2)
{
    int16_t count = 0;
    int32_t file_handle = 0;
    int32_t data_base = 0;
    int32_t base_offset = 0;
    u32 end_offset = 0;
    u32 v4 = 0;
    int32_t v5 = 0;
    int32_t v6 = 0;
    int len = 0;
    u32 buffer = 0;
    uc_engine *uc = MTK;
    // count = *(int16 *)(a1 + 8)
    // uc_mem_read(uc, a1, cbeTextString, 64);
    // printf("vm_DF_DataPackage_DoReadData(%x,%x)\n", a1, a2);
    uc_mem_read(uc, a1 + 8, &count, 2);
    if (count <= a2)
    {
        printf("codeLib\\DF_DataPackage.c");
        assert(0);
    }

    // file_handle = *(uint32 *)(a1 + 92)
    uc_mem_read(uc, a1 + 92, &file_handle, 4);

    // data_base = *(uint32 *)(a1 + 16)
    uc_mem_read(uc, a1 + 16, &data_base, 4); // 0x016f47ac

    // base_offset = *(uint32 *)(a1 + 88)
    uc_mem_read(uc, a1 + 88, &base_offset, 4); // 0x2a3ef 为什么这里这里与真机差0x100，应该是0x2a3ef，调试时得到0x2a2ef

    // v5 = *(uint32 *)(data_base + 4 * a2)
    uc_mem_read(uc, data_base + 4 * a2, &v5, 4); // v5 = 0x2ec9d

    // seek
    vm_cbfs_vm_file_seek(file_handle, v5 + base_offset, 0);

    if ((count - 1) > (int16_t)a2)
    {
        // v6 = *(uint32 *)(data_base + 4 * a2 + 4)
        uc_mem_read(uc, data_base + 4 * a2 + 4, &v6, 4);
    }
    else
    {
        // v6 = *(uint32 *)(a1 + 96)
        uc_mem_read(uc, a1 + 96, &v6, 4);
    }

    len = v6 - v5;
    //    buffer = vmGetDataBuffer(len);

    buffer = vm_malloc(len);
    vm_cbfs_vm_file_read(buffer, len, file_handle);
    // printf("DF_Package_DoReadData(adr:%x,len:0x%x)\n", buffer, len);
    return vm_set_call_result(buffer);
}

// ok
int vm_DF_DataPackage_GetFileByID(u32 a1, u32 fileId)
{
    int16_t i = 0;
    int16_t j = 0;
    int16_t count1 = 0;
    int16_t count2 = 0;
    u32 id_base = 0;
    u32 data_base = 0;
    u32 offset_base = 0;
    u32 child_base = 0;
    u32 v7 = 0;
    int16_t file_id = 0;
    u8 flag = 0;
    u32 data_ptr = 0;
    u32 offset = 0;
    int result = 0;
    uc_engine *uc = MTK;
    uc_mem_read(uc, a1 + 8, &count1, 2);
    uc_mem_read(uc, a1 + 20, &id_base, 4);
    uc_mem_read(uc, a1 + 16, &data_base, 4);
    uc_mem_read(uc, a1 + 24, &offset_base, 4);

    for (i = 0; i < count1; i++)
    {
        uc_mem_read(uc, id_base + i * 2, &file_id, 2);

        if (file_id == (int16_t)fileId)
        {
            uc_mem_read(uc, a1 + 84, &flag, 1);

            // printf("DF_DataPackage_GetFileByID:%d\n", flag);

            if (flag)
            {
                return vm_DF_DataPackage_DoReadData(a1, i);
            }
            else
            {
                uc_mem_read(uc, data_base + i * 4, &data_ptr, 4);
                offset = offset_base;
                offset = data_ptr + offset;
                return vm_set_call_result(offset);
            }
        }
    }

    uc_mem_read(uc, a1 + 10, &count2, 2);
    uc_mem_read(uc, a1 + 28, &child_base, 4);

    for (j = 0; j < count2; j++)
    {
        uc_mem_read(uc, child_base + j * 4, &v7, 4);

        if (v7)
        {
            result = vm_DF_DataPackage_GetFileByID(v7, fileId);
            if (result)
            {
                return vm_set_call_result(result);
            }
        }
    }
    return vm_set_call_result(0);
}

int vm_DF_DataPackage_GetFile(int a1, int namePtr)
{
    // vm_readStringByPtr(a2, cbeTextString);
    int FileID = vm_DF_DataPackage_GetFileID(a1, namePtr);
    if (FileID < 0)
    {
        gbk_to_utf8(cbeTextString, sprintfBuff, mySizeOf(sprintfBuff));
        printf("[读取包内文件失败]%s\n", sprintfBuff);
        printf("[文件ID]%d\n", FileID);
        dumpCpuInfo();
        assert(0);
    }
    return vm_DF_DataPackage_GetFileByID(a1, FileID);
}

// ok
void vm_DF_GetResourceIDByFileName(int namePtr)
{
    u32 tmp1;
    // uc_mem_read(MTK, namePtr, globalSprintfBuff, 128);
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
    return vm_DF_DataPackage_GetFileID(tmp1, namePtr);
}
int vm_DF_GetResourceByFileName(int namePtr)
{
    int DreamFactory_DataPackage;
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &DreamFactory_DataPackage, 4);
    if (DreamFactory_DataPackage)
        return vm_DF_DataPackage_GetFile(DreamFactory_DataPackage, namePtr);
    else
    {
        return vm_set_call_result(0);
    }
}

int vm_DF_GetTResource(int a1)
{
    u32 DreamFactoryResourceBuffer; // r0
    // todo DF_FactoryCharB_Init();
    DreamFactoryResourceBuffer = vm_DF_GetResourceByFileName(a1);
    uc_mem_write(MTK, VM_DreamFactoryResourceBuffer_ADDRESS, &DreamFactoryResourceBuffer, 4);
    return DreamFactoryResourceBuffer;
}
int vm_IMG_Destory(u32 a1)
{
    u32 n3 = 0;
    u32 ptr = 0;
    u32 zero = 0;
    uc_engine *uc = MTK;
    // n3 = *(uint8 *)(a1 + 8)
    uc_mem_read(uc, a1 + 8, &n3, 1);

    if (n3 == 1 || n3 == 3 || n3 == 4)
    {
        // ptr = *(uint32 *)a1
        uc_mem_read(uc, a1, &ptr, 4);

        if (ptr)
        {
            vm_free(ptr);
        }

        // *(uint32 *)a1 = 0
        uc_mem_write(uc, a1, &zero, 4);

        if (!a1)
        {
            printf("IMG_Destory释放失败");
            assert(0);
        }

        n3 = 0;

        // 清空结构
        uc_mem_write(uc, a1, &zero, 4);
        uc_mem_write(uc, a1 + 4, &zero, 4);
        uc_mem_write(uc, a1 + 8, &zero, 4);
    }
    return vm_set_call_result(n3);
}
// dst是屏幕内存，所以不需要关注，只需要关注srcInfo
/*
typedef struct {
    u32 pixels;   // base pointer (address)
    u16 width;    // at offset +4
    u16 height;   // at offset +6
} ImageHeader;
*/
void vM_DrawImageWithClipEx()
{
    int srcInfo, srcX, srcY, w, h, dstX, dstY, sp, dstPtr, srcPtr;
    u16 src_w, src_h;
    uc_reg_read(MTK, UC_ARM_REG_R1, &srcInfo);
    uc_reg_read(MTK, UC_ARM_REG_R2, &srcX);
    uc_reg_read(MTK, UC_ARM_REG_R3, &srcY);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_mem_read(MTK, sp, &w, 4);
    uc_mem_read(MTK, sp + 4, &h, 4);
    uc_mem_read(MTK, sp + 8, &dstX, 4);
    uc_mem_read(MTK, sp + 12, &dstY, 4);
    uc_mem_read(MTK, srcInfo, &srcPtr, 4);
    uc_mem_read(MTK, srcInfo + 4, &src_w, 2);
    uc_mem_read(MTK, srcInfo + 6, &src_h, 2);
    // u8 buffer[188];
    // uc_mem_read(MTK, srcPtr, buffer, 188);
    // printf("vM_DrawImageWithClipEx(srcPtr:%x,srcX:%d,srcY:%d,clipW:%d,clipH:%d,dstX:%d,dstY:%d)\n", srcPtr, srcX, srcY, w, h, dstX, dstY);
    // todo 裁剪边界检查
    if (w > src_w)
        w = src_w;
    if (h > src_h)
        h = src_h;
    if (srcY + h > src_h)
        h = src_h - h;
    if (srcX + w > src_w)
        w = src_w - w;

    int srcImgPitch = (((4 - src_w) & 3) + src_w) * 2; // 源图片的宽度按4像素对齐后再x2就是原图片一行像素占用的字节数
    int dstImgPicth = 2 * LCD_WIDTH;
    for (int i = 0; i < h; i++)
    {
        int srcOffset = (i + srcY) * srcImgPitch + srcX * 2;
        u32 dstScreenOffset = (i + dstY) * dstImgPicth + dstX * 2;

        uc_err r = uc_mem_read(MTK, srcPtr + srcOffset, Lcd_Cache_Buffer + dstScreenOffset, srcImgPitch);
        if (r != UC_ERR_OK)
        {
            printf("读取内存错误");
            assert(0);
        }
        uc_mem_write(MTK, VM_screenImage_ADDRESS + dstScreenOffset, Lcd_Cache_Buffer + dstScreenOffset, srcImgPitch);
    }
    return 0;
}
u16 blend565(u16 dst, u16 src, u8 a)
{
    if (a == 255)
        return src;
    if (a == 0)
        return dst;

    // 按反汇编里相同的位操作做混合（避免逐通道处理的除法）
    u32 dst_packed = (((dst & 0x07E0u) << 16) | (dst & 0xF81Fu));
    u32 src_packed = (((src & 0x07E0u) << 16) | (src & 0xF81Fu));

    u32 inv = 255u - (u32)a;
    u32 comb = inv * dst_packed + (u32)a * src_packed;

    u16 g = (u16)((comb >> 21) & 0x07E0u);
    u16 rb = (u16)((comb >> 5) & 0xF81Fu);
    return (u16)(g | rb);
}
void vm_vMDrawImageClipAndAlphaEx()
{
    int srcInfo, srcX, srcY, w, h, dstX, dstY, sp, dstPtr, srcPtr;
    u16 src_w, src_h;
    uc_reg_read(MTK, UC_ARM_REG_R1, &srcInfo);
    uc_reg_read(MTK, UC_ARM_REG_R2, &srcX);
    uc_reg_read(MTK, UC_ARM_REG_R3, &srcY);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    uc_mem_read(MTK, sp, &w, 4);
    uc_mem_read(MTK, sp + 4, &h, 4);
    uc_mem_read(MTK, sp + 8, &dstX, 4);
    uc_mem_read(MTK, sp + 12, &dstY, 4);
    uc_mem_read(MTK, srcInfo, &srcPtr, 4);
    uc_mem_read(MTK, srcInfo + 4, &src_w, 2);
    uc_mem_read(MTK, srcInfo + 6, &src_h, 2);
    u8 tmpBuffer[480];
    // printf("vMDrawImageClipAndAlphaEx(sx:%d,sy:%d,w:%d,h:%d,dx:%d,dy:%d)\n", srcX, srcY, w, h, dstX, dstY);
    // todo 裁剪边界检查
    if (w > src_w)
        w = src_w;
    if (h > src_h)
        h = src_h;
    if (srcY + h > src_h)
        h = src_h - h;
    if (srcX + w > src_w)
        w = src_w - w;

    int srcImgPitch = (((4 - src_w) & 3) + src_w) * 2; // 源图片的宽度按4像素对齐后再x2就是原图片一行像素占用的字节数
    int dstImgPicth = 2 * LCD_WIDTH;
    int special_alpha_map = 0;

    // 下面先写“通用”行为（没有单独 alpha map）：像素值为 0 表示透明
    for (int row = 0; row < h; ++row)
    {
        int srcOffset = (row + srcY) * srcImgPitch + srcX * 2;
        u32 dstScreenOffset = (row + dstY) * dstImgPicth + dstX * 2;
        uc_err r = uc_mem_read(MTK, srcPtr + srcOffset, tmpBuffer, srcImgPitch);
        if (r != UC_ERR_OK)
        {
            printf("读取内存错误");
            assert(0);
        }
        u16 *dstBasePtr = ((u16 *)(Lcd_Cache_Buffer + dstScreenOffset));
        if (!special_alpha_map)
        {
            for (u16 col = 0; col < w; ++col)
            {
                u16 color = ((u16 *)tmpBuffer)[col];
                if (color)
                    dstBasePtr[col] = color; // 非 0 即完全不透明，覆盖
            }
        }
        else
        {
            // todo
            //  u8 *a_ptr = alpha_map_base + sy * src_pw + sx;
            for (u16 col = 0; col < w; ++col)
            {
                u8 alphaByte = tmpBuffer[col]; // alpha byte
                u16 color = ((u16 *)tmpBuffer)[col];
                if (alphaByte == 0xff)
                {
                    dstBasePtr[col] = color;
                }
                else if (alphaByte == 0)
                {
                    // nothing
                }
                else
                {
                    dstBasePtr[col] = blend565(dstBasePtr[col], color, alphaByte);
                }
            }
        }
        uc_mem_write(MTK, VM_screenImage_ADDRESS + dstScreenOffset, Lcd_Cache_Buffer + dstScreenOffset, srcImgPitch);
    }
}

int vm_IMG_CreateImageFormStream(u32 a1, u32 a2)
{
    u32 v3 = a2;
    u8 n3 = 0;
    u8 b1 = 0, b2 = 0, b3 = 0, b4 = 0;
    u16 v5 = 0;
    u16 v6 = 0;
    u32 tmp32 = 0;
    u8 tmp8 = 0;
    uc_engine *uc = MTK;

    if (!v3)
    {
        uc_mem_read(MTK, VM_DreamFactory_MemoryBlock_ADDRESS, &tmp32, 4);
        v3 = vm_MF_MemoryBlock_Malloc(tmp32, 12);
    }

    // n3 = *a1
    uc_mem_read(uc, a1, &n3, 1);
    uc_mem_read(uc, a1, cbeTextString, 32); // debug

    if (n3)
    {
        if (n3 == 1)
        {
            vm_gifDecode(a1 + 1, v3);
        }
        else if (n3 == 3)
        {
            vm_pngDecodeStart(a1 + 1, v3);
        }
        else
        {
            printf("IMG_CreateImageFormRes:%d,%d,%d", n3, b1, b2);
            assert(0);
        }
    }
    else
    {
        uc_mem_read(uc, a1 + 1, &b1, 1);
        uc_mem_read(uc, a1 + 2, &b2, 1);
        uc_mem_read(uc, a1 + 3, &b3, 1);
        uc_mem_read(uc, a1 + 4, &b4, 1);

        v5 = (b1 << 8) | b2;
        v6 = (b3 << 8) | b4;

        // *(uint32 *)v3 = a1 + 8
        tmp32 = a1 + 8;
        uc_mem_write(uc, v3, &tmp32, 4);

        // *(uint16 *)(v3 + 4) = v5
        uc_mem_write(uc, v3 + 4, &v5, 2);

        // *(uint16 *)(v3 + 6) = v6
        uc_mem_write(uc, v3 + 6, &v6, 2);

        // *(uint8 *)(v3 + 8) = 0
        tmp8 = 0;
        uc_mem_write(uc, v3 + 8, &tmp8, 1);
    }
    return vm_set_call_result(v3);
}
int vm_gifDecode(int gifBufferPtr, int resultPtr)
{
    char buffer[1024 * 64]; // 由于不知道图片大小，预先读取64kb
    char ret[32];
    GifOut p;
    uc_mem_read(MTK, gifBufferPtr, buffer, mySizeOf(buffer));
    int retSize = sizeof(vm_img_result);
    gifDecodeExt(buffer, &p, 1);
    int vmPtr = vm_malloc(p.mallocSize);
    // for (int i = 0; i < p.height; i++)
    // {
    //     u32 srcOffset = p.width * 2 * i;
    //     u32 dstOffset = 240 * 2 * i;
    //     u32 picth = p.width * 2;
    //     my_memcpy((char*)Lcd_Cache_Buffer + dstOffset, p.pixels + srcOffset, picth);
    // }
    // UpdateLcd();
    uc_mem_write(MTK, vmPtr, p.pixels, p.mallocSize);
    vm_img_result *vr = (vm_img_result *)ret;
    vr->height = p.height;
    vr->pixelsPtr = vmPtr;
    vr->width = p.width;
    vr->need_free = p.allocated;
    SDL_free(p.pixels);
    uc_mem_write(MTK, resultPtr, ret, retSize);
    return vm_set_call_result(resultPtr);
}

int vm_pngDecodeStart(int pngBufferPtr, int resultPtr)
{
    char buffer[102400]; // 由于不知道图片大小，预先读取100kb
    char ret[32];
    uc_mem_read(MTK, pngBufferPtr, buffer, mySizeOf(buffer));
    uc_mem_write(MTK, resultPtr, ret, 12);
    return vm_set_call_result(resultPtr);
}

void vm_readStringByReg(uc_arm_reg reg, u8 *dst)
{
    int ptr;
    u8 tmp;
    uc_reg_read(MTK, reg, &ptr);
    while (true)
    {
        uc_mem_read(MTK, ptr, &tmp, 1);
        if (tmp == 0)
            break;
        *dst++ = tmp;
        ptr += 1;
    }
    *dst = 0;
}
void vm_readStringByPtr(u32 ptr, u8 *dst)
{
    u8 tmp;
    while (true)
    {
        uc_mem_read(MTK, ptr, &tmp, 1);
        if (tmp == 0)
            break;
        *dst++ = tmp;
        ptr += 1;
    }
    *dst = 0;
}

void vm_readStringGbkByReg(uc_arm_reg reg, u8 *dst2)
{
    u32 ptr;
    u8 tmp;
    u8 b2;
    u8 *dst = dst2;
    uc_reg_read(MTK, reg, &ptr);
    while (1)
    {
        uc_mem_read(MTK, ptr, dst2, 64);
        break;
        if (tmp < 0x80)
        {
            *dst++ = tmp;
            ptr += 1;
            if (tmp == 0)
                break;
        }
        else if (tmp >= 0x81 && tmp <= 0xfe)
        {
            *dst++ = tmp;
            uc_mem_read(MTK, ptr + 1, &b2, 1);
            *dst++ = b2;
            ptr += 2;
        }
        else
        {
            printf("[1]读取gbk字符串异常\n");

            break;
        }
    }
}
void vm_readStringUCS2ByReg(uc_arm_reg reg, u16 *dst)
{
    int ptr;
    u16 tmp;
    uc_reg_read(MTK, reg, &ptr);
    while (true)
    {
        uc_mem_read(MTK, ptr, &tmp, 2);
        if (tmp == 0)
            break;
        *dst++ = tmp;
        ptr += 2;
    }
    *dst = 0;
}
void vm_initManagerTable()
{
    u32 i = 0;
    u32 tmp;
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_SYS_MANAGER_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_SYS_MANAGER_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MEMORY_MANAGER_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_GAMEOLD_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_LCD_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_LCD_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_FILEIO_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_STDIO_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_STDIO_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_TIMER_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_TIMER_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_CTRL_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_CTRL_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_NETWORK_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_GAME_UTIL_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_DF_ENGINE_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_BILLING_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_BILLING_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_UCS2_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_UCS2_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_SCREEN_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_DF_SCRIPT_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_DF_SCRIPT_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_GAME_LCD_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_NETAPP_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_AUDIO_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_SENSOR_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_MANAGER_VMIM_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_VMIM_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = VM_APPSTORE_FUNC_LIST_ADDRESS + i * 4;
        uc_mem_write(MTK, VM_MANAGER_APPSTORE_TABLE_ADDRESS + i * 4, &tmp, 4);
    }
}
u32 vm_DF_GetDataPackage()
{
    u32 tmp1;
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
    return vm_set_call_result(tmp1);
}
void vm_IMG_CreateImageFormRes(u32 a1)
{
    u32 tmp1 = 0;
    u32 tmp2;
    u32 DataPackage = vm_DF_GetDataPackage();
    u32 Data;
    if (DataPackage)
    {
        uc_mem_read(MTK, DataPackage + 84, &tmp2, 1);
        if (tmp2)
        {
            Data = vm_DF_DataPackage_DoReadData(DataPackage, a1);
        }
        else
        {
            // Data = (*(_DWORD *)(DataPackage[4] + 4 * a1) + DataPackage[6]);
            // todo
            uc_mem_read(MTK, DataPackage + 16 + 4 * a1, &tmp1, 4);
            uc_mem_read(MTK, DataPackage + 6 * 4, &tmp2, 4);
            Data = tmp1 + tmp2;
        }
        return vm_IMG_CreateImageFormStream(Data, 0);
    }
    return vm_set_call_result(tmp1);
}
// ok
int vm_DF_ReadString(u32 a1, u32 a2)
{
    u32 idx = 0;
    u8 len = 0;
    u32 result = 0;
    u8 ch = 0;
    u32 i = 0;
    u8 zero = 0;
    uc_engine *uc = MTK;
    // idx = *a2
    // uc_mem_read(MTK, a1, cbeTextString, 64);//debug
    uc_mem_read(uc, a2, &idx, 4);

    // len = *(uint8 *)(a1 + idx)
    uc_mem_read(uc, a1 + idx, &len, 1);

    idx++;

    // *a2 = idx
    uc_mem_write(uc, a2, &idx, 4);
    uc_mem_read(MTK, VM_DreamFactory_MemoryBlock_ADDRESS, &result, 4);
    result = vm_MF_MemoryBlock_Malloc(result, len + 1);

    for (i = 0; i < len; i++)
    {
        // idx = *a2
        uc_mem_read(uc, a2, &idx, 4);

        // ch = *(uint8 *)(a1 + idx)
        uc_mem_read(uc, a1 + idx, &ch, 1);

        idx++;

        // *a2 = idx
        uc_mem_write(uc, a2, &idx, 4);

        // *(uint8 *)(result + i) = ch
        uc_err e = uc_mem_write(uc, result + i, &ch, 1);
        if (e != UC_ERR_OK)
        {
            printf("写入数据失败");
            assert(0);
        }
    }

    // null terminate
    zero = 0;
    uc_mem_write(uc, result + len, &zero, 1);
    return vm_set_call_result(result);
    // vm_readStringByPtr(result, sprintfBuff);
    // printf("DF_ReadString(adr:%x)\n", result);
    return result;
}

int vm_LzssDecode(u32 a1, u32 a2, u32 dest_2, u32 sizePtr)
{
    u32 v4 = a1;
    u32 v12 = a1 + a2 - 1;
    u32 dest = dest_2;
    u32 out_len = 0;
    u32 dest_1 = 0;

    u8 v7 = 0;
    u32 n3 = 0;
    u32 n3_1 = 0;
    u32 n3_2 = 0;

    u8 b = 0;
    u32 src = 0;
    u32 i = 0;
    uc_engine *uc = MTK;
    // out_len = *a4
    uc_mem_read(uc, sizePtr, &out_len, 4);

    dest_1 = dest_2 + out_len - 1;

    while (v4 <= v12 && dest <= dest_1)
    {
        uc_mem_read(uc, v4, &v7, 1);

        if (v7 >> 7)
        {
            n3 = v7 & 0x7F;

            if (n3 > (dest_1 - dest + 1))
                n3 = (dest_1 - dest + 1);

            n3_2 = (u8)n3;

            // memcpy(dest, v4 + 1, n3)
            for (i = 0; i < n3_2; i++)
            {
                uc_mem_read(uc, v4 + 1 + i, &b, 1);
                uc_mem_write(uc, dest + i, &b, 1);
            }

            v4 += n3_2 + 1;
        }
        else
        {
            n3_1 = v7 >> 1;

            if (n3_1 > (dest_1 - dest + 1))
                n3_1 = (dest_1 - dest + 1);

            n3_2 = (u8)n3_1;

            u8 next_b = 0;
            uc_mem_read(uc, v4 + 1, &next_b, 1);

            u32 offset = ((v7 << 8) & 0x1FF) | next_b;

            src = dest - offset;

            // memcpy(dest, src, n3_2)
            for (i = 0; i < n3_2; i++)
            {
                uc_mem_read(uc, src + i, &b, 1);
                uc_mem_write(uc, dest + i, &b, 1);
            }

            v4 += 2;
        }

        dest += n3_2;
    }

    out_len = dest - dest_2;

    uc_mem_write(uc, sizePtr, &out_len, 4);

    return out_len;
}
// ok
u32 vm_GetStreamDataFormRes(u32 a1, u32 a2, u32 a3, u32 a4)
{
    u32 v5 = 0;
    u32 v8 = 0;
    u8 b1 = 0, b2 = 0, b3 = 0, b4 = 0;
    u8 b5 = 0, b6 = 0, b7 = 0, b8 = 0;
    u32 dest = 0;
    uc_engine *uc = MTK;
    // 读取 a1[1..4] -> v5（输入压缩长度）
    uc_mem_read(uc, a1 + 1, &b1, 1);
    uc_mem_read(uc, a1 + 2, &b2, 1);
    uc_mem_read(uc, a1 + 3, &b3, 1);
    uc_mem_read(uc, a1 + 4, &b4, 1);

    v5 = (u32)b4 | ((u32)b3 << 8) | ((u32)b2 << 16) | ((u32)b1 << 24);

    // 读取 a1[5..8] -> v8（解压后长度）
    uc_mem_read(uc, a1 + 5, &b5, 1);
    uc_mem_read(uc, a1 + 6, &b6, 1);
    uc_mem_read(uc, a1 + 7, &b7, 1);
    uc_mem_read(uc, a1 + 8, &b8, 1);

    v8 = (u32)b8 | ((u32)b7 << 8) | ((u32)b6 << 16) | ((u32)b5 << 24);

    // printf("GetStreamDataFormRes data=%x bufInSize=%d,bufOutSize=%d", a1, v5, v8);

    dest = vm_malloc(v8);

    // _rt_memclr(dest, v8)
    u8 zero = 0;
    for (u32 i = 0; i < v8; i++)
    {
        uc_mem_write(uc, dest + i, &zero, 1);
    }
    // LzssDecode(a1 + 9, v5, dest, &v8);
    uc_mem_write(MTK, VM_Str_Tmp_ADDRESS, &v8, 4);
    vm_LzssDecode(a1 + 9, v5, dest, VM_Str_Tmp_ADDRESS);
    // uc_mem_read(MTK,dest,sprintfBuff,256);
    return vm_set_call_result(dest);
}
// ok
u32 vm_DF_GetStreamTResource(int a1)
{
    u32 DreamFactoryResourceBuffer; // r0
    DreamFactoryResourceBuffer = vm_DF_GetResourceByFileName(a1);
    uc_mem_write(MTK, VM_DreamFactoryResourceBuffer_ADDRESS, &DreamFactoryResourceBuffer, 4);
    return DreamFactoryResourceBuffer;
}

u32 vm_MF_resetGmemoryBlock()
{
    vm_MF_MemoryBlock_Reset(VM_MemoryBlock_PTR_ADDRESS);
    return vm_set_call_result(VM_MemoryBlock_PTR_ADDRESS);
}

void vm_strcpy(u32 dst, u32 src)
{
    vm_readStringByPtr(src, cbeTextString);
    uc_mem_write(MTK, dst, cbeTextString, strlen(cbeTextString) + 1);
}