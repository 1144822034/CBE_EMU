#include "main.h"
#include "gifDecode.h"
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdarg.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

FILE *openFileList[16];
static char openFileNames[16][256];

static void vm_read_string_by_ptr_limited(u32 ptr, char *dst, size_t dstSize);

static void vm_fileio_trace(const char *fmt, ...)
{
    FILE *fp = fopen("storage_trace.log", "a");
    if (fp == NULL)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

static void vm_fileio_trace_bytes(const char *tag, int handle, const char *path, const void *data, int len)
{
    FILE *fp = fopen("storage_trace.log", "a");
    if (fp == NULL)
        return;

    const u8 *bytes = (const u8 *)data;
    int dumpLen = len < 48 ? len : 48;
    fprintf(fp, "%s handle=%d path=%s len=%d dump=%d hex=", tag, handle, path ? path : "", len, dumpLen);
    for (int i = 0; i < dumpLen; ++i)
        fprintf(fp, "%02x", bytes[i]);
    fprintf(fp, " ascii=");
    for (int i = 0; i < dumpLen; ++i)
    {
        u8 ch = bytes[i];
        fputc((ch >= 0x20 && ch <= 0x7e) ? ch : '.', fp);
    }
    fputc('\n', fp);
    fclose(fp);
}

static void vm_lcd_image_trace(const char *fmt, ...)
{
    FILE *fp = fopen("net_trace.log", "a");
    if (fp == NULL)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(fp, fmt, args);
    va_end(args);
    fclose(fp);
}

#if TRACE_RESOURCE_IO
#define VM_RESOURCE_TRACE(...) vm_fileio_trace(__VA_ARGS__)
#else
#define VM_RESOURCE_TRACE(...) ((void)0)
#endif

static void vm_trim_mmorpg_tempdata_header(void)
{
    FILE *fp = fopen("JHOnlineData/mmorpgTempdata", "rb");
    if (fp == NULL)
        return;
    u8 header[40];
    size_t len = fread(header, 1, sizeof(header), fp);
    fclose(fp);
    if (len != sizeof(header))
        return;
    fp = fopen("JHOnlineData/mmorpgTempdata", "wb");
    if (fp == NULL)
        return;
    fwrite(header, 1, sizeof(header), fp);
    fclose(fp);
    vm_fileio_trace("file_trim_tempdata_header len=%u\n", (unsigned)sizeof(header));
}
#define VM_PSEUDO_DIR_HANDLE ((FILE *)-1)

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
int vm_DF_DataPackage_LoadPackage(int a1, int a2);
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
void vm_initManagerTable();
void vm_configManagerTable(u32 a, u32 b);
void vm_configManagerTableCount(u32 tableAddr, u32 funcAddr, u32 count);
void vm_InitDlRsManager(u32 tmp1);
void vm_InitDlImageManager(u32 tmp1);
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

static void vm_file_normalize_host_path(const char *src, char *dst, size_t dstSize)
{
    size_t pos = 0;
    if (dstSize == 0)
        return;
    dst[0] = 0;
    if (src == NULL)
        return;

    while (*src == '\\' || *src == '/')
        ++src;
    if (src[0] == '.' && (src[1] == '\\' || src[1] == '/'))
    {
        src += 2;
        while (*src == '\\' || *src == '/')
            ++src;
    }

    while (*src && pos + 1 < dstSize)
    {
        char ch = *src++;
        if (ch == '\\')
            ch = '/';
        dst[pos++] = ch;
    }
    while (pos > 1 && dst[pos - 1] == '/')
        --pos;
    dst[pos] = 0;
}

static void vm_read_path_string(u32 namePtr, char *out, size_t outSize)
{
    u8 rawName[256];
    memset(out, 0, outSize);
    if (outSize == 0)
        return;
    uc_mem_read(MTK, namePtr, rawName, sizeof(rawName));
    if (rawName[0] != 0 && rawName[1] == 0)
    {
        u32 ucs2Len = 0;
        while (ucs2Len + 1 < sizeof(rawName) && (rawName[ucs2Len] != 0 || rawName[ucs2Len + 1] != 0))
            ucs2Len += 2;
        ucs2_to_gbk(rawName, ucs2Len, out, outSize);
    }
    else
    {
        vm_read_string_by_ptr_limited(namePtr, out, outSize);
    }
}

static int vm_is_pseudo_dir_path(const char *nameBuf)
{
    char normalized[256];
    size_t len;
    struct stat st;

    if (nameBuf == NULL || nameBuf[0] == 0)
        return 0;

    if (strcmp(nameBuf, "./") == 0 || strcmp(nameBuf, ".\\") == 0)
        return 1;

    vm_file_normalize_host_path(nameBuf, normalized, sizeof(normalized));
    len = strlen(normalized);

    while (len > 0 && (normalized[len - 1] == '/' || normalized[len - 1] == '\\'))
        normalized[--len] = 0;

    if (len == 0)
        return 0;

    return stat(normalized, &st) == 0 && (st.st_mode & S_IFDIR);
}

static int vm_is_cbm_resource_path(const char *nameBuf)
{
    const char *ext = strrchr(nameBuf, '.');
    if (ext == NULL)
        return 0;
    return strcasecmp(ext, ".cbm") == 0;
}

static int vm_file_ext_requires_binary(const char *nameBuf)
{
    const char *ext = strrchr(nameBuf, '.');
    if (strstr(nameBuf, "cbm") != NULL || strstr(nameBuf, "CBM") != NULL)
        return 1;
    if (ext == NULL)
        return 0;
    return _stricmp(ext, ".cbe") == 0 ||
           _stricmp(ext, ".cbm") == 0 ||
           _stricmp(ext, ".gif") == 0 ||
           _stricmp(ext, ".png") == 0 ||
           _stricmp(ext, ".actor") == 0 ||
           _stricmp(ext, ".map") == 0 ||
           _stricmp(ext, ".sce") == 0 ||
           _stricmp(ext, ".dsh") == 0 ||
           _stricmp(ext, ".mid") == 0 ||
           _stricmp(ext, ".mp3") == 0;
}

static int vm_file_mode_is_writeable(const char *mode)
{
    if (mode == NULL)
        return 0;
    return strchr(mode, 'w') != NULL || strchr(mode, 'a') != NULL || strchr(mode, '+') != NULL;
}

static void vm_file_make_dir(const char *path)
{
    char normalized[256];
    vm_file_normalize_host_path(path, normalized, sizeof(normalized));
    if (normalized[0] == 0 || strcmp(normalized, ".") == 0)
        return;
#ifdef _WIN32
    _mkdir(normalized);
#else
    mkdir(normalized, 0755);
#endif
}

static void vm_file_ensure_parent_dirs(const char *path)
{
    char tmp[256];
    size_t len;
    if (path == NULL)
        return;

    len = strlen(path);
    if (len >= sizeof(tmp))
        len = sizeof(tmp) - 1;
    memcpy(tmp, path, len);
    tmp[len] = 0;

    for (size_t i = 0; tmp[i]; ++i)
    {
        if (tmp[i] == '\\')
            tmp[i] = '/';
    }

    for (size_t i = 0; tmp[i]; ++i)
    {
        if (tmp[i] != '/')
            continue;
        tmp[i] = 0;
        vm_file_make_dir(tmp);
        tmp[i] = '/';
    }
}

static void vm_file_select_mode(int openMode, const char *modeHint, char *mode, size_t modeSize)
{
    snprintf(mode, modeSize, "rb");
    if (modeHint && modeHint[0])
    {
        char hint[8] = {0};
        size_t pos = 0;
        for (size_t i = 0; modeHint[i] && pos + 1 < sizeof(hint); ++i)
        {
            char ch = modeHint[i];
            if (ch == 'r' || ch == 'w' || ch == 'a' || ch == 'b' || ch == '+')
                hint[pos++] = ch;
        }
        if (pos)
        {
            snprintf(mode, modeSize, "%s", hint);
            return;
        }
    }

    if (openMode & 0x10)
        snprintf(mode, modeSize, "ab+");
    else if (openMode & 0x08)
        snprintf(mode, modeSize, "wb+");
    else if (openMode & 0x04)
        snprintf(mode, modeSize, "rb+");
    else if (openMode == 1)
        snprintf(mode, modeSize, "wb+");
    else if (openMode == 3)
        snprintf(mode, modeSize, "rb+");
    else if (openMode == 0 || openMode == 2)
        snprintf(mode, modeSize, "rb");
}

int vm_get_file_handle(char *nameBuf, const char *mode)
{
    int handle = -1;
    char binaryMode[8];
    char normalizedName[256];
    const char *openMode = mode;
    vm_file_normalize_host_path(nameBuf, normalizedName, sizeof(normalizedName));
    if (normalizedName[0] == 0)
        snprintf(normalizedName, sizeof(normalizedName), "%s", nameBuf);
    if (vm_file_ext_requires_binary(normalizedName) && mode && strchr(mode, 'b') == NULL)
    {
        snprintf(binaryMode, sizeof(binaryMode), "%sb", mode);
        openMode = binaryMode;
    }
    for (int i = 0; i < 16; i++)
    {
        if (openFileList[i] == NULL)
        {
            if (vm_is_pseudo_dir_path(normalizedName))
            {
                openFileList[i] = VM_PSEUDO_DIR_HANDLE;
                snprintf(openFileNames[i], sizeof(openFileNames[i]), "%s", normalizedName);
                return i;
            }
            FILE *f = fopen(normalizedName, openMode);
            if (f == NULL && vm_file_mode_is_writeable(openMode))
            {
                vm_file_ensure_parent_dirs(normalizedName);
                f = fopen(normalizedName, openMode);
                if (f == NULL)
                    f = fopen(normalizedName, "wb+");
            }
            if (f == NULL)
            {
                vm_fileio_trace("file_open_fail path=%s mode=%s\n", normalizedName, openMode);
                return -1;
            }
            openFileList[i] = f;
            snprintf(openFileNames[i], sizeof(openFileNames[i]), "%s", normalizedName);
            vm_fileio_trace("file_open handle=%d path=%s mode=%s\n", i, normalizedName, openMode);
            return i;
        }
    }
    return -1;
}
int vm_dir_exists(int a1)
{
    if (a1 == 0)
        return vm_set_call_result(0);
    char nameBuf[1024];
    vm_read_path_string(a1, nameBuf, sizeof(nameBuf));
    char normalizedName[256];
    vm_file_normalize_host_path(nameBuf, normalizedName, sizeof(normalizedName));
    if (strcmp(normalizedName, ".") == 0)
        return vm_set_call_result(0);
    int r = dirExists(normalizedName);
    vm_fileio_trace("dir_exists raw=%s normalized=%s result=%d last=%08x\n", nameBuf, normalizedName, r, lastAddress);
    return vm_set_call_result(r);
}

// ok
int vm_cbfs_vm_file_open(int openMode, int namePtr, int rwPtr)
{
    char rwBuff[128] = {0};
    char nameBuff[256];
    char mode[8];
    u8 rawName[256];
    uc_mem_read(MTK, namePtr, rawName, sizeof(rawName));
    vm_read_path_string(namePtr, nameBuff, sizeof(nameBuff));
    if (rwPtr)
        uc_mem_read(MTK, rwPtr, &rwBuff, 128);
    rwBuff[sizeof(rwBuff) - 1] = 0;
    vm_file_select_mode(openMode, rwBuff, mode, sizeof(mode));
    int handle = vm_get_file_handle(nameBuff, mode);
    vm_fileio_trace("file_open_request openMode=%x namePtr=%08x name=%s hint=%s selected=%s handle=%d last=%08x\n",
                    openMode, namePtr, nameBuff, rwBuff, mode, handle, lastAddress);
    if (handle < 0)
    {
        u32 r[8] = {0};
        uc_reg_read(MTK, UC_ARM_REG_R0, &r[0]);
        uc_reg_read(MTK, UC_ARM_REG_R1, &r[1]);
        uc_reg_read(MTK, UC_ARM_REG_R2, &r[2]);
        uc_reg_read(MTK, UC_ARM_REG_R3, &r[3]);
        uc_reg_read(MTK, UC_ARM_REG_R4, &r[4]);
        uc_reg_read(MTK, UC_ARM_REG_R5, &r[5]);
        uc_reg_read(MTK, UC_ARM_REG_R6, &r[6]);
        uc_reg_read(MTK, UC_ARM_REG_LR, &r[7]);
        vm_fileio_trace("file_open_fail_regs namePtr=%08x r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x lr=%08x last=%08x\n",
                        namePtr, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], lastAddress);
        vm_fileio_trace_bytes("file_open_fail_mem_namePtr", -1, nameBuff, rawName, sizeof(rawName));
        for (u32 i = 4; i <= 6; ++i)
        {
            if ((r[i] >= ROM_ADDRESS && r[i] < ROM_ADDRESS + 0x1000000) ||
                (r[i] >= VM_Memory_Pool_ADDRESS && r[i] < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE) ||
                (r[i] >= STACK_ADDRESS && r[i] < STACK_ADDRESS + 0x100000))
            {
                u8 bytes[96] = {0};
                if (uc_mem_read(MTK, r[i], bytes, sizeof(bytes)) == UC_ERR_OK)
                {
                    char tag[64];
                    snprintf(tag, sizeof(tag), "file_open_fail_mem_r%u_%08x", i, r[i]);
                    vm_fileio_trace_bytes(tag, -1, nameBuff, bytes, sizeof(bytes));
                }
            }
        }
    }
    vm_fileio_trace_bytes("file_open_raw_name", -1, nameBuff, rawName, sizeof(rawName));
    return vm_set_call_result(handle);
}
// ok
int vm_cbfs_vm_file_read(int bufferPtr, int size, int handle)
{
    if (handle < 0 || handle >= 16 || openFileList[handle] == NULL || size <= 0)
        return vm_set_call_result(-1);
    if (openFileList[handle] == VM_PSEUDO_DIR_HANDLE)
        return vm_set_call_result(0);
    char *tmp = SDL_malloc(size);
    int readed = fread(tmp, 1, size, openFileList[handle]);

    if (readed > 0)
    {
        uc_mem_write(MTK, bufferPtr, tmp, readed);
        vm_fileio_trace_bytes("file_read", handle, openFileNames[handle], tmp, readed);
    }
    else
    {
        vm_fileio_trace("file_read handle=%d path=%s size=%d result=%d\n", handle, openFileNames[handle], size, readed);
    }
    SDL_free(tmp);
    return vm_set_call_result(readed);
}
// ok
int vm_cbfs_vm_file_write(int bufferPtr, int size, int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= 16 || openFileList[fileHandle] == NULL || size <= 0)
        return vm_set_call_result(-1);
    if (openFileList[fileHandle] == VM_PSEUDO_DIR_HANDLE)
        return vm_set_call_result(0);
    void *buffer = SDL_malloc(size);
    uc_mem_read(MTK, bufferPtr, buffer, size);
    int r = fwrite(buffer, 1, size, openFileList[fileHandle]);
    if (r > 0)
        vm_fileio_trace_bytes("file_write_data", fileHandle, openFileNames[fileHandle], buffer, r);
    SDL_free(buffer);
    vm_fileio_trace("file_write handle=%d path=%s size=%d result=%d\n", fileHandle, openFileNames[fileHandle], size, r);
    return vm_set_call_result(r);
}
// ok
int vm_cbfs_vm_file_seek(int handle, int pos, int type)
{
    if (handle < 0 || handle >= 16 || openFileList[handle] == NULL)
        return vm_set_call_result(-1);
    if (openFileList[handle] == VM_PSEUDO_DIR_HANDLE)
        return vm_set_call_result(0);
    int r = fseek(openFileList[handle], pos, type);
    if (r == 0)
        r = ftell(openFileList[handle]);
    vm_fileio_trace("file_seek handle=%d path=%s pos=%d type=%d result=%d\n", handle, openFileNames[handle], pos, type, r);
    return vm_set_call_result(r);
}
// ok
int vm_cbfs_vm_file_tell(int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= 16 || openFileList[fileHandle] == NULL)
        return vm_set_call_result(-1);
    if (openFileList[fileHandle] == VM_PSEUDO_DIR_HANDLE)
        return vm_set_call_result(0);
    int r = ftell(openFileList[fileHandle]);
    vm_fileio_trace("file_tell handle=%d path=%s result=%d\n", fileHandle, openFileNames[fileHandle], r);
    return vm_set_call_result(r);
}

