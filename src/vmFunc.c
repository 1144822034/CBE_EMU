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

static bool vm_uses_be_fixed_df_layout(void)
{
    return g_cbeInfo.isBiggianProgram && g_cbeInfo.headerInt1 != 0 &&
           g_cbeInfo.headerInt2 > g_cbeInfo.codeLen;
}

static u32 vm_df_package_struct_size(void)
{
    return vm_uses_be_fixed_df_layout() ? 80u : 108u;
}

static void vm_read_string_by_ptr_limited(u32 ptr, char *dst, size_t dstSize);
static void vm_read_path_string(u32 namePtr, char *out, size_t outSize);

#define VM_STRING_READ_FALLBACK_SIZE 1024u
#if defined(__GNUC__)
#define VM_STRING_READ_OBJECT_SIZE(dst) __builtin_object_size((dst), 0)
#define VM_STRING_READ_SIZE(dst) \
    (VM_STRING_READ_OBJECT_SIZE(dst) == (size_t)-1 ? VM_STRING_READ_FALLBACK_SIZE : VM_STRING_READ_OBJECT_SIZE(dst))
#else
#define VM_STRING_READ_SIZE(dst) VM_STRING_READ_FALLBACK_SIZE
#endif

static void vm_readStringByRegLimited(uc_arm_reg reg, u8 *dst, size_t dstSize);
static void vm_readStringByPtrLimited(u32 ptr, u8 *dst, size_t dstSize);
static void vm_readStringGbkByRegLimited(uc_arm_reg reg, u8 *dst, size_t dstSize);
static void vm_readStringUCS2ByRegLimited(uc_arm_reg reg, u16 *dst, size_t dstBytes);

#define vm_readStringByReg(reg, dst) vm_readStringByRegLimited((reg), (u8 *)(dst), VM_STRING_READ_SIZE(dst))
#define vm_readStringByPtr(ptr, dst) vm_readStringByPtrLimited((ptr), (u8 *)(dst), VM_STRING_READ_SIZE(dst))
#define vm_readStringGbkByReg(reg, dst) vm_readStringGbkByRegLimited((reg), (u8 *)(dst), VM_STRING_READ_SIZE(dst))
#define vm_readStringUCS2ByReg(reg, dst) vm_readStringUCS2ByRegLimited((reg), (u16 *)(dst), VM_STRING_READ_SIZE(dst))

#define VM_C_STRING_SCAN_MAX (64u * 1024u)

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
u32 vm_MF_MemoryBlock_Malloc(int a1, int a2);

u32 vm_malloc(u32 size);
void vm_free(u32 addr);
u32 vm_malloc_user_size(u32 addr);
int vm_strlen(int addr);
int vm_memcpy(int dstAddr, int srcAddr, int len);
u8 vm_DF_DataPackage_GetPackageIndex(int a1);
int vm_DF_DataPackage_LoadPackage(int a1, int a2);
int vm_DF_DataPackage_InitTxt(int a1, int a2);
int vm_DF_DataPackage_ReleasePackage(int a1, int a2);

u16 vm_DF_ReadShort(u32 bufPtr, u32 offsetPtr);
u32 vm_DF_WriteShort(u32 bufPtr, u32 offsetPtr, u32 value);
int vm_DF_ReadInt(int a1, int a2);
u32 vm_DF_WriteInt(u32 bufPtr, u32 offsetPtr, u32 value);

int vm_DF_Malloc_IN(int a1, int a2);
int vm_DF_ReadString2(u32 bufferPtr, u32 offsetPtr);
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
int vm_cbfs_vm_file_rename(int disk, int oldNamePtr, int newNamePtr);
u32 vm_initDFDataPackage(u32 a1, u32 a2);
u32 vm_IMG_InitDataPage(u32 a1, u32 a2);
u32 vm_IMG_InitInnerDataPageEx(u32 a1, u32 a2);
u32 vm_IMG_InitDataPageEx(u32 a1, u32 a2, u32 dataPackage);
u32 vm_IMG_ReleaseDataPage(void);
u32 vm_IMG_InitDataPageTxt(u32 textId);
u32 vm_IMG_CreateImageFormIdEx(u32 imageId, u32 dataPackage, u32 outImage);
u32 vm_IMG_CreateImageFormResForVm(u32 imageId, u32 outImage);
u32 vm_IMG_CreateImageFormRes(u32 imageId);
void vm_sprintf();
void vm_DF_GetFormatString();
u32 vm_DF_GetMemoryBlock(void);
u32 vm_set_call_result(u32 r);
void vm_initManagerTable();
void vm_configManagerTable(u32 a, u32 b);
void vm_configManagerTableCount(u32 tableAddr, u32 funcAddr, u32 count);
void vm_InitDlWPayManager(u32 tmp1);
void vm_InitDlLoadManager(u32 tmp1);
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
    u8 b[4];
    u32 a;
    uc_mem_read(MTK, addr, b, 4);
    if (g_cbeInfo.isBiggianProgram)
        a = ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | (u32)b[3];
    else
        a = ((u32)b[3] << 24) | ((u32)b[2] << 16) | ((u32)b[1] << 8) | (u32)b[0];
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
    u8 b[4];
    if (g_cbeInfo.isBiggianProgram)
    {
        b[0] = (u8)(v >> 24);
        b[1] = (u8)(v >> 16);
        b[2] = (u8)(v >> 8);
        b[3] = (u8)v;
    }
    else
    {
        b[0] = (u8)v;
        b[1] = (u8)(v >> 8);
        b[2] = (u8)(v >> 16);
        b[3] = (u8)(v >> 24);
    }
    uc_mem_write(MTK, addr, b, 4);
}
inline u16 vm_get_var_short(u32 addr)
{
    u8 b[2];
    u16 a;
    uc_mem_read(MTK, addr, b, 2);
    if (g_cbeInfo.isBiggianProgram)
        a = (u16)(((u16)b[0] << 8) | b[1]);
    else
        a = (u16)(((u16)b[1] << 8) | b[0]);
    return a;
}