int vm_cbfs_vm_file_exists(int disk, int namePtr)
{
    u8 charBuffer[1024];
    vm_read_path_string(namePtr, (char *)charBuffer, sizeof(charBuffer));
    char normalizedName[256];
    vm_file_normalize_host_path((char *)charBuffer, normalizedName, sizeof(normalizedName));
    if (vm_is_pseudo_dir_path(normalizedName))
        return vm_set_call_result(1);
    FILE *f = fopen(normalizedName, "rb");
    u32 r = 0;
    if (f != NULL)
    {
        r = 1;
        fclose(f);
    }
    vm_fileio_trace("file_exists raw=%s normalized=%s result=%u last=%08x\n", charBuffer, normalizedName, r, lastAddress);
    return vm_set_call_result(r);
}

int vm_cbfs_vm_file_delete(int disk, int namePtr)
{
    u8 charBuffer[1024];
    vm_read_path_string(namePtr, (char *)charBuffer, sizeof(charBuffer));
    char normalizedName[256];
    vm_file_normalize_host_path((char *)charBuffer, normalizedName, sizeof(normalizedName));
    if (vm_is_pseudo_dir_path(normalizedName) || vm_is_cbm_resource_path(normalizedName))
        return vm_set_call_result(0);
    FILE *f = fopen(normalizedName, "rb");
    u32 r = 0;
    if (f != NULL)
    {
        r = 1;
        fclose(f);
    }
    if (r == 1)
        unlink(normalizedName);
    return vm_set_call_result(r);
}
int vm_cbfs_vm_file_getfilesize(int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= 16 || openFileList[fileHandle] == NULL)
        return vm_set_call_result(-1);
    if (openFileList[fileHandle] == VM_PSEUDO_DIR_HANDLE)
        return vm_set_call_result(0);
    FILE *f = openFileList[fileHandle];
    fseek(f, 0, SEEK_END);
    int r = ftell(f);
    fseek(f, 0, SEEK_SET);
    vm_fileio_trace("file_getfilesize handle=%d path=%s result=%d\n", fileHandle, openFileNames[fileHandle], r);
    return vm_set_call_result(r);
}
// ok
int vm_cbfs_vm_file_close(int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= 16 || openFileList[fileHandle] == NULL)
        return vm_set_call_result(-1);
    if (openFileList[fileHandle] == VM_PSEUDO_DIR_HANDLE)
    {
        openFileList[fileHandle] = NULL;
        openFileNames[fileHandle][0] = 0;
        return vm_set_call_result(0);
    }
    int r = fclose(openFileList[fileHandle]);
    vm_fileio_trace("file_close handle=%d path=%s result=%d\n", fileHandle, openFileNames[fileHandle], r);
    openFileList[fileHandle] = NULL;
    openFileNames[fileHandle][0] = 0;
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
        if (v3)
        {
            uc_mem_read(MTK, v3 + 1, &tmp, 1);
            if (tmp == v1)
                goto LABEL_8;
        }
    }
    return (u8)v1;
}

int vm_DF_DataPackage_LoadPackage(int a1, int srcPtr)
{
    int result = 0;
    char traceName[128] = {0};
    if (srcPtr)
        vm_readStringByPtr(srcPtr, traceName);
    VM_RESOURCE_TRACE("datapackage_load_package begin pkg=%08x src=%08x name=%s count=%u\n",
                      a1, srcPtr, traceName, (unsigned)vm_get_var_short(a1 + 10));

    /* src == NULL -> set first byte at a1 to 0 and return 0 */
    if (srcPtr == 0)
    {
        vm_set_var_byte(a1, 0); /* *(_BYTE *)a1 = 0; */
        VM_RESOURCE_TRACE("datapackage_load_package null pkg=%08x\n", a1);
        return 0;
    }

    /* read current slot count (u16) and table base pointer */
    int slot_count = (unsigned short)vm_get_var_short(a1 + 10); /* *(__int16 *)(a1 + 10) */
    int table_base = vm_get_var(a1 + 28);                       /* *(_DWORD *)(a1 + 28) */

    /* search for a free slot */
    for (int idx = 0; idx < slot_count; ++idx)
    {
        int slot_addr_vm = table_base + 4 * idx; /* address in VM where slot pointer is stored */
        int slot_ptr = vm_get_var(slot_addr_vm); /* *(_DWORD *)(table_base + 4*idx) */

        if (slot_ptr == 0)
        {
            /* allocate slot struct (108 bytes) and write pointer into table_base+4*idx */
            vm_DF_Malloc_IN(slot_addr_vm, 108);  /* DF_Malloc_IN((DWORD*)(table_base+4*idx), 108) */
            slot_ptr = vm_get_var(slot_addr_vm); /* newly allocated slot pointer */

            /* length of src string in VM */
            int n = vm_strlen(srcPtr);

            /* allocate name buffer at slot_ptr+4 and copy string */
            vm_DF_Malloc_IN(slot_ptr + 4, n + 1); /* DF_Malloc_IN((DWORD*)(slot+4), n+1) */
            int name_buf = vm_get_var(slot_ptr + 4);
            vm_memcpy(name_buf, srcPtr, n);   /* _rt_memcpy -> vm_memcpy(dst, src, len) */
            vm_set_var_byte(name_buf + n, 0); /* null-terminate */

            /* initialize fields: word at slot+10 = 0; byte at slot = 0 */
            vm_set_var_short(slot_ptr + 10, 0);
            vm_set_var_byte(slot_ptr, 0);

            /* assign package index into slot->byte[1] and return it */
            int pkgIndex = vm_DF_DataPackage_GetPackageIndex(a1);
            vm_set_var_byte(slot_ptr + 1, (unsigned char)pkgIndex);
            VM_RESOURCE_TRACE("datapackage_load_package reuse_slot pkg=%08x name=%s slot=%d child=%08x pkgIndex=%d count=%u\n",
                              a1, traceName, idx, slot_ptr, pkgIndex, (unsigned)vm_get_var_short(a1 + 10));
            return vm_set_call_result(pkgIndex);
        }
    }

    /* no free slot: append at end (index = slot_count) */
    {
        int new_slot_addr_vm = table_base + 4 * slot_count;
        vm_DF_Malloc_IN(new_slot_addr_vm, 108);
        int slot_ptr = vm_get_var(new_slot_addr_vm);

        int n = vm_strlen(srcPtr);

        vm_DF_Malloc_IN(slot_ptr + 4, n + 1);
        int name_buf = vm_get_var(slot_ptr + 4);
        vm_memcpy(name_buf, srcPtr, n);
        vm_set_var_byte(name_buf + n, 0);

        vm_set_var_short(slot_ptr + 10, 0);
        vm_set_var_byte(slot_ptr, 0);

        int pkgIndex = vm_DF_DataPackage_GetPackageIndex(a1);
        vm_set_var_byte(slot_ptr + 1, (unsigned char)pkgIndex);

        result = slot_count + 1;
        vm_set_var_short(a1 + 10, (unsigned short)result); /* update slot count */
        VM_RESOURCE_TRACE("datapackage_load_package append pkg=%08x name=%s slot=%d child=%08x pkgIndex=%u count=%u\n",
                          a1, traceName, slot_count, slot_ptr, (unsigned)vm_get_var_byte(slot_ptr + 1),
                          (unsigned)vm_get_var_short(a1 + 10));
    }

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

    // printf("DF_DataPackage_InitTxt 222:%x,%x\n", a2, count);

    // 条件判断
    if (fileHandle < 0 || count <= index || txt_buffer)
    {
        // printf("DF_DataPackage_InitTxt error :%x\n", fileHandle);
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
    u32 offset;
    u16 ret = 0;
    uc_mem_read(MTK, offsetPtr, &offset, 4);

    uc_mem_read(MTK, bufPtr + offset, &ret, 2);
    if ((lastAddress >= 0x0100d780 && lastAddress <= 0x0100d8c0) ||
        lastAddress == 0x0100dbdc)
    {
        u8 raw[8] = {0};
        uc_mem_read(MTK, bufPtr + offset, raw, sizeof(raw));
        vm_fileio_trace("df_read_short base=%08x off=%u raw=%02x%02x%02x%02x%02x%02x%02x%02x ret=%u next=%u last=%08x\n",
                        bufPtr,
                        offset,
                        raw[0], raw[1], raw[2], raw[3], raw[4], raw[5], raw[6], raw[7],
                        ret,
                        offset + 2,
                        lastAddress);
    }
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
    if ((lastAddress >= 0x0100d560 && lastAddress <= 0x0100de90) ||
        (lastAddress >= 0x0100d780 && lastAddress <= 0x0100d8c0))
    {
        vm_fileio_trace("df_read_int base=%08x off=%u raw=%02x%02x%02x%02x ret=%d next=%u last=%08x\n",
                        a1,
                        offset - 4,
                        arr[0], arr[1], arr[2], arr[3],
                        result,
                        offset,
                        lastAddress);
    }
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
    len = vm_get_var_byte(a2 + offset);

    offset++;

    buf_ptr = vm_malloc(len + 1);
    vm_set_var(a1, buf_ptr);

    // 一次性 memcpy（核心优化）
    vm_memcpy(buf_ptr, a2 + offset, len);

    offset += len;
    vm_set_var(a3, offset);

    u8 zero = 0;
    uc_mem_write(MTK, buf_ptr + len, &zero, 1);

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
    u32 target = 0;
    u32 zero = 0;

    if (p)
    {
        uc_mem_read(MTK, p, &target, 4);
        if (target)
            vm_free(target);
        uc_mem_write(MTK, p, &zero, 4);
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
    // printf("DF_DataPackage_LoadFormTCard2:%x\n", size);

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
        count = (u16)vm_DF_ReadInt(bufferPtr, ptr);
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
        char entryName[128] = {0};
        if (namePtr)
            vm_readStringByPtr(namePtr, entryName);
        u8 entryLoaded = 0xff;
        if (entry)
            entryLoaded = vm_get_var_byte(entry);
        VM_RESOURCE_TRACE("datapackage_tcard_entry pkg=%08x i=%d count=%d name=%s namePtr=%08x entry=%08x loaded=%u nextOff=%08x fileSeek=%08x\n",
                          a1, i, count, entryName, namePtr, entry, entryLoaded, offset, fileSeekPos);

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

                u32 blockOffset = 0;

                int arr_cnt = 0, data_size = 0;
                if (1)
                {
                    u32 ptr = vm_malloc_var();
                    vm_set_var(ptr, blockOffset);
                    data_size = vm_DF_ReadInt(pfBuffer, ptr); //&offset
                    blockOffset = vm_get_var(ptr);

                    arr_cnt = vm_DF_ReadInt(pfBuffer, ptr); //&offset
                    blockOffset = vm_get_var(ptr);
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
                            vm_set_var(ptr, blockOffset);
                            int val = vm_DF_ReadInt(pfBuffer, ptr);
                            blockOffset = vm_get_var(ptr);
                            vm_set_var(res_idxPtr + 4 * k, val);
                        }

                        for (int j = 0; j < arr_cnt; j++)
                        {
                            vm_set_var(ptr, blockOffset);
                            vm_DF_ReadStringEx((int *)(res_stringPtr + 4 * j), pfBuffer, ptr);
                            blockOffset = vm_get_var(ptr);

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

    if (bufferPtr)
    {
        u32 ptr = vm_malloc_var();
        vm_set_var(ptr, bufferPtr);
        vm_DF_Free(ptr);
        vm_free_var(ptr);
    }

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
    u8 n2 = 0;
    u32 inFileOffset = 0;

    if (a3)
    {
        uc_mem_read(MTK, VM_DF_DataPackage_LoadType_ADDRESS, &n2, 1);
        uc_mem_read(MTK, VM_DF_DataPackage_In_File_Offset_ADDRESS, &inFileOffset, 4);
        VM_RESOURCE_TRACE("datapackage_doload a1=%08x a2=%08x a3=%08x loadType=%u inFileOffset=%08x\n",
                          a1, a2, a3, n2, inFileOffset);

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

    if (a2 == 0)
        VM_RESOURCE_TRACE("datapackage_load_tresource_null a1=%08x a3=%08x loadType=%u\n", a1, a3, n2);

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
    // printf("vm_initDFDataPackage(%x,%x)\n", a1, a2); // debug
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
    u8 charBuffer[1024];
    u8 spBuffer[1024];
    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_mem_read(MTK, tmp1, charBuffer, 128);
    printf("[vm_sprintf]");
    tmp1 = 0;
    tmp2 = 0;
    tmp3 = 0;
    while (tmp1 < 128)
    {
        char c = charBuffer[tmp1++];
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
                    vm_readStringByPtr(tmp5, spBuffer);
                    printf(spBuffer);
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
    u8 charBuffer[1024];
    u8 spBuffer[1024];
    uc_reg_read(MTK, UC_ARM_REG_R0, &inBufferPtr);
    vm_readStringByReg(UC_ARM_REG_R1, charBuffer);
    strCount = strlen(charBuffer);
    i = 0;
    paramPos = 0;
    needConvert = 0;
    u32 bufferPtr = inBufferPtr;
    while (1)
    {
        u8 c = charBuffer[i++];
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
                    vm_readStringByPtr(currParam, spBuffer);
                    strCnt2 = strlen(spBuffer);
                    uc_mem_write(MTK, inBufferPtr, spBuffer, strCnt2);
                    inBufferPtr += strCnt2;
                }
                else if (c == 'x')
                {
                    sprintf(spBuffer, "%x", currParam);
                    strCnt2 = strlen(spBuffer);
                    uc_mem_write(MTK, inBufferPtr, spBuffer, strCnt2);
                    inBufferPtr += strCnt2;
                }
                else if (c == 'd')
                {
                    sprintf(spBuffer, "%d", currParam);
                    strCnt2 = strlen(spBuffer);
                    uc_mem_write(MTK, inBufferPtr, spBuffer, strCnt2);
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

void vm_DF_GetFormatString()
{
    int i, paramPos, needConvert, tmp4, currParam, strCount, strCnt2;
    u8 charBuffer[1024];
    u8 spBuffer[1024];
    vm_readStringByReg(UC_ARM_REG_R0, charBuffer);
    strCount = strlen(charBuffer);
    i = 0;
    paramPos = 0;
    needConvert = 0;
    u32 inBufferPtr = VM_DreamFactory_CharBuffer_ADDRESS;
    u32 bufferPtr = inBufferPtr;
    while (1)
    {
        u8 c = charBuffer[i++];
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
                if (paramPos < 3)
                {
                    uc_reg_read(MTK, UC_ARM_REG_R1 + paramPos, &currParam);
                }
                else
                { // 从栈顶读取参数
                    uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                    uc_mem_read(MTK, tmp4 + (paramPos - 2) * 4, &currParam, 4);
                }

                if (c == 's')
                {
                    vm_readStringByPtr(currParam, spBuffer);
                    strCnt2 = strlen(spBuffer);
                    uc_mem_write(MTK, inBufferPtr, spBuffer, strCnt2);
                    inBufferPtr += strCnt2;
                }
                else if (c == 'x')
                {
                    sprintf(spBuffer, "%x", currParam);
                    strCnt2 = strlen(spBuffer);
                    uc_mem_write(MTK, inBufferPtr, spBuffer, strCnt2);
                    inBufferPtr += strCnt2;
                }
                else if (c == 'd')
                {
                    sprintf(spBuffer, "%d", currParam);
                    strCnt2 = strlen(spBuffer);
                    uc_mem_write(MTK, inBufferPtr, spBuffer, strCnt2);
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
    // printf("[vm_DF_GetFormatString]%s\n", cbeTextString);
    vm_set_call_result(bufferPtr);
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
    // p_g_memoryBlock[0] = 新的内存块地址。全局块使用固定池；其它块需要独立
    // backing storage，避免多个 MemoryBlock 互相覆盖 actor/资源解析结果。
    tmp1 = (p_g_memoryBlock == VM_MemoryBlock_PTR_ADDRESS) ? VM_MF_MemoryBlock_POOL_ADDRESS : vm_malloc(size);
    VM_RESOURCE_TRACE("memory_block_init ptr=%08x base=%08x size=%08x\n", p_g_memoryBlock, tmp1, size);
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
    u32 alignedSize = (a2 + 3) & ~3;
    offset = oldOffset + alignedSize;
    if (offset > size)
    {
        printf("申请内存(%x)超过可用值(%x)\n", a2, size - oldOffset);
        dumpCpuInfo();
        assert(0);
    }
    uc_mem_write(MTK, a1 + 4, &offset, 4);
    return vm_set_call_result(baseAddr + oldOffset);
    // printf("[call]MF_MemoryBlock_Malloc(%x,%x)(%x)\n", baseAddr, a2, oldOffset);
}

void vm_MF_MemoryBlock_Reset(int a1)
{
    u32 v = 0;
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
    char traceName[128] = {0};
    if (namePtr)
        vm_readStringByPtr(namePtr, traceName);
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
            VM_RESOURCE_TRACE("df_get_file_id hit pkg=%08x name=%s id=%d index=%d local=1 str=%08x names=%08x ids=%08x count=%d\n",
                              a1, traceName, (int16_t)file_id, i, str_ptr, base_ptr, id_base, count1);
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
                VM_RESOURCE_TRACE("df_get_file_id hit pkg=%08x name=%s id=%d child=%08x\n",
                                  a1, traceName, result, v7);
                uc_reg_write(uc, UC_ARM_REG_R0, &result);
                return result;
            }
        }
    }
    result = -1;
    VM_RESOURCE_TRACE("df_get_file_id miss pkg=%08x name=%s\n", a1, traceName);
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
    uc_mem_read(uc, a1 + 16, &data_base, 4);

    // base_offset = *(uint32 *)(a1 + 88)
    uc_mem_read(uc, a1 + 88, &base_offset, 4);

    // v5 = *(uint32 *)(data_base + 4 * a2)
    uc_mem_read(uc, data_base + 4 * a2, &v5, 4);

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
    u8 head[8] = {0};
    if (len > 0)
        uc_mem_read(MTK, buffer, head, SDL_min((u32)len, (u32)sizeof(head)));
    VM_RESOURCE_TRACE("df_read_data pkg=%08x index=%d dataBase=%08x seek=%08x len=%08x out=%08x head=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                      a1, a2, data_base, v5 + base_offset, len, buffer,
                      head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
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
    // printf("DF_DataPackage_GetFileByID:%d\n", fileId);

    for (i = 0; i < count1; i++)
    {
        uc_mem_read(uc, id_base + i * 2, &file_id, 2);

        if (file_id == (int16_t)fileId)
        {
            uc_mem_read(uc, a1 + 84, &flag, 1);

            if (flag)
            {
                int data = vm_DF_DataPackage_DoReadData(a1, i);
                VM_RESOURCE_TRACE("df_get_file_by_id hit pkg=%08x id=%d index=%d flag=%u out=%08x\n",
                                  a1, (int16_t)fileId, i, flag, data);
                return data;
            }
            else
            {
                uc_mem_read(uc, data_base + i * 4, &data_ptr, 4);
                offset = offset_base;
                offset = data_ptr + offset;
                VM_RESOURCE_TRACE("df_get_file_by_id hit pkg=%08x id=%d index=%d flag=%u out=%08x\n",
                                  a1, (int16_t)fileId, i, flag, offset);
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
                VM_RESOURCE_TRACE("df_get_file_by_id child pkg=%08x id=%d child=%08x out=%08x\n",
                                  a1, (int16_t)fileId, v7, result);
                return vm_set_call_result(result);
            }
        }
    }
    VM_RESOURCE_TRACE("df_get_file_by_id miss pkg=%08x id=%d\n", a1, (int16_t)fileId);
    return vm_set_call_result(0);
}

int vm_DF_DataPackage_GetFile(int a1, int namePtr)
{
    int FileID = vm_DF_DataPackage_GetFileID(a1, namePtr);
    int data = vm_DF_DataPackage_GetFileByID(a1, FileID);
    char traceName[128] = {0};
    if (namePtr)
        vm_readStringByPtr(namePtr, traceName);
    VM_RESOURCE_TRACE("df_get_file pkg=%08x name=%s id=%d out=%08x\n", a1, traceName, FileID, data);
    return data;
}

int vm_DF_DataPackage_GetFileNameByID(int a1, int a2)
{
    int i;
    int count;
    int table12;
    int table20;
    int table28;
    int j;
    int child_ptr;
    int res;
    /* count = *(__int16 *)(a1 + 8) */
    count = (unsigned short)vm_get_var_short(a1 + 8);

    table12 = vm_get_var(a1 + 12); /* pointer to filenames array */
    table20 = vm_get_var(a1 + 20); /* pointer to id (short) array */

    /* search local arrays: for i in [0, count) if id[i] == a2 return filenames[i] */
    for (i = 0; i < count; ++i)
    {
        int id = (short)vm_get_var_short(table20 + 2 * i); /* *(__int16 *)(table20 + 2*i) */
        if (id == a2)
        {
            res = vm_get_var(table12 + 4 * i); /* *(_DWORD *)(table12 + 4*i) */
            return vm_set_call_result(res);
        }
    }

    /* search child packages: iterate over package slots */
    j = 0;
    table28 = vm_get_var(a1 + 28); /* pointer to package pointer array */
    while (j < (unsigned short)vm_get_var_short(a1 + 10))
    {
        child_ptr = vm_get_var(table28 + 4 * j); /* *(_DWORD *)(table28 + 4*j) */
        if (child_ptr)
        {
            res = vm_DF_DataPackage_GetFileNameByID(child_ptr, a2);
            if (res)
                return vm_set_call_result(res);
        }
        j = (short)(j + 1);
    }

    return vm_set_call_result(0);
}

// ok
u32 vm_DF_GetResourceIDByFileName(int a1)
{
    u32 tmp1;
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
    if (tmp1)
        return vm_DF_DataPackage_GetFileID(tmp1, a1);
    else
        return vm_set_call_result(-1);
}
// ok
int vm_DF_GetResourceByFileName(int a1)
{
    int tmp1;
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
    if (tmp1)
        return vm_DF_DataPackage_GetFile(tmp1, a1);
    else
        return vm_set_call_result(-1);
}
// ok
int vm_DF_GetResourceByResourceID(int a1)
{
    int tmp1;
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
    if (tmp1)
        return vm_DF_DataPackage_GetFileByID(tmp1, a1);
    else
        return vm_set_call_result(-1);
}
// ok
void vm_DF_GetResourceNameByID(int a1)
{
    int tmp1;
    uc_mem_read(MTK, VM_DreamFactory_DataPackage_ADDRESS, &tmp1, 4);
    if (tmp1)
        return vm_DF_DataPackage_GetFileNameByID(tmp1, a1);
    else
        return vm_set_call_result(-1);
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
    int dstInfo, srcInfo, srcX, srcY, w, h, dstX, dstY, sp, dstPtr, srcPtr;
    u16 dst_w, dst_h, src_w, src_h;
    uc_reg_read(MTK, UC_ARM_REG_R0, &dstInfo);
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
    uc_mem_read(MTK, dstInfo, &dstPtr, 4);
    uc_mem_read(MTK, dstInfo + 4, &dst_w, 2);
    uc_mem_read(MTK, dstInfo + 6, &dst_h, 2);
    if (dstInfo == VM_screenImageStruct_ADDRESS || dstPtr == 0 || dst_w == 0 || dst_h == 0 || dst_w > LCD_WIDTH || dst_h > LCD_HEIGHT)
    {
        dstPtr = VM_screenImage_ADDRESS;
        dst_w = LCD_WIDTH;
        dst_h = LCD_HEIGHT;
    }
    int origSrcX = srcX, origSrcY = srcY, origW = w, origH = h, origDstX = dstX, origDstY = dstY;
    u8 tmpBuffer[480];
    // u8 buffer[188];
    // uc_mem_read(MTK, srcPtr, buffer, 188);
    // printf("vM_DrawImageWithClipEx(srcPtr:%x,srcX:%d,srcY:%d,clipW:%d,clipH:%d,dstX:%d,dstY:%d)\n", srcPtr, srcX, srcY, w, h, dstX, dstY);
    if (srcPtr == 0 || w <= 0 || h <= 0 || src_w == 0 || src_h == 0)
        return;

    if (srcX < 0)
    {
        dstX -= srcX;
        w += srcX;
        srcX = 0;
    }
    if (srcY < 0)
    {
        dstY -= srcY;
        h += srcY;
        srcY = 0;
    }
    if (srcX >= src_w || srcY >= src_h)
        return;
    if (srcX + w > src_w)
        w = src_w - srcX;
    if (srcY + h > src_h)
        h = src_h - srcY;
    if (dstX < 0)
    {
        srcX -= dstX;
        w += dstX;
        dstX = 0;
    }
    if (dstY < 0)
    {
        srcY -= dstY;
        h += dstY;
        dstY = 0;
    }
    if (dstX >= dst_w || dstY >= dst_h)
        return;
    if (dstX + w > dst_w)
        w = dst_w - dstX;
    if (dstY + h > dst_h)
        h = dst_h - dstY;
    if (w <= 0 || h <= 0)
        return;

    if ((dstX <= 205 && dstX + w >= 190) || h > 120)
        vm_lcd_image_trace("lcd_image api=vMDrawImageWithClipEx srcInfo=%08x srcPtr=%08x srcDim=%u,%u orig sx=%d sy=%d w=%d h=%d dx=%d dy=%d clipped sx=%d sy=%d w=%d h=%d dx=%d dy=%d last=%08x\n",
                           srcInfo, srcPtr, src_w, src_h, origSrcX, origSrcY, origW, origH, origDstX, origDstY,
                           srcX, srcY, w, h, dstX, dstY, lastAddress);

    int srcImgPitch = (((4 - src_w) & 3) + src_w) * 2; // 源图片的宽度按4像素对齐后再x2就是原图片一行像素占用的字节数
    int dstImgPicth = (((4 - dst_w) & 3) + dst_w) * 2;
    int copyBytes = w * 2;
    for (int i = 0; i < h; i++)
    {
        int srcOffset = (i + srcY) * srcImgPitch + srcX * 2;
        u32 dstScreenOffset = (i + dstY) * dstImgPicth + dstX * 2;

        uc_err r = uc_mem_read(MTK, srcPtr + srcOffset, tmpBuffer, copyBytes);
        if (r != UC_ERR_OK)
        {
            printf("读取内存错误");
            assert(0);
        }
        uc_mem_write(MTK, dstPtr + dstScreenOffset, tmpBuffer, copyBytes);
        if (dstPtr == VM_screenImage_ADDRESS && dst_w == LCD_WIDTH)
            memcpy(Lcd_Cache_Buffer + dstScreenOffset, tmpBuffer, copyBytes);
    }
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
    int dstInfo, srcInfo, srcX, srcY, w, h, dstX, dstY, sp, dstPtr, srcPtr;
    u16 dst_w, dst_h, src_w, src_h;
    uc_reg_read(MTK, UC_ARM_REG_R0, &dstInfo);
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
    uc_mem_read(MTK, dstInfo, &dstPtr, 4);
    uc_mem_read(MTK, dstInfo + 4, &dst_w, 2);
    uc_mem_read(MTK, dstInfo + 6, &dst_h, 2);
    if (dstInfo == VM_screenImageStruct_ADDRESS || dstPtr == 0 || dst_w == 0 || dst_h == 0 || dst_w > LCD_WIDTH || dst_h > LCD_HEIGHT)
    {
        dstPtr = VM_screenImage_ADDRESS;
        dst_w = LCD_WIDTH;
        dst_h = LCD_HEIGHT;
    }
    int origSrcX = srcX, origSrcY = srcY, origW = w, origH = h, origDstX = dstX, origDstY = dstY;
    u8 tmpBuffer[480];
    u8 dstBuffer[480];
    // printf("vMDrawImageClipAndAlphaEx(sx:%d,sy:%d,w:%d,h:%d,dx:%d,dy:%d)\n", srcX, srcY, w, h, dstX, dstY);
    if (srcPtr == 0 || w <= 0 || h <= 0 || src_w == 0 || src_h == 0)
        return;

    if (srcX < 0)
    {
        dstX -= srcX;
        w += srcX;
        srcX = 0;
    }
    if (srcY < 0)
    {
        dstY -= srcY;
        h += srcY;
        srcY = 0;
    }
    if (srcX >= src_w || srcY >= src_h)
        return;
    if (srcX + w > src_w)
        w = src_w - srcX;
    if (srcY + h > src_h)
        h = src_h - srcY;
    if (dstX < 0)
    {
        srcX -= dstX;
        w += dstX;
        dstX = 0;
    }
    if (dstY < 0)
    {
        srcY -= dstY;
        h += dstY;
        dstY = 0;
    }
    if (dstX >= dst_w || dstY >= dst_h)
        return;
    if (dstX + w > dst_w)
        w = dst_w - dstX;
    if (dstY + h > dst_h)
        h = dst_h - dstY;
    if (w <= 0 || h <= 0)
        return;

    if ((dstX <= 205 && dstX + w >= 190) || h > 120)
        vm_lcd_image_trace("lcd_image api=vMDrawImageClipAndAlphaEx srcInfo=%08x srcPtr=%08x srcDim=%u,%u orig sx=%d sy=%d w=%d h=%d dx=%d dy=%d clipped sx=%d sy=%d w=%d h=%d dx=%d dy=%d last=%08x\n",
                           srcInfo, srcPtr, src_w, src_h, origSrcX, origSrcY, origW, origH, origDstX, origDstY,
                           srcX, srcY, w, h, dstX, dstY, lastAddress);

    int srcImgPitch = (((4 - src_w) & 3) + src_w) * 2; // 源图片的宽度按4像素对齐后再x2就是原图片一行像素占用的字节数
    int dstImgPicth = (((4 - dst_w) & 3) + dst_w) * 2;
    int copyBytes = w * 2;
    int special_alpha_map = 0;

    // 下面先写“通用”行为（没有单独 alpha map）：像素值为 0 表示透明
    for (int row = 0; row < h; ++row)
    {
        int srcOffset = (row + srcY) * srcImgPitch + srcX * 2;
        u32 dstScreenOffset = (row + dstY) * dstImgPicth + dstX * 2;
        // if (dstScreenOffset > (dstImgPicth * 400 + srcImgPitch))
        // {
        //     printf("dst screen offset 错误");
        //     assert(0);
        // }
        uc_err r = uc_mem_read(MTK, srcPtr + srcOffset, tmpBuffer, copyBytes);
        if (r != UC_ERR_OK)
        {
            printf("读取内存错误");
            assert(0);
        }
        int dstIsScreen = (dstPtr == VM_screenImage_ADDRESS && dst_w == LCD_WIDTH);
        u16 *dstBasePtr = dstIsScreen ? ((u16 *)(Lcd_Cache_Buffer + dstScreenOffset)) : (u16 *)dstBuffer;
        if (!dstIsScreen)
            uc_mem_read(MTK, dstPtr + dstScreenOffset, dstBasePtr, copyBytes);
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
        uc_mem_write(MTK, dstPtr + dstScreenOffset, dstBasePtr, copyBytes);
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

    if (a1 == 0)
    {
        VM_RESOURCE_TRACE("img_create_stream null stream resultPtr=%08x\n", a2);
        return vm_set_call_result(0);
    }

    // n3 = *a1
    uc_mem_read(uc, a1, &n3, 1);
    // uc_mem_read(uc, a1, cbeTextString, 32); // debug
    u8 imageHead[8] = {0};
    uc_mem_read(uc, a1, imageHead, sizeof(imageHead));
    VM_RESOURCE_TRACE("img_create_stream stream=%08x resultPtr=%08x type=%u head=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                      a1, v3, n3, imageHead[0], imageHead[1], imageHead[2], imageHead[3],
                      imageHead[4], imageHead[5], imageHead[6], imageHead[7]);

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
    char buffer[1024 * 256]; // 由于不知道图片大小，预先读取64kb
    char ret[32];
    int mallocSize = 0;
    GifOutput p;
    memset(&p, 0, sizeof(p));
    uc_mem_read(MTK, gifBufferPtr, buffer, mySizeOf(buffer));

    int ok = gifDecodeExt(buffer, &p, 1, &mallocSize);
    if (!ok || p.pixels == NULL || p.width == 0 || p.height == 0 || mallocSize <= 0)
    {
        u32 dictSize = ((u32)(u8)buffer[0] << 24) | ((u32)(u8)buffer[1] << 16) |
                       ((u32)(u8)buffer[2] << 8) | (u32)(u8)buffer[3];
        vm_fileio_trace("gif_decode_fallback ptr=%08x dict=%u flags=%02x reason=%s head=%02x%02x%02x%02x%02x%02x%02x%02x\n",
                        gifBufferPtr, dictSize, (u8)buffer[4], gifDecodeGetLastError(),
                        (u8)buffer[0], (u8)buffer[1], (u8)buffer[2], (u8)buffer[3],
                        (u8)buffer[4], (u8)buffer[5], (u8)buffer[6], (u8)buffer[7]);
        u16 transparent = 0;
        int vmPtr = vm_malloc(sizeof(transparent));
        uc_mem_write(MTK, vmPtr, &transparent, sizeof(transparent));
        vm_img_result *vr = (vm_img_result *)ret;
        memset(ret, 0, sizeof(ret));
        vr->pixelsPtr = vmPtr;
        vr->width = 1;
        vr->height = 1;
        vr->need_free = 1;
        if (p.pixels)
            free_mem(p.pixels);
        uc_mem_write(MTK, resultPtr, ret, sizeof(vm_img_result));
        return vm_set_call_result(resultPtr);
    }

    int retSize = sizeof(vm_img_result);
    int vmPtr = vm_malloc(mallocSize);

    uc_mem_write(MTK, vmPtr, p.pixels, mallocSize);
    vm_img_result *vr = (vm_img_result *)ret;
    vr->height = p.height;
    vr->pixelsPtr = vmPtr;
    vr->width = p.width;
    vr->need_free = 1;
    free_mem(p.pixels);
    uc_mem_write(MTK, resultPtr, ret, retSize);
    return vm_set_call_result(resultPtr);
}

int vm_pngDecodeStart(int pngBufferPtr, int resultPtr)
{
    u8 hdr[8];
    uc_mem_read(MTK, pngBufferPtr, hdr, sizeof(hdr));

    u32 pngSize = ((u32)hdr[0] << 24) | ((u32)hdr[1] << 16) | ((u32)hdr[2] << 8) | hdr[3];
    u16 headerWidth = ((u16)hdr[4] << 8) | hdr[5];
    u16 headerHeight = ((u16)hdr[6] << 8) | hdr[7];
    if (pngSize == 0 || pngSize > 1024 * 1024)
    {
        printf("PNG资源长度异常:%u\n", pngSize);
        assert(0);
    }

    u8 *pngData = (u8 *)SDL_malloc(pngSize);
    if (!pngData)
    {
        printf("PNG临时内存申请失败:%u\n", pngSize);
        assert(0);
    }
    uc_mem_read(MTK, pngBufferPtr + 8, pngData, pngSize);

    int w = 0, h = 0, comp = 0;
    u8 *rgba = stbi_load_from_memory(pngData, (int)pngSize, &w, &h, &comp, 4);
    SDL_free(pngData);
    if (!rgba)
    {
        printf("PNG解码失败:%s\n", stbi_failure_reason());
        assert(0);
    }
    if (headerWidth && headerWidth != w)
        w = headerWidth;
    if (headerHeight && headerHeight != h)
        h = headerHeight;

    u32 pitchPixels = ((4 - (u32)w) & 3) + (u32)w;
    u32 pixelBytes = pitchPixels * (u32)h * 2;
    u32 vmPtr = vm_malloc(pixelBytes);
    u16 *rgb565 = (u16 *)SDL_malloc(pixelBytes);
    if (!rgb565)
    {
        stbi_image_free(rgba);
        printf("PNG像素内存申请失败:%u\n", pixelBytes);
        assert(0);
    }
    memset(rgb565, 0, pixelBytes);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            u8 *px = rgba + (y * w + x) * 4;
            u8 a = px[3];
            if (a == 0)
                continue;
            u8 r = px[0];
            u8 g = px[1];
            u8 b = px[2];
            rgb565[y * pitchPixels + x] = (u16)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
        }
    }

    uc_mem_write(MTK, vmPtr, rgb565, pixelBytes);
    SDL_free(rgb565);
    stbi_image_free(rgba);

    vm_img_result ret;
    memset(&ret, 0, sizeof(ret));
    ret.pixelsPtr = vmPtr;
    ret.width = (short)w;
    ret.height = (short)h;
    ret.need_free = 1;
    uc_mem_write(MTK, resultPtr, &ret, sizeof(ret));
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

static void vm_read_string_by_ptr_limited(u32 ptr, char *dst, size_t dstSize)
{
    if (dstSize == 0)
        return;

    size_t pos = 0;
    while (pos + 1 < dstSize)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, ptr + (u32)pos, &ch, 1) != UC_ERR_OK || ch == 0)
            break;
        dst[pos++] = (char)ch;
    }
    dst[pos] = 0;
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
    vm_configManagerTable(VM_MANAGER_TABLE_ADDRESS, VM_MANAGER_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_SYS_MANAGER_TABLE_ADDRESS, VM_SYS_MANAGER_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MEMORY_MANAGER_TABLE_ADDRESS, VM_MEMORY_MANAGER_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_GAMEOLD_TABLE_ADDRESS, VM_MANAGER_GAMEOLD_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_LCD_TABLE_ADDRESS, VM_MANAGER_LCD_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_FILEIO_TABLE_ADDRESS, VM_MANAGER_FILEIO_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_STDIO_TABLE_ADDRESS, VM_MANAGER_STDIO_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_TIMER_TABLE_ADDRESS, VM_MANAGER_TIMER_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_CTRL_TABLE_ADDRESS, VM_MANAGER_CTRL_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_NETWORK_TABLE_ADDRESS, VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_GAME_UTIL_TABLE_ADDRESS, VM_MANAGER_GAME_UTIL_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_DF_ENGINE_TABLE_ADDRESS, VM_MANAGER_DF_ENGINE_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_BILLING_TABLE_ADDRESS, VM_MANAGER_BILLING_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_UCS2_TABLE_ADDRESS, VM_MANAGER_UCS2_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_SCREEN_TABLE_ADDRESS, VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS);
    uc_mem_write(MTK, VM_MANAGER_DF_SCRIPT_TABLE_ADDRESS, emptyBuff, VM_MANAGER_TABLE_SIZE);
    vm_configManagerTable(VM_MANAGER_GAME_LCD_TABLE_ADDRESS, VM_MANAGER_GAME_LCD_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_NETAPP_TABLE_ADDRESS, VM_MANAGER_NETAPP_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_AUDIO_TABLE_ADDRESS, VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_SENSOR_TABLE_ADDRESS, VM_MANAGER_SENSOR_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_VMIM_TABLE_ADDRESS, VM_MANAGER_VMIM_FUNC_LIST_ADDRESS);
    vm_configManagerTable(VM_MANAGER_APPSTORE_TABLE_ADDRESS, VM_APPSTORE_FUNC_LIST_ADDRESS);
    vm_InitDlLoadManager(VM_DL_LOAD_MANAGER_ADDRESS);
    vm_configManagerTableCount(VM_DL_PAY_MANAGER_ADDRESS, VM_DL_PAY_FUNC_LIST_ADDRESS, 16);
    vm_InitDlRsManager(VM_DL_RS_MANAGER_ADDRESS);
    vm_InitDlImageManager(VM_DL_IMAGE_MANAGER_ADDRESS);
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
            u32 dataBase = 0;
            u32 offsetBase = 0;
            uc_mem_read(MTK, DataPackage + 16, &dataBase, 4);
            uc_mem_read(MTK, DataPackage + 24, &offsetBase, 4);
            uc_mem_read(MTK, dataBase + 4 * a1, &tmp1, 4);
            Data = tmp1 + offsetBase;
        }
        VM_RESOURCE_TRACE("img_create_res id=%u dataPackage=%08x flag=%u data=%08x\n", a1, DataPackage, tmp2, Data);
        return vm_IMG_CreateImageFormStream(Data, 0);
    }
    VM_RESOURCE_TRACE("img_create_res no_datapackage id=%u\n", a1);
    return vm_set_call_result(tmp1);
}
// ok
int vm_DF_ReadString(u32 a1, u32 a2)
{
    u32 idx = 0;
    u32 startIdx = 0;
    u8 len = 0;
    u32 result = 0;
    u8 ch = 0;
    u32 i = 0;
    u8 zero = 0;
    uc_engine *uc = MTK;
    // idx = *a2
    // uc_mem_read(MTK, a1, cbeTextString, 64);//debug
    uc_mem_read(uc, a2, &idx, 4);
    startIdx = idx;

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
    if (len > 0)
    {
        char outName[128] = {0};
        u8 srcBytes[96] = {0};
        u32 outLen = len < sizeof(outName) - 1 ? len : (u32)sizeof(outName) - 1;
        uc_mem_read(uc, result, outName, outLen);
        if (strstr(outName, "warrior") || strstr(outName, "rior") || strstr(outName, "jian") ||
            strstr(outName, "guang") || strstr(outName, "death") || strstr(outName, "ying"))
        {
            u32 srcLen = len + 1 < sizeof(srcBytes) ? len + 1 : (u32)sizeof(srcBytes);
            uc_mem_read(uc, a1 + startIdx, srcBytes, srcLen);
            vm_fileio_trace("df_read_string base=%08x off=%u len=%u out=%08x text=%s next=%u last=%08x\n",
                            a1, startIdx, len, result, outName, vm_get_var(a2), lastAddress);
            vm_fileio_trace_bytes("df_read_string_src", -1, outName, srcBytes, srcLen);
        }
    }
    return vm_set_call_result(result);
    // vm_readStringByPtr(result, sprintfBuff);
    // printf("DF_ReadString(adr:%x)\n", result);
    return result;
}

int vm_LzssDecode_old(u32 a1, u32 a2, u32 dest_2, u32 sizePtr)
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

int vm_LzssDecode(int srcPtr, int srcLen, int destPtr, int a4Ptr)
{
    unsigned int src_pos = 0;
    unsigned int dest_pos = 0;

    /* src end = srcPtr + srcLen - 1 (we compare offsets) */
    unsigned int src_max_pos = (srcLen > 0) ? (unsigned int)(srcLen - 1) : 0;

    /* dest buffer max length is stored at *a4 */
    int dest_max_len = vm_get_var(a4Ptr); /* *a4 */
    unsigned int dest_max_pos = (dest_max_len > 0) ? (unsigned int)(dest_max_len - 1) : 0;

    while (src_pos <= src_max_pos && dest_pos <= dest_max_pos)
    {
        unsigned int v7 = (unsigned int)vm_get_var_byte(srcPtr + src_pos); /* (unsigned __int8)*v4 */

        if (v7 >> 7) /* top bit set -> literal copy */
        {
            int n3 = (int)(v7 & 0x7F);
            /* available space in dest */
            int avail = (int)(dest_max_len - dest_pos);
            if (n3 > avail)
                n3 = avail;
            if (n3 <= 0)
                break;

            /* copy n3 bytes from src+src_pos+1 to dest+dest_pos */
            vm_memcpy(destPtr + dest_pos, srcPtr + src_pos + 1, n3);
            src_pos += (unsigned int)(n3 + 1);
            dest_pos += (unsigned int)n3;
        }
        else /* back-reference copy */
        {
            unsigned int n3_1 = v7 >> 1;
            int avail = (int)(dest_max_len - dest_pos);
            if ((int)n3_1 > avail)
                n3_1 = (unsigned int)avail;
            if ((int)n3_1 <= 0)
                break;

            /* distance = ((v7 << 8) & 0x1FF) | (unsigned __int8)v4[1] */
            unsigned int next_byte = vm_get_var_byte(srcPtr + src_pos + 1);
            unsigned int distance = (((v7 << 8) & 0x1FF) | next_byte);

            /* source address = dest - distance */
            int copy_src_vm = destPtr + dest_pos - (int)distance;
            for (unsigned int i = 0; i < n3_1; ++i)
            {
                u8 b = vm_get_var_byte(copy_src_vm + i);
                vm_set_var_byte(destPtr + dest_pos + i, b);
            }

            src_pos += 2;
            dest_pos += n3_1;
        }

        /* recompute end-conditions in case we consumed buffers */
        if (src_pos > src_max_pos || dest_pos > dest_max_pos)
            break;
    }

    /* result length = dest_pos */
    vm_set_var(a4Ptr, (int)dest_pos); /* *a4 = dest - dest_2; */

    /* return the decoded length as call result */
    return vm_set_call_result((int)dest_pos);
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
    u32 ptr = vm_malloc_var();
    vm_set_var(ptr, v8);
    vm_LzssDecode(a1 + 9, v5, dest, ptr);
    v8 = vm_get_var(ptr);
    vm_free_var(ptr);
    if ((lastAddress >= 0x0100d530 && lastAddress <= 0x0100d590) ||
        (lastAddress >= 0x0100db80 && lastAddress <= 0x0100de90))
    {
        u8 head[32] = {0};
        u32 dumpLen = v8 < sizeof(head) ? v8 : (u32)sizeof(head);
        if (dumpLen > 0)
            uc_mem_read(MTK, dest, head, dumpLen);
        vm_fileio_trace("GetStreamDataFormRes src=%08x type=%u comp=%u outCap=%u out=%u dest=%08x head=%02x%02x%02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x%02x%02x last=%08x\n",
                        a1,
                        vm_get_var_byte(a1),
                        v5,
                        (u32)b8 | ((u32)b7 << 8) | ((u32)b6 << 16) | ((u32)b5 << 24),
                        v8,
                        dest,
                        head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7],
                        head[8], head[9], head[10], head[11], head[12], head[13], head[14], head[15],
                        lastAddress);
    }
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
    u8 charBuffer[1024];
    vm_readStringByPtr(src, charBuffer);
    uc_mem_write(MTK, dst, charBuffer, strlen(charBuffer) + 1);
}

int vm_DF_Sin(int deg)
{
    if (deg < 0)
        deg += 360;
    double rad = deg * 3.14159265358979323846 / 180.0;
    double result = sin(rad);
    int r = (int)(result * 4096);
    return vm_set_call_result(r);
}

void vm_configManagerTable(u32 tableAddr, u32 funcAddr)
{
    u32 tmp, i;
    for (i = 0; i < (VM_MANAGER_FUNC_LIST_SIZE / 4); i++)
    {
        tmp = funcAddr + i * 4;
        uc_mem_write(MTK, tableAddr + i * 4, &tmp, 4);
    }
}

void vm_configManagerTableCount(u32 tableAddr, u32 funcAddr, u32 count)
{
    u32 tmp, i;
    if (tableAddr == 0)
        return;
    for (i = 0; i < count; i++)
    {
        tmp = funcAddr + i * 4;
        uc_mem_write(MTK, tableAddr + i * 4, &tmp, 4);
    }
}
void vm_InitDlLoadManager(u32 tmp1)
{
    u32 tmp2 = VM_DL_LOAD_FUNC_LIST_ADDRESS;
    vm_set_var(tmp1, tmp2);
    vm_set_var(tmp1 + 4, tmp2 + 4);
    vm_set_var(tmp1 + 8, tmp2 + 8);
    vm_set_var(tmp1 + 12, tmp2 + 12);
    vm_set_var(tmp1 + 16, tmp2 + 16);
    vm_set_var(tmp1 + 20, tmp2 + 20);
    vm_set_var(tmp1 + 24, tmp2 + 24);
    vm_set_var(tmp1 + 28, tmp2 + 28);
    vm_set_var(tmp1 + 32, tmp2 + 32);
    vm_set_var(tmp1 + 36, tmp2 + 36);
    vm_set_var(tmp1 + 40, tmp2 + 40);
}

void vm_InitDlRsManager(u32 tmp1)
{
    u32 tmp2 = VM_DL_RS_FUNC_LIST_ADDRESS;
    for (u32 i = 0; i < 20; ++i)
        vm_set_var(tmp1 + i * 4, tmp2 + i * 4);
}

void vm_InitDlImageManager(u32 tmp1)
{
    u32 tmp2 = VM_DL_IMAGE_FUNC_LIST_ADDRESS;
    for (u32 i = 0; i < 12; ++i)
        vm_set_var(tmp1 + i * 4, tmp2 + i * 4);
}