inline void vm_set_var_short(u32 addr, u16 v)
{
    u8 b[2];
    if (g_cbeInfo.isBiggianProgram)
    {
        b[0] = (u8)(v >> 8);
        b[1] = (u8)v;
    }
    else
    {
        b[0] = (u8)v;
        b[1] = (u8)(v >> 8);
    }
    uc_mem_write(MTK, addr, b, 2);
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

static u32 vm_guest_strlen(u32 addr)
{
    u32 len = 0;
    while (len < VM_C_STRING_SCAN_MAX)
    {
        u8 ch = 0;
        if (uc_mem_read(MTK, addr + len, &ch, 1) != UC_ERR_OK || ch == 0)
            break;
        ++len;
    }
    return len;
}

static u32 vm_guest_strcpy(u32 dst, u32 src)
{
    u32 copied = 0;
    u8 chunk[128];

    while (copied < VM_C_STRING_SCAN_MAX)
    {
        u32 count = 0;
        while (count < sizeof(chunk) && copied + count < VM_C_STRING_SCAN_MAX)
        {
            u8 ch = 0;
            if (uc_mem_read(MTK, src + copied + count, &ch, 1) != UC_ERR_OK)
                ch = 0;
            chunk[count++] = ch;
            if (ch == 0)
            {
                uc_mem_write(MTK, dst + copied, chunk, count);
                return copied + count - 1;
            }
        }
        uc_mem_write(MTK, dst + copied, chunk, count);
        copied += count;
    }

    {
        u8 zero = 0;
        uc_mem_write(MTK, dst + copied, &zero, 1);
    }
    return copied;
}

// ok
int vm_strlen(int addr)
{
    int len = (int)vm_guest_strlen((u32)addr);
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

static int vm_path_looks_like_ascii_c_string(const u8 *raw, size_t rawSize)
{
    size_t len = 0;
    if (raw == NULL || rawSize == 0)
        return 0;
    while (len < rawSize && raw[len] != 0)
    {
        if (raw[len] < 0x20 || raw[len] > 0x7e)
            return 0;
        len++;
    }
    return len >= 2 && len < rawSize;
}

static int vm_path_looks_like_ucs2_le(const u8 *raw, size_t rawSize, u32 *ucs2LenOut)
{
    if (ucs2LenOut)
        *ucs2LenOut = 0;
    if (raw == NULL || rawSize < 4)
        return 0;
    if (vm_path_looks_like_ascii_c_string(raw, rawSize))
        return 0;

    u32 pos = 0;
    u32 units = 0;
    int sawWideChar = 0;
    while (pos + 1 < rawSize)
    {
        u16 ch = (u16)(raw[pos] | (raw[pos + 1] << 8));
        if (ch == 0)
        {
            if (units == 0)
                return 0;
            if (ucs2LenOut)
                *ucs2LenOut = pos;
            return 1;
        }

        if (ch >= 0xD800 && ch <= 0xDFFF)
            return 0;

        if (ch <= 0x7F)
        {
            if (!(ch == '/' || ch == '\\' || ch == '.' || ch == '_' || ch == '-' || ch == '+' ||
                  (ch >= '0' && ch <= '9') ||
                  (ch >= 'A' && ch <= 'Z') ||
                  (ch >= 'a' && ch <= 'z')))
                return 0;
        }
        else
        {
            /* Accept common CJK/unicode BMP path characters used by the game. */
            if (!((ch >= 0x2E80 && ch <= 0x9FFF) || (ch >= 0xF900 && ch <= 0xFAFF)))
                return 0;
            sawWideChar = 1;
        }

        pos += 2;
        ++units;
    }

    return sawWideChar;
}

static void vm_read_path_string(u32 namePtr, char *out, size_t outSize)
{
    u8 rawName[256];
#ifdef CBE_HOST_UTF8_PATHS
    char gbkPath[256];
#endif
    memset(out, 0, outSize);
#ifdef CBE_PLATFORM_ANDROID
    memset(gbkPath, 0, sizeof(gbkPath));
#endif
    if (outSize == 0)
        return;
    uc_mem_read(MTK, namePtr, rawName, sizeof(rawName));
    u32 ucs2Len = 0;
    if (vm_path_looks_like_ucs2_le(rawName, sizeof(rawName), &ucs2Len))
    {
#ifdef CBE_PLATFORM_ANDROID
        ucs2_to_gbk(rawName, ucs2Len, (u8 *)gbkPath, sizeof(gbkPath));
#else
        ucs2_to_gbk(rawName, ucs2Len, out, outSize);
#endif
    }
    else
    {
#ifdef CBE_PLATFORM_ANDROID
        vm_read_string_by_ptr_limited(namePtr, gbkPath, sizeof(gbkPath));
#else
        vm_read_string_by_ptr_limited(namePtr, out, outSize);
#endif
    }
#ifdef CBE_PLATFORM_ANDROID
    gbk_to_utf8((u8 *)gbkPath, (u8 *)out, outSize);
#endif
    out[outSize - 1] = 0;
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
    if (strstr(nameBuf, "tempbin") != NULL || strstr(nameBuf, "Tempbin") != NULL || strstr(nameBuf, "TEMPBIN") != NULL)
        return 1;
    if (ext == NULL)
        return 0;
    return _stricmp(ext, ".cbe") == 0 ||
           _stricmp(ext, ".bin") == 0 ||
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

static int vm_file_is_read_only_mode(const char *mode)
{
    if (mode == NULL)
        return 1;
    return strchr(mode, 'r') != NULL && !vm_file_mode_is_writeable(mode);
}

static int vm_file_has_extension(const char *path)
{
    const char *slash;
    const char *backslash;
    const char *dot;
    if (path == NULL)
        return 0;
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
    dot = strrchr(path, '.');
    return dot != NULL && (slash == NULL || dot > slash);
}

static int vm_path_has_separator(const char *path)
{
    return path != NULL && (strchr(path, '/') != NULL || strchr(path, '\\') != NULL);
}

static const char *vm_path_basename(const char *path)
{
    const char *slash;
    const char *backslash;
    if (path == NULL)
        return "";
    slash = strrchr(path, '/');
    backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
    return slash ? slash + 1 : path;
}

static int vm_file_is_bare_dsh_resource(const char *path)
{
    const char *ext;
    if (path == NULL || path[0] == 0 || vm_path_has_separator(path))
        return 0;
    ext = strrchr(path, '.');
    return ext != NULL && _stricmp(ext, ".dsh") == 0;
}

static int vm_file_is_bare_cbe_module(const char *path)
{
    const char *ext;
    if (path == NULL || path[0] == 0 || vm_path_has_separator(path))
        return 0;
    ext = strrchr(path, '.');
    return ext != NULL && (_stricmp(ext, ".cbm") == 0 || _stricmp(ext, ".cbe") == 0);
}

#define VM_RELEASED_RESOURCE_MAX 1024
#define VM_RELEASED_RESOURCE_ID_BASE 0x7000u

typedef struct
{
    char name[128];
    char path[256];
    u32 id;
} VmReleasedResource;

static VmReleasedResource g_releasedResources[VM_RELEASED_RESOURCE_MAX];
static u32 g_releasedResourceCount = 0;

static int vm_resource_name_matches(const char *storedName, const char *queryName)
{
    char stored[128];
    char query[128];
    if (storedName == NULL || queryName == NULL || storedName[0] == 0 || queryName[0] == 0)
        return 0;
    vm_file_normalize_host_path(storedName, stored, sizeof(stored));
    vm_file_normalize_host_path(queryName, query, sizeof(query));
    if (_stricmp(stored, query) == 0)
        return 1;
    if (!vm_path_has_separator(query))
        return _stricmp(vm_path_basename(stored), query) == 0;
    return _stricmp(vm_path_basename(stored), vm_path_basename(query)) == 0;
}

static VmReleasedResource *vm_resource_cache_find_by_name(const char *name)
{
    for (u32 i = 0; i < g_releasedResourceCount; i++)
    {
        if (vm_resource_name_matches(g_releasedResources[i].name, name) ||
            (g_releasedResources[i].path[0] &&
             vm_resource_name_matches(g_releasedResources[i].path, name)))
        {
            return &g_releasedResources[i];
        }
    }
    return NULL;
}

static VmReleasedResource *vm_resource_cache_find_by_id(u32 id)
{
    for (u32 i = 0; i < g_releasedResourceCount; i++)
    {
        if (g_releasedResources[i].id == id)
            return &g_releasedResources[i];
    }
    return NULL;
}

static VmReleasedResource *vm_resource_cache_note_name(const char *name)
{
    char normalized[128];
    VmReleasedResource *entry;
    if (name == NULL || name[0] == 0)
        return NULL;

    entry = vm_resource_cache_find_by_name(name);
    if (entry)
        return entry;

    if (g_releasedResourceCount >= VM_RELEASED_RESOURCE_MAX)
        return NULL;

    entry = &g_releasedResources[g_releasedResourceCount];
    memset(entry, 0, sizeof(*entry));
    vm_file_normalize_host_path(name, normalized, sizeof(normalized));
    snprintf(entry->name, sizeof(entry->name), "%s", normalized[0] ? normalized : name);
    entry->id = VM_RELEASED_RESOURCE_ID_BASE + g_releasedResourceCount;
    g_releasedResourceCount++;
    return entry;
}

static void vm_resource_cache_note_path(const char *path)
{
    VmReleasedResource *entry;
    const char *baseName;
    char normalized[256];
    if (path == NULL || path[0] == 0 || !vm_file_has_extension(path))
        return;

    vm_file_normalize_host_path(path, normalized, sizeof(normalized));
    baseName = vm_path_basename(normalized);
    if (baseName[0] == 0)
        return;

    entry = vm_resource_cache_note_name(baseName);
    if (entry)
        snprintf(entry->path, sizeof(entry->path), "%s", normalized);
}

static int vm_resource_host_file_exists(const char *path)
{
    FILE *fp;
    if (path == NULL || path[0] == 0)
        return 0;
    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;
    fclose(fp);
    return 1;
}

static u32 vm_resource_cache_lookup_id(const char *name)
{
    VmReleasedResource *entry = vm_resource_cache_find_by_name(name);
    if (entry)
        return entry->id;
    if (vm_resource_host_file_exists(name))
    {
        entry = vm_resource_cache_note_name(name);
        if (entry)
        {
            snprintf(entry->path, sizeof(entry->path), "%s", name);
            return entry->id;
        }
    }
    return (u32)-1;
}

static u32 vm_resource_cache_load_file(const char *path)
{
    FILE *fp;
    long fileSize;
    u8 header[4] = {0};
    u32 declaredLen;
    u32 payloadOffset = 0;
    u32 payloadLen;
    u8 *hostBuf;
    u32 vmBuf;

    if (path == NULL || path[0] == 0)
        return 0;

    fp = fopen(path, "rb");
    if (fp == NULL)
        return 0;

    if (fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return 0;
    }
    fileSize = ftell(fp);
    if (fileSize <= 0 || fileSize > 16 * 1024 * 1024)
    {
        fclose(fp);
        return 0;
    }
    fseek(fp, 0, SEEK_SET);

    payloadLen = (u32)fileSize;
    if (fileSize >= 4 && fread(header, 1, sizeof(header), fp) == sizeof(header))
    {
        declaredLen = (u32)header[0] |
                      ((u32)header[1] << 8) |
                      ((u32)header[2] << 16) |
                      ((u32)header[3] << 24);
        if (declaredLen > 0 && declaredLen <= (u32)fileSize - 4)
        {
            payloadOffset = 4;
            payloadLen = declaredLen;
        }
    }

    hostBuf = SDL_malloc(payloadLen);
    if (hostBuf == NULL)
    {
        fclose(fp);
        return 0;
    }
    fseek(fp, payloadOffset, SEEK_SET);
    if (fread(hostBuf, 1, payloadLen, fp) != payloadLen)
    {
        SDL_free(hostBuf);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    vmBuf = vm_malloc(payloadLen);
    uc_mem_write(MTK, vmBuf, hostBuf, payloadLen);
    SDL_free(hostBuf);
    return vmBuf;
}

static u32 vm_resource_cache_load_by_name(const char *name)
{
    VmReleasedResource *entry = vm_resource_cache_find_by_name(name);
    if (entry && entry->path[0])
        return vm_resource_cache_load_file(entry->path);
    if (name && vm_resource_host_file_exists(name))
        return vm_resource_cache_load_file(name);
    return 0;
}

static u32 vm_resource_cache_load_by_id(u32 id)
{
    VmReleasedResource *entry = vm_resource_cache_find_by_id(id);
    if (entry == NULL)
        return 0;
    return vm_resource_cache_load_by_name(entry->name);
}

static void vm_resource_cache_note_package(u32 dataPackage)
{
    u16 count;
    u32 nameTable;
    if (dataPackage == 0)
        return;
    count = vm_get_var_short(dataPackage + 8);
    nameTable = vm_get_var(dataPackage + 12);
    for (u32 i = 0; nameTable && i < count; i++)
    {
        u32 namePtr = vm_get_var(nameTable + 4 * i);
        char name[128];
        if (namePtr == 0)
            continue;
        vm_read_path_string(namePtr, name, sizeof(name));
        vm_resource_cache_note_name(name);
    }
}

static int vm_path_has_high_byte(const char *path)
{
    if (path == NULL)
        return 0;
    while (*path)
    {
        if ((u8)*path >= 0x80)
            return 1;
        ++path;
    }
    return 0;
}

static int vm_file_is_invalid_named_resource_cache(const char *normalizedName, const char *mode)
{
    FILE *fp;
    long fileSize = 0;
    u8 header[4] = {0};
    u32 declaredLen = 0;

    if (normalizedName == NULL || normalizedName[0] == 0)
        return 0;
    if (!vm_file_is_read_only_mode(mode))
        return 0;
    if (strncmp(normalizedName, "JHOnlineData/", 13) != 0)
        return 0;
    if (vm_file_has_extension(normalizedName))
        return 0;
    if (!vm_path_has_high_byte(normalizedName + 13))
        return 0;

    fp = fopen(normalizedName, "rb");
    if (fp == NULL)
        return 0;
    if (fseek(fp, 0, SEEK_END) == 0)
        fileSize = ftell(fp);
    if (fileSize >= 4)
    {
        fseek(fp, 0, SEEK_SET);
        if (fread(header, 1, sizeof(header), fp) != sizeof(header))
            fileSize = -1;
    }
    fclose(fp);

    if (fileSize < 4)
        return 1;

    declaredLen = (u32)header[0] |
                  ((u32)header[1] << 8) |
                  ((u32)header[2] << 16) |
                  ((u32)header[3] << 24);
    return declaredLen == 0 || declaredLen > (u32)(fileSize - 4);
}

static int vm_file_try_resolve_map_path(const char *normalizedName, const char *mode, char *resolvedName, size_t resolvedSize)
{
    FILE *fp;
    const char *baseName;
    char sceneName[256];
    if (normalizedName == NULL || resolvedName == NULL || resolvedSize == 0)
        return 0;
    if (!vm_file_is_read_only_mode(mode) || vm_file_has_extension(normalizedName))
        return 0;
    if (strncmp(normalizedName, "JHOnlineData/", 13) != 0)
        return 0;
    baseName = normalizedName + 13;
    if ((baseName[0] == 'c' || baseName[0] == 'b') &&
        snprintf(sceneName, sizeof(sceneName), "%s.sce", normalizedName) < (int)sizeof(sceneName))
    {
        fp = fopen(sceneName, "rb");
        if (fp != NULL)
        {
            fclose(fp);
            snprintf(resolvedName, resolvedSize, "%s", sceneName);
            return 1;
        }
    }
    if (snprintf(resolvedName, resolvedSize, "%s.map", normalizedName) >= (int)resolvedSize)
        return 0;
    fp = fopen(resolvedName, "rb");
    if (fp == NULL)
        return 0;
    fclose(fp);
    return 1;
}

static int vm_file_try_resolve_jhonline_dsh_path(const char *normalizedName, const char *mode, char *resolvedName, size_t resolvedSize)
{
    static const char *pathFormats[] = {
        "JHOnlineData/%s",
        "../web/fs/JHOnlineData/%s",
        "web/fs/JHOnlineData/%s"
    };
    if (normalizedName == NULL || resolvedName == NULL || resolvedSize == 0)
        return 0;
    if (!vm_file_is_read_only_mode(mode) || !vm_file_is_bare_dsh_resource(normalizedName))
        return 0;
    for (size_t i = 0; i < sizeof(pathFormats) / sizeof(pathFormats[0]); ++i)
    {
        FILE *fp;
        if (snprintf(resolvedName, resolvedSize, pathFormats[i], normalizedName) >=
            (int)resolvedSize)
        {
            continue;
        }
        fp = fopen(resolvedName, "rb");
        if (fp == NULL)
            continue;
        fclose(fp);
        return 1;
    }
    return 0;
}

static int vm_file_try_resolve_cbe_module_path(const char *normalizedName, const char *mode, char *resolvedName, size_t resolvedSize)
{
    FILE *fp;
    if (normalizedName == NULL || resolvedName == NULL || resolvedSize == 0)
        return 0;
    if (!vm_file_is_read_only_mode(mode) || !vm_file_is_bare_cbe_module(normalizedName))
        return 0;
    if (snprintf(resolvedName, resolvedSize, "CBE/%s", normalizedName) >= (int)resolvedSize)
        return 0;
    fp = fopen(resolvedName, "rb");
    if (fp == NULL)
        return 0;
    fclose(fp);
    return 1;
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
            if (ch == 'r' || ch == 'w' || ch == 'a')
            {
                if (pos == 0)
                    hint[pos++] = ch;
                else
                    break;
            }
            else if ((ch == 'b' || ch == '+') && pos > 0)
            {
                if (strchr(hint, ch) == NULL)
                    hint[pos++] = ch;
            }
            else
            {
                break;
            }
        }
        if (pos)
        {
            if (strchr(hint, 'b') == NULL && pos + 1 < sizeof(hint))
                hint[pos++] = 'b';
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
    char resolvedName[256];
    const char *openMode = mode;
    vm_file_normalize_host_path(nameBuf, normalizedName, sizeof(normalizedName));
    if (normalizedName[0] == 0)
        snprintf(normalizedName, sizeof(normalizedName), "%s", nameBuf);
    if (vm_file_try_resolve_map_path(normalizedName, openMode, resolvedName, sizeof(resolvedName)))
    {
        snprintf(normalizedName, sizeof(normalizedName), "%s", resolvedName);
    }
    if (vm_file_ext_requires_binary(normalizedName) && mode && strchr(mode, 'b') == NULL)
    {
        snprintf(binaryMode, sizeof(binaryMode), "%sb", mode);
        openMode = binaryMode;
    }
    if (vm_file_is_invalid_named_resource_cache(normalizedName, openMode))
    {
        return -1;
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
            if (f == NULL && vm_file_try_resolve_cbe_module_path(normalizedName, openMode, resolvedName, sizeof(resolvedName)))
            {
                f = fopen(resolvedName, openMode);
                if (f != NULL)
                    snprintf(normalizedName, sizeof(normalizedName), "%s", resolvedName);
            }
            if (f == NULL && vm_file_try_resolve_jhonline_dsh_path(normalizedName, openMode, resolvedName, sizeof(resolvedName)))
            {
                f = fopen(resolvedName, openMode);
                if (f != NULL)
                    snprintf(normalizedName, sizeof(normalizedName), "%s", resolvedName);
            }
            if (f == NULL && vm_file_mode_is_writeable(openMode))
            {
                vm_file_ensure_parent_dirs(normalizedName);
                f = fopen(normalizedName, openMode);
                if (f == NULL)
                    f = fopen(normalizedName, "wb+");
            }
            if (f == NULL)
            {
                return -1;
            }
            openFileList[i] = f;
            snprintf(openFileNames[i], sizeof(openFileNames[i]), "%s", normalizedName);
            if (vm_file_has_extension(normalizedName))
                vm_resource_cache_note_path(normalizedName);
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
    return vm_set_call_result(r);
}

// ok
int vm_cbfs_vm_file_open(int openMode, int namePtr, int rwPtr)
{
    char rwBuff[128] = {0};
    char nameBuff[256];
    char mode[8];
    u8 rawName[256];
    u32 pc = 0, lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    uc_mem_read(MTK, namePtr, rawName, sizeof(rawName));
    vm_read_path_string(namePtr, nameBuff, sizeof(nameBuff));
    // printf("file_open=%s\n",nameBuff);
    if (rwPtr)
        uc_mem_read(MTK, rwPtr, &rwBuff, 128);
    rwBuff[sizeof(rwBuff) - 1] = 0;
    vm_file_select_mode(openMode, rwBuff, mode, sizeof(mode));
    int handle = vm_get_file_handle(nameBuff, mode);
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
        for (u32 i = 4; i <= 6; ++i)
        {
            if ((r[i] >= Program_ROM_Address && r[i] < Program_ROM_Address + Program_ROM_Mapped_Size) ||
                (r[i] >= VM_Memory_Pool_ADDRESS && r[i] < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE) ||
                (r[i] >= STACK_ADDRESS && r[i] < STACK_ADDRESS + 0x100000))
            {
                u8 bytes[96] = {0};
                if (uc_mem_read(MTK, r[i], bytes, sizeof(bytes)) == UC_ERR_OK)
                {
                    char tag[64];
                    snprintf(tag, sizeof(tag), "file_open_fail_mem_r%u_%08x", i, r[i]);
                }
            }
        }
    }
    return vm_set_call_result(handle);
}
// ok
int vm_cbfs_vm_file_read(int bufferPtr, int size, int handle)
{
    /* MMORPG_DataSheet reads each row as a 4-byte length followed by at most
     * 0x200 bytes of row data.  World-map setup walks hundreds of rows across
     * wMap/sMap and used to pay four unused Unicorn register reads plus one
     * SDL_malloc/SDL_free pair for every tiny file read.  Keep the large-read
     * behavior unchanged, but serve the common data-sheet reads from a bounded
     * host stack buffer. */
    u8 smallBuffer[0x200];
    u8 *tmp = NULL;
    int heapAllocated = 0;
    size_t readed = 0;

    if (size <= 0)
        return vm_set_call_result(0);
    if (handle < 0 || handle >= 16 || openFileList[handle] == NULL)
        return vm_set_call_result(-1);
    if (openFileList[handle] == VM_PSEUDO_DIR_HANDLE)
        return vm_set_call_result(0);

    if ((u32)size <= sizeof(smallBuffer))
    {
        tmp = smallBuffer;
    }
    else
    {
        tmp = SDL_malloc((size_t)size);
        heapAllocated = 1;
        if (tmp == NULL)
            return vm_set_call_result(-1);
    }

    readed = fread(tmp, 1, (size_t)size, openFileList[handle]);

    if (readed > 0)
        uc_mem_write(MTK, bufferPtr, tmp, readed);
    if (heapAllocated)
        SDL_free(tmp);
    return vm_set_call_result((int)readed);
}
// ok
int vm_cbfs_vm_file_write(int bufferPtr, int size, int fileHandle)
{
    u32 pc = 0, lr = 0;
    uc_reg_read(MTK, UC_ARM_REG_PC, &pc);
    uc_reg_read(MTK, UC_ARM_REG_LR, &lr);
    if (fileHandle < 0 || fileHandle >= 16 || openFileList[fileHandle] == NULL || size <= 0)
    {
        return vm_set_call_result(-1);
    }
    if (openFileList[fileHandle] == VM_PSEUDO_DIR_HANDLE)
    {
        return vm_set_call_result(0);
    }
    void *buffer = SDL_malloc(size);
    uc_mem_read(MTK, bufferPtr, buffer, size);
    int r = fwrite(buffer, 1, size, openFileList[fileHandle]);
    SDL_free(buffer);
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
    if (f == NULL)
    {
        char resolvedName[256];
        if (vm_file_try_resolve_cbe_module_path(normalizedName, "rb", resolvedName, sizeof(resolvedName)) ||
            vm_file_try_resolve_jhonline_dsh_path(normalizedName, "rb", resolvedName, sizeof(resolvedName)))
            f = fopen(resolvedName, "rb");
    }
    u32 r = 0;
    if (f != NULL)
    {
        r = 1;
        fclose(f);
    }
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

int vm_cbfs_vm_file_rename(int disk, int oldNamePtr, int newNamePtr)
{
    char oldRaw[256];
    char newRaw[256];
    char oldName[256];
    char newName[256];
    char finalNewName[256];
    int r;

    vm_read_path_string(oldNamePtr, oldRaw, sizeof(oldRaw));
    vm_read_path_string(newNamePtr, newRaw, sizeof(newRaw));
    vm_file_normalize_host_path(oldRaw, oldName, sizeof(oldName));
    vm_file_normalize_host_path(newRaw, newName, sizeof(newName));
    snprintf(finalNewName, sizeof(finalNewName), "%s", newName);

    if (newName[0] != 0 && strchr(newName, '/') == NULL && strchr(newName, '\\') == NULL)
    {
        char *slash = strrchr(oldName, '/');
        if (slash != NULL)
        {
            size_t dirLen = (size_t)(slash - oldName) + 1;
            if (dirLen + strlen(newName) < sizeof(finalNewName))
            {
                memcpy(finalNewName, oldName, dirLen);
                snprintf(finalNewName + dirLen, sizeof(finalNewName) - dirLen, "%s", newName);
            }
        }
    }

    if (oldName[0] == 0 || finalNewName[0] == 0 ||
        vm_is_pseudo_dir_path(oldName) || vm_is_pseudo_dir_path(finalNewName) ||
        vm_is_cbm_resource_path(oldName) || vm_is_cbm_resource_path(finalNewName))
    {
        return vm_set_call_result(0);
    }

    vm_file_ensure_parent_dirs(finalNewName);
    r = rename(oldName, finalNewName);
    if (r != 0)
    {
        remove(finalNewName);
        r = rename(oldName, finalNewName);
    }

    return vm_set_call_result(r == 0 ? 1 : 0);
}

int vm_cbfs_vm_file_getfilesize(int fileHandle)
{
    if (fileHandle < 0 || fileHandle >= 16 || openFileList[fileHandle] == NULL)
        return vm_set_call_result(-1);
    if (openFileList[fileHandle] == VM_PSEUDO_DIR_HANDLE)
        return vm_set_call_result(0);
    FILE *f = openFileList[fileHandle];
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    int r = ftell(f);
    if (pos >= 0)
        fseek(f, pos, SEEK_SET);
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
    len = vm_get_var_short(a1 + 10);
    for (i = 0; len > i; i++)
    {
        tmp = (int)vm_get_var(a1 + 28);
        v3 = (int)vm_get_var(tmp + 4 * i);
        if (v3)
        {
            u8 pkgIndex = 0;
            uc_mem_read(MTK, v3 + 1, &pkgIndex, 1);
            if (pkgIndex == v1)
                goto LABEL_8;
        }
    }
    return (u8)v1;
}

int vm_DF_DataPackage_ReleasePackage(int a1, int namePtr)
{
    if (a1 == 0)
        return vm_set_call_result(0);

    u16 childCount = vm_get_var_short(a1 + 10);
    u32 childTable = vm_get_var(a1 + 28);

    for (int i = 0; childTable && i < childCount; i++)
    {
        u32 slotAddr = childTable + 4 * i;
        u32 child = vm_get_var(slotAddr);
        int matched = namePtr == 0;

        if (child == 0)
            continue;
        if (!matched)
        {
            u32 childName = vm_get_var(child + 4);
            matched = childName && vm_DF_String_Equal(namePtr, childName);
        }
        if (!matched)
            continue;

        vm_resource_cache_note_package(child);

        u16 resCount = vm_get_var_short(child + 8);
        u32 stringTable = vm_get_var(child + 12);
        for (int j = 0; stringTable && j < resCount; j++)
            vm_DF_Free(stringTable + 4 * j);

        vm_DF_Free(child + 4);
        vm_DF_Free(child + 12);
        vm_DF_Free(child + 16);
        vm_DF_Free(child + 20);
        if (vm_uses_be_fixed_df_layout())
            vm_DF_Free(child + 24);
        vm_DF_Free(slotAddr);
        vm_set_var(slotAddr, 0);
    }

    if (namePtr == 0)
    {
        u16 resCount = vm_get_var_short(a1 + 8);
        u32 stringTable = vm_get_var(a1 + 12);
        for (int i = 0; stringTable && i < resCount; i++)
            vm_DF_Free(stringTable + 4 * i);

        vm_DF_Free(a1 + 12);
        vm_DF_Free(a1 + 16);
        vm_DF_Free(a1 + 20);
        vm_DF_Free(a1 + 28);

        if (!vm_uses_be_fixed_df_layout())
        {
            int fileHandle = (int)vm_get_var(a1 + 92);
            if (fileHandle >= 0)
            {
                vm_cbfs_vm_file_close(fileHandle);
                vm_set_var(a1 + 92, (u32)-1);
            }
            if (vm_get_var(a1 + 100))
                vm_DF_Free(a1 + 100);
        }
        else
        {
            vm_DF_Free(a1 + 24);
        }

        uc_mem_write(MTK, a1, emptyBuff, vm_df_package_struct_size());
        return vm_set_call_result(0);
    }

    return vm_set_call_result(childCount);
}

int vm_DF_DataPackage_LoadPackage(int a1, int srcPtr)
{
    int result = 0;
    char packageName[128] = {0};
    if (srcPtr)
        vm_readStringByPtr(srcPtr, packageName);

    /* src == NULL -> set first byte at a1 to 0 and return 0 */
    if (srcPtr == 0)
    {
        vm_set_var_byte(a1, 0); /* *(_BYTE *)a1 = 0; */
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
            /* Firmware generations use either an 80-byte or 108-byte package. */
            vm_DF_Malloc_IN(slot_addr_vm, vm_df_package_struct_size());
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
            return vm_set_call_result(pkgIndex);
        }
    }

    /* no free slot: append at end (index = slot_count) */
    {
        int new_slot_addr_vm = table_base + 4 * slot_count;
        vm_DF_Malloc_IN(new_slot_addr_vm, vm_df_package_struct_size());
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
        (lastAddress >= 0x0100db80 && lastAddress <= 0x0100de90))
    {
        u8 raw[8] = {0};
        uc_mem_read(MTK, bufPtr + offset, raw, sizeof(raw));
    }
    offset += 2;
    uc_mem_write(MTK, offsetPtr, &offset, 4);
    return vm_set_call_result(ret);
}

u32 vm_DF_WriteShort(u32 bufPtr, u32 offsetPtr, u32 value)
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
u32 vm_DF_WriteInt(u32 a1, u32 a2, u32 value)
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
    /*
     * DreamFactory's slot allocator is also used for zero-length arrays.  The
     * CBE text layout code immediately stores the first line offset after
     * DF_Malloc_IN(slot, 0), so the target ABI must leave a writable sentinel
     * in the slot instead of NULL.  Keep direct vm_malloc(0) semantics intact
     * and normalize only this slot-based allocator contract.  The sentinel is
     * a 16-bit line offset, so reserve at least two logical bytes.
     */
    int ptr = vm_malloc(a2 > 0 ? (u32)a2 : 2u);
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

int vm_DF_ReadString2(u32 bufferPtr, u32 offsetPtr)
{
    u32 len = (u32)vm_DF_ReadInt(bufferPtr, offsetPtr);
    u32 offset = vm_get_var(offsetPtr);
    u32 memoryBlock = vm_get_var(VM_DreamFactory_MemoryBlock_ADDRESS);
    u32 result = vm_MF_MemoryBlock_Malloc(memoryBlock, len + 1);

    if (len)
        vm_memcpy(result, bufferPtr + offset, len);
    offset += len;
    vm_set_var(offsetPtr, offset);

    {
        u8 zero = 0;
        uc_mem_write(MTK, result + len, &zero, 1);
    }
    return vm_set_call_result(result);
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

static int vm_DF_String_Dsh_Basename_Equal(int resourceNamePtr, int requestNamePtr)
{
    char resourceName[128];
    char requestName[128];
    char normalizedResource[128];
    char normalizedRequest[128];

    if (resourceNamePtr == 0 || requestNamePtr == 0)
        return 0;

    vm_read_path_string(resourceNamePtr, resourceName, sizeof(resourceName));
    vm_read_path_string(requestNamePtr, requestName, sizeof(requestName));
    vm_file_normalize_host_path(resourceName, normalizedResource, sizeof(normalizedResource));
    vm_file_normalize_host_path(requestName, normalizedRequest, sizeof(normalizedRequest));
    if (!vm_file_is_bare_dsh_resource(normalizedRequest))
        return 0;

    return _stricmp(vm_path_basename(normalizedResource), normalizedRequest) == 0;
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

    count = (int16_t)vm_get_var_short(a1 + 10);
    base = (int)vm_get_var(a1 + 28);

    for (i = 0; i < count; i++)
    {
        entry = (int)vm_get_var(base + 4 * i);

        if (!entry)
            continue;

            str_ptr = (int)vm_get_var(entry + 4);

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

    if (p)
    {
        target = vm_get_var(p);
        if (target)
            vm_free(target);
        vm_set_var(p, 0);
    }
    return vm_set_call_result(p);
}
int vm_DF_DataPackage_LoadFromTResource(int a1, int a2)
{
    int n4, result = 0;
    int v22 = VM_Str_Tmp_ADDRESS;     // offset
    int v17 = VM_Str_Tmp_ADDRESS + 4; // name ptr storage
    int namePtr = 0;
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
            namePtr = vm_get_var(v17);
            v19 = vm_DF_ReadInt(v27, v22);
        }
        else
        {
            namePtr = 0;
        }

        int entry = vm_DF_DataPackage_LocateDataPackage(a1, namePtr);

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
                u32 v21Ptr = vm_malloc_var();
                vm_set_var(v21Ptr, v21);
                vm_DF_ReadInt(v27, v21Ptr);
                v21 = vm_get_var(v21Ptr);
                int v24 = vm_DF_ReadInt(v27, v21Ptr);
                v21 = vm_get_var(v21Ptr);
                int v7 = vm_DF_ReadInt(v27, v21Ptr);
                v21 = vm_get_var(v21Ptr);

                u16 cnt16 = (u16)v7;
                uc_mem_write(MTK, entry + 8, &cnt16, 2);

                int int_arr = 0, str_arr = 0, idx_arr = 0;

                if (v7)
                {
                    int pkg_id = vm_DF_DataPackage_GetPackageID(a1, namePtr);

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
                        int val = vm_DF_ReadInt(v27, v21Ptr);
                        v21 = vm_get_var(v21Ptr);
                        uc_mem_write(MTK, int_arr + 4 * i, &val, 4);
                    }

                    for (int j = 0; j < v7; j = (j + 1))
                    {
                        int str_ptr = str_arr + 4 * j;
                        vm_DF_ReadStringEx(str_ptr, v27, v21Ptr);
                        v21 = vm_get_var(v21Ptr);

                        u16 idx = (u16)(v23++);
                        uc_mem_write(MTK, idx_arr + 2 * j, &idx, 2);
                    }
                }

                if (v24)
                {
                    int data_ptr = v27 + v21;
                    uc_mem_write(MTK, entry + 24, &data_ptr, 4);

                    int id = vm_DF_DataPackage_GetPackageID(a1, namePtr);
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

                vm_free_var(v21Ptr);
                u8 one = 1;
                uc_mem_write(MTK, entry, &one, 1);
            }
        }

        if (namePtr)
        {
            vm_DF_Free(v17);
            namePtr = 0;
        }

        result = (int16_t)(v18 + 1);
    }

    return result;
}
int vm_DF_DataPackage_LoadFormTCardEx(int a1, int pathPtr, int fileSeekPos)
{
    int fileHandle;
    bool compactLayout = vm_uses_be_fixed_df_layout();

    int v34 = 0;

    int namePtr = 0;
    int count, i = 0;
    int pkg_id = 0;

    fileHandle = compactLayout ? -1 : (int)vm_get_var(a1 + 92);

    if (fileHandle == -1)
    {
        char rwBuffer[] = {"rb"}; // 注意，windows中要按二进制读取，真机中的r = windows中的rb
        u32 ptr = vm_malloc(4);
        // vm中新建"r"内存
        uc_mem_write(MTK, ptr, &rwBuffer, sizeof(rwBuffer));
        fileHandle = vm_cbfs_vm_file_open(2, pathPtr, ptr);
        vm_free(ptr);
        if (fileHandle < 0)
            return fileHandle;
    }

    if (!compactLayout)
        vm_set_var(a1 + 92, fileHandle);
    vm_cbfs_vm_file_seek(fileHandle, fileSeekPos, 0);

    int size = vm_DF_File_ReadInt(fileHandle);
    // printf("DF_DataPackage_LoadFormTCard2:%x\n", size);

    int bufferPtr = 0;
    if (1)
    {
        u32 ptr = vm_malloc_var();
        vm_set_var(ptr, 0);
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
    if (!compactLayout)
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

        if (entry && !vm_get_var_byte(entry))
        {

            if (1)
            {
                u32 pfBuffer = 0;
                if (!compactLayout)
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

                if (g_cbeInfo.isBiggianProgram)
                    printf("[info][cbe] df_tcard_block entry=%08x name=%s block=%08x data=%08x count=%d\n",
                           entry, entryName[0] ? entryName : "<root>", block_size,
                           data_size, arr_cnt);

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
                    if (compactLayout)
                    {
                        int dataEntry = entry;
                        vm_DF_Malloc_IN(dataEntry + 24, data_size);
                        int dataPtr = vm_get_var(dataEntry + 24);
                        if (dataPtr)
                            vm_DF_File_ReadToBuffer(fileHandle, dataPtr, data_size);
                        if (g_cbeInfo.isBiggianProgram)
                            printf("[info][cbe] df_tcard_data entry=%08x package=%08x pkg=%d data=%08x size=%08x\n",
                                   entry, a1, pkg_id, dataPtr, data_size);
                    }
                    else
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

    if (compactLayout)
    {
        vm_cbfs_vm_file_close(fileHandle);
        return 0;
    }

    int txt_id = (int)vm_get_var(a1 + 104);
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
    int offset = (int)vm_get_var(VM_DF_DataPackage_In_File_Offset_ADDRESS);
    return vm_DF_DataPackage_LoadFormTCardEx(a1, VM_DF_DataPackage_FilePath_ADDRESS, offset);
}

int VM_DF_DataPackage_DoLoading(int a1, int a2, int a3)
{
    u8 n2 = 0;
    u32 inFileOffset = 0;

    if (a3)
    {
        uc_mem_read(MTK, VM_DF_DataPackage_LoadType_ADDRESS, &n2, 1);
        inFileOffset = vm_get_var(VM_DF_DataPackage_In_File_Offset_ADDRESS);

        if (n2 == 1)
        {
            // TF卡加载
            return vm_DF_DataPackage_LoadFormTCard(a1);
        }

        if (n2 == 2)
        {
            a2 = vm_get_var(VM_DF_DataPackage_In_File_Offset_ADDRESS);
        }
    }

    // 默认走资源加载
    return vm_DF_DataPackage_LoadFromTResource(a1, a2);
}

u32 vm_initDFDataPackage(u32 a1, u32 a2)
{
    u32 tmp2, tmp3, tmp4;
    tmp2 = 0;
    vm_set_var(a1 + 4, tmp2);
    vm_set_var_short(a1 + 8, (u16)tmp2);
    vm_set_var_short(a1 + 10, (u16)tmp2);
    vm_set_var(a1 + 12, tmp2);
    vm_set_var(a1 + 16, tmp2);
    vm_set_var(a1 + 24, tmp2);
    vm_set_var(a1 + 28, tmp2);
    // printf("vm_initDFDataPackage(%x,%x)\n", a1, a2); // debug
    //*a1 = 1
    tmp2 = 1;
    uc_mem_write(MTK, a1, &tmp2, 1);
    // DF_Malloc_IN((_DWORD *)(a1 + 28), 4 * a2);
    tmp4 = vm_malloc(a2 * 4);
    uc_mem_write(MTK, tmp4, emptyBuff, SDL_min(a2 * 4, 1024)); // memclr_w()
    vm_set_var(a1 + 28, tmp4);
    if (!vm_uses_be_fixed_df_layout())
    {
        tmp2 = (u32)-1;
        vm_set_var(a1 + 92, tmp2);
        tmp2 = 0;
        vm_set_var(a1 + 100, tmp2);
    }
    tmp3 = VM_DF_DATAPACKAGE_FUNC_LIST_ADDRESS; // DF_DataPackage_LoadPackage
    vm_set_var(a1 + 32, tmp3);
    tmp3 += 4; // DF_DataPackage_ReleasePackage
    vm_set_var(a1 + 36, tmp3);
    tmp3 += 4; // DF_DataPackage_LoadFromTResource
    vm_set_var(a1 + 40, tmp3);
    tmp3 += 4; // DF_DataPackage_LoadFormTCard
    vm_set_var(a1 + 44, tmp3);
    tmp3 += 4; // DF_DataPackage_DoLoading
    vm_set_var(a1 + 48, tmp3);
    tmp3 += 4; // DF_DataPackage_LocateDataPackage
    vm_set_var(a1 + 52, tmp3);
    tmp3 += 4; // DF_DataPackage_GetFile
    vm_set_var(a1 + 56, tmp3);
    tmp3 += 4; // DF_DataPackage_GetFileByID
    vm_set_var(a1 + 60, tmp3);
    tmp3 += 4; // DF_DataPackage_GetFileNameByID
    vm_set_var(a1 + 64, tmp3);
    tmp3 += 4; // DF_DataPackage_GetFileID
    vm_set_var(a1 + 68, tmp3);
    tmp3 += 4; // DF_DataPackage_ShowFileList
    // return DF_DataPackage_ShowFileList
    u32 r = tmp3;
    vm_set_var(a1 + 72, tmp3);
    tmp3 += 4;
    if (vm_uses_be_fixed_df_layout())
        vm_set_var(a1 + 76, tmp3); // DF_DataPackage_LoadFormTCardEx
    else
        vm_set_var(a1 + 80, tmp3); // DF_DataPackage_InitTxt
    return vm_set_call_result(r);
}

void vm_sprintf()
{
    int tmp1, tmp2, tmp3, tmp4, tmp5;
    u8 charBuffer[1024];
    u8 spBuffer[1024];
    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_mem_read(MTK, tmp1, charBuffer, 128);
    printf("[vm_sprintf][%x]",lastAddress);
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
    u8 outBuffer[1024];
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
                char fmtBuffer[32];
                int fmtLen = 0;
                char spec = 0;
                fmtBuffer[fmtLen++] = '%';
                while (c)
                {
                    if (fmtLen < (int)sizeof(fmtBuffer) - 1)
                        fmtBuffer[fmtLen++] = (char)c;
                    if (strchr("diuoxXcsp%", c))
                    {
                        spec = (char)c;
                        break;
                    }
                    if (!(c == '0' || c == '-' || c == '+' || c == ' ' || c == '#' ||
                          c == '.' || c == 'l' || c == 'h' || (c >= '1' && c <= '9')))
                    {
                        spec = (char)c;
                        break;
                    }
                    c = charBuffer[i++];
                }
                fmtBuffer[fmtLen] = 0;

                if (spec == '%')
                {
                    outBuffer[0] = '%';
                    strCnt2 = 1;
                }
                else
                {
                    if (paramPos < 2)
                    {
                        uc_reg_read(MTK, UC_ARM_REG_R2 + paramPos, &currParam);
                    }
                    else
                    { // 从栈顶读取参数
                        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                        currParam = vm_get_var(tmp4 + (paramPos - 2) * 4);
                    }

                    if (spec == 's')
                    {
                        vm_readStringByPtr(currParam, spBuffer);
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, spBuffer);
                    }
                    else if (spec == 'x' || spec == 'X' || spec == 'u' || spec == 'o' || spec == 'p')
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, (u32)currParam);
                    }
                    else if (spec == 'd')
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, (int)currParam);
                    }
                    else if (spec == 'c')
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, currParam & 0xff);
                    }
                    else
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), "%%%c", spec ? spec : c);
                    }
                    paramPos++;
                }
                if (strCnt2 < 0)
                    strCnt2 = 0;
                if (strCnt2 >= (int)sizeof(outBuffer))
                    strCnt2 = sizeof(outBuffer) - 1;
                uc_mem_write(MTK, inBufferPtr, outBuffer, strCnt2);
                inBufferPtr += strCnt2;
                needConvert = 0;
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
    u8 outBuffer[1024];
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
                char fmtBuffer[32];
                int fmtLen = 0;
                char spec = 0;
                fmtBuffer[fmtLen++] = '%';
                while (c)
                {
                    if (fmtLen < (int)sizeof(fmtBuffer) - 1)
                        fmtBuffer[fmtLen++] = (char)c;
                    if (strchr("diuoxXcsp%", c))
                    {
                        spec = (char)c;
                        break;
                    }
                    if (!(c == '0' || c == '-' || c == '+' || c == ' ' || c == '#' ||
                          c == '.' || c == 'l' || c == 'h' || (c >= '1' && c <= '9')))
                    {
                        spec = (char)c;
                        break;
                    }
                    c = charBuffer[i++];
                }
                fmtBuffer[fmtLen] = 0;

                if (spec == '%')
                {
                    outBuffer[0] = '%';
                    strCnt2 = 1;
                }
                else
                {
                    if (paramPos < 3)
                    {
                        uc_reg_read(MTK, UC_ARM_REG_R1 + paramPos, &currParam);
                    }
                    else
                    { // 从栈顶读取参数
                        uc_reg_read(MTK, UC_ARM_REG_SP, &tmp4);
                        currParam = vm_get_var(tmp4 + (paramPos - 3) * 4);
                    }

                    if (spec == 's')
                    {
                        vm_readStringByPtr(currParam, spBuffer);
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, spBuffer);
                    }
                    else if (spec == 'x' || spec == 'X' || spec == 'u' || spec == 'o' || spec == 'p')
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, (u32)currParam);
                    }
                    else if (spec == 'd')
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, (int)currParam);
                    }
                    else if (spec == 'c')
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), fmtBuffer, currParam & 0xff);
                    }
                    else
                    {
                        strCnt2 = snprintf((char *)outBuffer, sizeof(outBuffer), "%%%c", spec ? spec : c);
                    }
                    paramPos++;
                }
                if (strCnt2 < 0)
                    strCnt2 = 0;
                if (strCnt2 >= (int)sizeof(outBuffer))
                    strCnt2 = sizeof(outBuffer) - 1;
                uc_mem_write(MTK, inBufferPtr, outBuffer, strCnt2);
                inBufferPtr += strCnt2;
                needConvert = 0;
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
u32 vm_DF_GetMemoryBlock(void)
{
    u32 tmp1;
    tmp1 = vm_get_var(VM_DreamFactory_MemoryBlock_ADDRESS);
    return vm_set_call_result(tmp1);
}
// ok
void vm_initMemoryBlock(p_g_memoryBlock, size)
{
    int tmp1, tmp2, tmp3, tmp4;
    // p_g_memoryBlock[0] = 新的内存块地址。全局块使用固定池；其它块需要独立
    // backing storage，避免多个 MemoryBlock 互相覆盖 actor/资源解析结果。
    tmp1 = (p_g_memoryBlock == VM_MemoryBlock_PTR_ADDRESS) ? VM_MF_MemoryBlock_POOL_ADDRESS : vm_malloc(size);
    vm_set_var(p_g_memoryBlock, tmp1);
    // p_g_memoryBlock[1] = offset
    tmp1 = 0;
    vm_set_var(p_g_memoryBlock + 4, tmp1);
    // p_g_memoryBlock[2] = size
    vm_set_var(p_g_memoryBlock + 8, size);
    // p_g_memoryBlock[3] = MF_MemoryBlock_Malloc;
    // p_g_memoryBlock[4] = MF_MemoryBlock_Reset;
    // p_g_memoryBlock[5] = MF_MemoryBlock_Release;
    for (tmp3 = 3; tmp3 < 6; tmp3++)
    {
        tmp4 = VM_MF_MemoryBlock_FUNC_LIST_ADDRESS + (tmp3 - 3) * 4;
        vm_set_var(p_g_memoryBlock + tmp3 * 4, tmp4);
    }
}
// ok
// a2允许为0
u32 vm_MF_MemoryBlock_Malloc(int a1, int a2)
{
    u32 baseAddr, offset, size, oldOffset;
    // mpcmdFreeShareMemIn
    baseAddr = vm_get_var(a1);      //->base
    oldOffset = vm_get_var(a1 + 4); //->offset
    size = vm_get_var(a1 + 8);      //->offset
    u32 alignedSize = (a2 + 3) & ~3;
    offset = oldOffset + alignedSize;
    if (offset > size)
    {
        printf("申请内存(%x)超过可用值(%x)\n", a2, size - oldOffset);
        dumpCpuInfo();
        assert(0);
    }
    vm_set_var(a1 + 4, offset);
    return vm_set_call_result(baseAddr + oldOffset);
    // printf("[call]MF_MemoryBlock_Malloc(%x,%x)(%x)\n", baseAddr, a2, oldOffset);
}

u32 vm_MF_MemoryBlock_Reset(int a1)
{
    u32 v = 0;
    vm_set_var(a1 + 4, v); // offset重置为0
    return vm_set_call_result(v);
}
u32 vm_MF_MemoryBlock_Release(int a1)
{
    u32 base = vm_get_var(a1);
    u32 size = vm_get_var(a1 + 8);
    u32 zero = 0;

    if (size > 0 && base != 0 && vm_malloc_user_size(base) > 0)
        vm_free(base);
    vm_set_var(a1 + 8, zero);
    return vm_set_call_result(zero);
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
    count1 = (int16_t)vm_get_var_short(a1 + 8);

    // base_ptr = *(uint32 *)(a1 + 12)
    base_ptr = vm_get_var(a1 + 12);

    // id_base = *(uint32 *)(a1 + 20)
    id_base = vm_get_var(a1 + 20);

    for (i = 0; i < count1; i++)
    {
        // str_ptr = *(uint32 *)(base_ptr + 4 * i)
        str_ptr = vm_get_var(base_ptr + 4 * i);
        // uc_mem_read(MTK,str_ptr,globalSprintfBuff,128);

        if (vm_DF_String_Equal(str_ptr, namePtr))
        {
            u32 file_id = vm_get_var_short(id_base + 2 * i);
            uc_reg_write(uc, UC_ARM_REG_R0, &file_id);
            return file_id;
        }
    }

    for (i = 0; i < count1; i++)
    {
        str_ptr = vm_get_var(base_ptr + 4 * i);
        if (vm_DF_String_Dsh_Basename_Equal(str_ptr, namePtr))
        {
            u32 file_id = vm_get_var_short(id_base + 2 * i);
            uc_reg_write(uc, UC_ARM_REG_R0, &file_id);
            return file_id;
        }
    }

    // count2 = *(int16 *)(a1 + 10)
    count2 = (int16_t)vm_get_var_short(a1 + 10);

    // child_base = *(uint32 *)(a1 + 28)
    child_base = vm_get_var(a1 + 28);

    for (j = 0; j < count2; j++)
    {
        // v7 = *(uint32 *)(child_base + 4 * j)
        v7 = vm_get_var(child_base + 4 * j);

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
    // Guest manager structures use the CBE's byte order. Resource package
    // payload integers remain little-endian and are decoded separately by
    // vm_DF_File_ReadInt/vm_DF_ReadInt.
    count = (int16_t)vm_get_var_short(a1 + 8);
    if (count <= a2)
    {
        printf("codeLib\\DF_DataPackage.c");
        assert(0);
    }

    file_handle = (int32_t)vm_get_var(a1 + 92);
    data_base = (int32_t)vm_get_var(a1 + 16);
    base_offset = (int32_t)vm_get_var(a1 + 88);
    v5 = (int32_t)vm_get_var(data_base + 4 * a2);

    // seek
    vm_cbfs_vm_file_seek(file_handle, v5 + base_offset, 0);

    if ((count - 1) > (int16_t)a2)
    {
        v6 = (int32_t)vm_get_var(data_base + 4 * a2 + 4);
    }
    else
    {
        v6 = (int32_t)vm_get_var(a1 + 96);
    }

    len = v6 - v5;
    //    buffer = vmGetDataBuffer(len);

    buffer = vm_malloc(len);
    vm_cbfs_vm_file_read(buffer, len, file_handle);
    u8 head[8] = {0};
    if (len > 0)
        uc_mem_read(MTK, buffer, head, SDL_min((u32)len, (u32)sizeof(head)));
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
    if (fileId == (u32)-1)
        return vm_set_call_result(0);
    count1 = (int16_t)vm_get_var_short(a1 + 8);
    id_base = vm_get_var(a1 + 20);
    data_base = vm_get_var(a1 + 16);
    offset_base = vm_get_var(a1 + 24);
    // printf("DF_DataPackage_GetFileByID:%d\n", fileId);

    for (i = 0; i < count1; i++)
    {
        file_id = (int16_t)vm_get_var_short(id_base + i * 2);

        if (file_id == (int16_t)fileId)
        {
            if (!vm_uses_be_fixed_df_layout())
                uc_mem_read(uc, a1 + 84, &flag, 1);

            if (flag)
            {
                int data = vm_DF_DataPackage_DoReadData(a1, i);
                return data;
            }
            else
            {
                data_ptr = vm_get_var(data_base + i * 4);
                offset = offset_base;
                offset = data_ptr + offset;
                return vm_set_call_result(offset);
            }
        }
    }

    count2 = (int16_t)vm_get_var_short(a1 + 10);
    child_base = vm_get_var(a1 + 28);

    for (j = 0; j < count2; j++)
    {
        v7 = vm_get_var(child_base + j * 4);

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
    int FileID = vm_DF_DataPackage_GetFileID(a1, namePtr);
    if (FileID < 0)
        return vm_set_call_result(0);
    int data = vm_DF_DataPackage_GetFileByID(a1, FileID);
    char fileName[128] = {0};
    if (namePtr)
        vm_readStringByPtr(namePtr, fileName);
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
    char name[128] = {0};
    tmp1 = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
    int result = tmp1 ? (int)vm_DF_DataPackage_GetFileID(tmp1, a1) : -1;
    if (result < 0 && a1)
    {
        vm_read_path_string(a1, name, sizeof(name));
        result = (int)vm_resource_cache_lookup_id(name);
    }
    return vm_set_call_result((u32)result);
}
// ok
int vm_DF_GetResourceByFileName(int a1)
{
    int tmp1;
    int result = 0;
    tmp1 = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
    if (tmp1)
        result = vm_DF_DataPackage_GetFile(tmp1, a1);
    if (result == 0 && a1)
    {
        char name[128];
        vm_read_path_string(a1, name, sizeof(name));
        result = (int)vm_resource_cache_load_by_name(name);
    }
    return vm_set_call_result((u32)result);
}
// ok
int vm_DF_GetResourceByResourceID(int a1)
{
    int tmp1;
    int result = 0;
    tmp1 = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
    if (tmp1)
        result = vm_DF_DataPackage_GetFileByID(tmp1, a1);
    if (result == 0)
        result = (int)vm_resource_cache_load_by_id((u32)a1);
    return vm_set_call_result((u32)result);
}
// ok
void vm_DF_GetResourceNameByID(int a1)
{
    int tmp1;
    VmReleasedResource *entry;
    tmp1 = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
    if (tmp1)
    {
        u32 result = vm_DF_DataPackage_GetFileNameByID(tmp1, a1);
        if (result)
            return;
    }
    entry = vm_resource_cache_find_by_id((u32)a1);
    if (entry)
    {
        u32 len = (u32)strlen(entry->name);
        u32 ptr = vm_malloc(len + 1);
        uc_mem_write(MTK, ptr, entry->name, len + 1);
        vm_set_call_result(ptr);
        return;
    }
    vm_set_call_result((u32)-1);
}
int vm_DF_GetTResource(int a1)
{
    u32 DreamFactoryResourceBuffer; // r0
    // todo DF_FactoryCharB_Init();
    DreamFactoryResourceBuffer = vm_DF_GetResourceByFileName(a1);
    vm_set_var(VM_DreamFactoryResourceBuffer_ADDRESS, DreamFactoryResourceBuffer);
    return DreamFactoryResourceBuffer;
}

int vm_IMG_Destory(u32 a1)
{
    u32 n3 = 0;
    u32 ptr = 0;
    u32 zero = 0;
    // n3 = *(uint8 *)(a1 + 8)
    n3 = vm_get_var_byte(a1 + 8);

    if (n3 == 1 || n3 == 3 || n3 == 4)
    {
        // ptr = *(uint32 *)a1
        ptr = vm_get_var(a1);

        if (ptr)
        {
            vm_free(ptr);
        }

        // *(uint32 *)a1 = 0
        vm_set_var(a1, zero);

        if (!a1)
        {
            printf("IMG_Destory释放失败");
            assert(0);
        }

        n3 = 0;

        // 清空结构
        uc_mem_write(MTK, a1, emptyBuff, 12);
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
static void vm_IMG_WriteResult(u32 resultPtr, u32 pixelsPtr, u16 width, u16 height, u8 needFree)
{
    if (!resultPtr)
        return;
    uc_mem_write(MTK, resultPtr, emptyBuff, 12);
    vm_set_var(resultPtr, pixelsPtr);
    vm_set_var_short(resultPtr + 4, width);
    vm_set_var_short(resultPtr + 6, height);
    vm_set_var_byte(resultPtr + 8, needFree);
}

void vM_DrawImageWithClipEx()
{
    int dstInfo, srcInfo, srcX, srcY, w, h, dstX, dstY, sp, dstPtr, srcPtr;
    u16 dst_w, dst_h, src_w, src_h;
    uc_reg_read(MTK, UC_ARM_REG_R0, &dstInfo);
    uc_reg_read(MTK, UC_ARM_REG_R1, &srcInfo);
    uc_reg_read(MTK, UC_ARM_REG_R2, &srcX);
    uc_reg_read(MTK, UC_ARM_REG_R3, &srcY);
    uc_reg_read(MTK, UC_ARM_REG_SP, &sp);
    w = (int)vm_get_var(sp);
    h = (int)vm_get_var(sp + 4);
    dstX = (int)vm_get_var(sp + 8);
    dstY = (int)vm_get_var(sp + 12);
    srcPtr = (int)vm_get_var(srcInfo);
    src_w = vm_get_var_short(srcInfo + 4);
    src_h = vm_get_var_short(srcInfo + 6);
    dstPtr = (int)vm_get_var(dstInfo);
    dst_w = vm_get_var_short(dstInfo + 4);
    dst_h = vm_get_var_short(dstInfo + 6);
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
    w = (int)vm_get_var(sp);
    h = (int)vm_get_var(sp + 4);
    dstX = (int)vm_get_var(sp + 8);
    dstY = (int)vm_get_var(sp + 12);
    srcPtr = (int)vm_get_var(srcInfo);
    src_w = vm_get_var_short(srcInfo + 4);
    src_h = vm_get_var_short(srcInfo + 6);
    dstPtr = (int)vm_get_var(dstInfo);
    dst_w = vm_get_var_short(dstInfo + 4);
    dst_h = vm_get_var_short(dstInfo + 6);
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
        tmp32 = vm_get_var(VM_DreamFactory_MemoryBlock_ADDRESS);
        v3 = vm_MF_MemoryBlock_Malloc(tmp32, 12);
    }

    if (a1 == 0)
    {
        return vm_set_call_result(0);
    }

    // n3 = *a1
    uc_mem_read(uc, a1, &n3, 1);
    // uc_mem_read(uc, a1, cbeTextString, 32); // debug

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
        vm_IMG_WriteResult(v3, tmp32, v5, v6, 0);
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
        u16 transparent = 0;
        int vmPtr = vm_malloc(sizeof(transparent));
        uc_mem_write(MTK, vmPtr, &transparent, sizeof(transparent));
        if (p.pixels)
            free_mem(p.pixels);
        vm_IMG_WriteResult(resultPtr, vmPtr, 1, 1, 1);
        return vm_set_call_result(resultPtr);
    }

    int vmPtr = vm_malloc(mallocSize);

    uc_mem_write(MTK, vmPtr, p.pixels, mallocSize);
    free_mem(p.pixels);
    vm_IMG_WriteResult(resultPtr, vmPtr, (u16)p.width, (u16)p.height, 1);
    return vm_set_call_result(resultPtr);
}

static u32 vm_png_crc32(const u8 *data, u32 len)
{
    u32 crc = 0xffffffffu;
    for (u32 i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (u32 bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return crc ^ 0xffffffffu;
}

static u32 vm_png_read_be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static void vm_png_write_be32(u8 *p, u32 value)
{
    p[0] = (u8)(value >> 24);
    p[1] = (u8)(value >> 16);
    p[2] = (u8)(value >> 8);
    p[3] = (u8)value;
}

/* Mobile Rainbow PNGGAME keeps normal PNG chunks except PLTE: the chunk's
 * declared length remains RGB888-sized while its payload contains one
 * big-endian RGB565 word per palette entry. */
static u8 *vm_png_normalize_firmware_palette(const u8 *src, u32 srcSize, u32 *outSize)
{
    static const u8 firmwareSignature[8] = {0x89, 'P', 'N', 'G', 'G', 'A', 'M', 'E'};
    static const u8 pngSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (srcSize < 8 || memcmp(src, firmwareSignature, 8) != 0)
        return NULL;

    u32 capacity = srcSize + 1024;
    u8 *dst = (u8 *)SDL_malloc(capacity);
    if (!dst)
        return NULL;
    memcpy(dst, pngSignature, 8);

    u32 srcPos = 8;
    u32 dstPos = 8;
    while (srcPos + 8 <= srcSize)
    {
        u32 declaredLen = vm_png_read_be32(src + srcPos);
        const u8 *type = src + srcPos + 4;
        bool isPalette = memcmp(type, "PLTE", 4) == 0;
        u32 sourcePayloadLen = isPalette ? (declaredLen / 3) * 2 : declaredLen;
        if (srcPos + 8 + sourcePayloadLen + 4 > srcSize ||
            dstPos + 12 + declaredLen > capacity)
        {
            SDL_free(dst);
            return NULL;
        }

        vm_png_write_be32(dst + dstPos, declaredLen);
        memcpy(dst + dstPos + 4, type, 4);
        if (isPalette)
        {
            u32 colors = declaredLen / 3;
            const u8 *encoded = src + srcPos + 8;
            u8 *expanded = dst + dstPos + 8;
            for (u32 i = 0; i < colors; ++i)
            {
                u16 rgb565 = ((u16)encoded[i * 2] << 8) | encoded[i * 2 + 1];
                u8 r5 = (u8)((rgb565 >> 11) & 0x1f);
                u8 g6 = (u8)((rgb565 >> 5) & 0x3f);
                u8 b5 = (u8)(rgb565 & 0x1f);
                expanded[i * 3] = (u8)((r5 << 3) | (r5 >> 2));
                expanded[i * 3 + 1] = (u8)((g6 << 2) | (g6 >> 4));
                expanded[i * 3 + 2] = (u8)((b5 << 3) | (b5 >> 2));
            }
            u32 crc = vm_png_crc32(dst + dstPos + 4, 4 + declaredLen);
            vm_png_write_be32(dst + dstPos + 8 + declaredLen, crc);
        }
        else
        {
            memcpy(dst + dstPos + 8, src + srcPos + 8, declaredLen + 4);
        }

        srcPos += 8 + sourcePayloadLen + 4;
        dstPos += 12 + declaredLen;
        if (memcmp(type, "IEND", 4) == 0)
            break;
    }

    *outSize = dstPos;
    return dst;
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

    u32 decodeSize = pngSize;
    u8 *normalized = vm_png_normalize_firmware_palette(pngData, pngSize, &decodeSize);
    if (normalized)
    {
        SDL_free(pngData);
        pngData = normalized;
    }

    int w = 0, h = 0, comp = 0;
    u8 *rgba = stbi_load_from_memory(pngData, (int)decodeSize, &w, &h, &comp, 4);
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

    vm_IMG_WriteResult(resultPtr, vmPtr, (u16)w, (u16)h, 1);
    return vm_set_call_result(resultPtr);
}

static void vm_readStringByRegLimited(uc_arm_reg reg, u8 *dst, size_t dstSize)
{
    u32 ptr;
    if (dstSize == 0)
        return;
    dst[0] = 0;
    uc_reg_read(MTK, reg, &ptr);
    vm_readStringByPtrLimited(ptr, dst, dstSize);
}

static void vm_readStringByPtrLimited(u32 ptr, u8 *dst, size_t dstSize)
{
    size_t pos = 0;
    if (dstSize == 0)
        return;
    dst[0] = 0;
    while (pos + 1 < dstSize)
    {
        u8 tmp = 0;
        if (uc_mem_read(MTK, ptr + (u32)pos, &tmp, 1) != UC_ERR_OK || tmp == 0)
            break;
        dst[pos++] = tmp;
    }
    dst[pos] = 0;
}

static void vm_read_string_by_ptr_limited(u32 ptr, char *dst, size_t dstSize)
{
    vm_readStringByPtrLimited(ptr, (u8 *)dst, dstSize);
}

static void vm_readStringGbkByRegLimited(uc_arm_reg reg, u8 *dst, size_t dstSize)
{
    vm_readStringByRegLimited(reg, dst, dstSize);
}

static void vm_readStringUCS2ByRegLimited(uc_arm_reg reg, u16 *dst, size_t dstBytes)
{
    u32 ptr;
    size_t maxChars = dstBytes / sizeof(u16);
    size_t pos = 0;
    if (maxChars == 0)
        return;
    dst[0] = 0;
    uc_reg_read(MTK, reg, &ptr);
    while (pos + 1 < maxChars)
    {
        u16 tmp = 0;
        if (uc_mem_read(MTK, ptr + (u32)(pos * sizeof(u16)), &tmp, sizeof(tmp)) != UC_ERR_OK || tmp == 0)
            break;
        dst[pos++] = tmp;
    }
    dst[pos] = 0;
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
    vm_InitDlWPayManager(VM_DL_PAY_MANAGER_ADDRESS);
    vm_InitDlRsManager(VM_DL_RS_MANAGER_ADDRESS);
    vm_InitDlImageManager(VM_DL_IMAGE_MANAGER_ADDRESS);
}
u32 vm_DF_GetDataPackage()
{
    u32 tmp1;
    tmp1 = vm_get_var(VM_DreamFactory_DataPackage_ADDRESS);
    return vm_set_call_result(tmp1);
}

static u32 g_vm_img_app_data_package = 0;
static u32 g_vm_img_inner_data_package = 0;
static u32 g_vm_img_current_data_package = 0;

static u32 vm_IMG_ensureDataPackage(u32 *slot)
{
    if (*slot == 0)
    {
        u32 packageSize = vm_df_package_struct_size();
        *slot = vm_malloc(packageSize);
        if (*slot)
            uc_mem_write(MTK, *slot, emptyBuff, packageSize);
    }
    return *slot;
}

static u32 vm_IMG_getappDataPackage(void)
{
    return vm_IMG_ensureDataPackage(&g_vm_img_app_data_package);
}

static u32 vm_IMG_getappInDataPackage(void)
{
    return vm_IMG_ensureDataPackage(&g_vm_img_inner_data_package);
}

u32 vm_IMG_InitDataPageEx(u32 a1, u32 a2, u32 dataPackage)
{
    if (dataPackage == 0)
        return vm_set_call_result(0);

    g_vm_img_current_data_package = dataPackage;
    vm_set_var(VM_DreamFactory_DataPackage_ADDRESS, dataPackage);

    u16 count = vm_get_var_short(dataPackage + 8);
    if (count != 0)
        return vm_set_call_result(count);

    vm_initDFDataPackage(dataPackage, 5);
    vm_DF_DataPackage_LoadPackage(dataPackage, 0);
    return VM_DF_DataPackage_DoLoading(dataPackage, a1, a2);
}

u32 vm_IMG_InitDataPage(u32 a1, u32 a2)
{
    u32 dataPackage = vm_IMG_getappDataPackage();
    return vm_IMG_InitDataPageEx(a1, a2, dataPackage);
}

u32 vm_IMG_InitInnerDataPageEx(u32 a1, u32 a2)
{
    u32 dataPackage = vm_IMG_getappInDataPackage();
    return vm_IMG_InitDataPageEx(a1, 0, dataPackage);
}

u32 vm_IMG_ReleaseDataPage(void)
{
    if (g_vm_img_inner_data_package)
    {
        vm_free(g_vm_img_inner_data_package);
        g_vm_img_inner_data_package = 0;
    }
    if (g_vm_img_app_data_package)
    {
        vm_free(g_vm_img_app_data_package);
        g_vm_img_app_data_package = 0;
    }
    g_vm_img_current_data_package = 0;
    vm_set_var(VM_DreamFactory_DataPackage_ADDRESS, 0);
    return vm_set_call_result(0);
}

u32 vm_IMG_InitDataPageTxt(u32 textId)
{
    u32 dataPackage;
    if (textId >= 0xfff)
    {
        dataPackage = vm_IMG_getappInDataPackage();
        textId -= 0xfff;
    }
    else
    {
        dataPackage = g_vm_img_current_data_package ? g_vm_img_current_data_package : vm_IMG_getappDataPackage();
    }

    if (dataPackage == 0)
        return vm_set_call_result(0);
    return vm_DF_DataPackage_InitTxt(dataPackage, textId);
}

u32 vm_IMG_CreateImageFormIdEx(u32 imageId, u32 dataPackage, u32 outImage)
{
    if (dataPackage == 0)
        return vm_set_call_result(0);

    u16 count = vm_get_var_short(dataPackage + 8);
    if (imageId >= count)
        return vm_set_call_result(0);

    u8 loadedFromFile = 0;
    u32 data = 0;
    if (!vm_uses_be_fixed_df_layout())
        uc_mem_read(MTK, dataPackage + 84, &loadedFromFile, 1);
    if (loadedFromFile)
    {
        data = vm_DF_DataPackage_DoReadData(dataPackage, imageId);
    }
    else
    {
        u32 dataBase = 0;
        u32 offsetBase = 0;
        u32 offset = 0;
        dataBase = vm_get_var(dataPackage + 16);
        offsetBase = vm_get_var(dataPackage + 24);
        if (dataBase == 0)
            return vm_set_call_result(0);
        offset = vm_get_var(dataBase + imageId * 4);
        data = offset + offsetBase;
    }

    if (data == 0)
        return vm_set_call_result(0);
    return vm_IMG_CreateImageFormStream(data, outImage);
}

u32 vm_IMG_CreateImageFormResForVm(u32 imageId, u32 outImage)
{
    u32 dataPackage;
    u32 localId = imageId;
    if (imageId >= 0xfff)
    {
        dataPackage = vm_IMG_getappInDataPackage();
        localId -= 0xfff;
    }
    else
    {
        dataPackage = g_vm_img_current_data_package ? g_vm_img_current_data_package : vm_IMG_getappDataPackage();
    }

    vm_IMG_CreateImageFormIdEx(localId, dataPackage, outImage);
    return vm_set_call_result(outImage);
}

u32 vm_IMG_CreateImageFormRes(u32 a1)
{
    u32 tmp1 = 0;
    u32 tmp2 = 0;
    u32 DataPackage = vm_DF_GetDataPackage();
    u32 Data;
    if (DataPackage)
    {
        if (!vm_uses_be_fixed_df_layout())
            uc_mem_read(MTK, DataPackage + 84, &tmp2, 1);
        if (tmp2)
        {
            Data = vm_DF_DataPackage_DoReadData(DataPackage, a1);
        }
        else
        {
            u32 dataBase = 0;
            u32 offsetBase = 0;
            dataBase = vm_get_var(DataPackage + 16);
            offsetBase = vm_get_var(DataPackage + 24);
            tmp1 = vm_get_var(dataBase + 4 * a1);
            Data = tmp1 + offsetBase;
        }
        return vm_IMG_CreateImageFormStream(Data, 0);
    }
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
    idx = vm_get_var(a2);
    startIdx = idx;

    // len = *(uint8 *)(a1 + idx)
    uc_mem_read(uc, a1 + idx, &len, 1);

    idx++;

    // *a2 = idx
    vm_set_var(a2, idx);
    result = vm_get_var(VM_DreamFactory_MemoryBlock_ADDRESS);
    result = vm_MF_MemoryBlock_Malloc(result, len + 1);

    for (i = 0; i < len; i++)
    {
        // idx = *a2
        idx = vm_get_var(a2);

        // ch = *(uint8 *)(a1 + idx)
        uc_mem_read(uc, a1 + idx, &ch, 1);

        idx++;

        // *a2 = idx
        vm_set_var(a2, idx);

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
        if ((lastAddress >= 0x0100db80 && lastAddress <= 0x0100de90) ||
            strstr(outName, "warrior") || strstr(outName, "rior") || strstr(outName, "jian") ||
            strstr(outName, "guang") || strstr(outName, "death") || strstr(outName, "ying"))
        {
            u32 srcLen = len + 1 < sizeof(srcBytes) ? len + 1 : (u32)sizeof(srcBytes);
            uc_mem_read(uc, a1 + startIdx, srcBytes, srcLen);
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
    u8 resType = vm_get_var_byte(a1);

    /*
     * The real firmware always treats normal resources as LZSS streams here.
     * Network mock resources sometimes need to provide a tiny hand-authored DF
     * descriptor, so use a private pseudo type instead of changing real type=1
     * resource behavior.
     */
    if (resType == 0xf1)
    {
        u32 srcSize = vm_malloc_user_size(a1);
        if (srcSize <= 1)
        {
            return vm_set_call_result(0);
        }

        u32 rawLen = srcSize - 1;
        dest = vm_malloc(rawLen);
        for (u32 i = 0; i < rawLen; ++i)
        {
            u8 byte = vm_get_var_byte(a1 + 1 + i);
            uc_mem_write(uc, dest + i, &byte, 1);
        }
        return vm_set_call_result(dest);
    }

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

    u32 rawOutLen = (u32)b8 | ((u32)b7 << 8) | ((u32)b6 << 16) | ((u32)b5 << 24);
    /* Some CBE resources set the high bit as a stream flag; the firmware
       allocator accepts it, but the emulator heap needs a real byte count. */
    v8 = rawOutLen & 0x7fffffffu;

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
    }
    return vm_set_call_result(dest);
}
// ok
u32 vm_DF_GetStreamTResource(int a1)
{
    u32 DreamFactoryResourceBuffer; // r0
    DreamFactoryResourceBuffer = vm_DF_GetResourceByFileName(a1);
    vm_set_var(VM_DreamFactoryResourceBuffer_ADDRESS, DreamFactoryResourceBuffer);
    return DreamFactoryResourceBuffer;
}

u32 vm_MF_resetGmemoryBlock()
{
    vm_MF_MemoryBlock_Reset(VM_MemoryBlock_PTR_ADDRESS);
    return vm_set_call_result(VM_MemoryBlock_PTR_ADDRESS);
}

void vm_strcpy(u32 dst, u32 src)
{
    vm_guest_strcpy(dst, src);
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
        vm_set_var(tableAddr + i * 4, tmp);
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
        vm_set_var(tableAddr + i * 4, tmp);
    }
}

void vm_InitDlWPayManager(u32 tmp1)
{
    if (tmp1 == 0)
        return;
    uc_mem_write(MTK, tmp1, emptyBuff, VM_MANAGER_TABLE_SIZE);
    vm_set_var(tmp1 + 7 * 4, VM_DL_PAY_FUNC_LIST_ADDRESS + 7 * 4);
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
