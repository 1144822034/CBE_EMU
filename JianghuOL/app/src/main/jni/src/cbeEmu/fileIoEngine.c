#include "fileIoEngine.h"

#ifdef _WIN32
#include <windows.h>
#include <wchar.h>
#endif

#ifdef _WIN32
static FILE *vm_wfopen_codepage(const char *filename, const char *mode, UINT codepage)
{
    int nameLen = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, filename, -1, NULL, 0);
    int modeLen = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, mode, -1, NULL, 0);
    if (nameLen > 0 && modeLen > 0)
    {
        wchar_t *wideName = (wchar_t *)SDL_malloc(nameLen * sizeof(wchar_t));
        wchar_t *wideMode = (wchar_t *)SDL_malloc(modeLen * sizeof(wchar_t));
        if (wideName && wideMode)
        {
            MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, filename, -1, wideName, nameLen);
            MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, mode, -1, wideMode, modeLen);
            FILE *file = _wfopen(wideName, wideMode);
            SDL_free(wideName);
            SDL_free(wideMode);
            if (file)
                return file;
        }
        else
        {
            if (wideName)
                SDL_free(wideName);
            if (wideMode)
                SDL_free(wideMode);
        }
    }
    return NULL;
}
#endif

static FILE *vm_fopen_host_path(const char *filename, const char *mode)
{
#ifdef _WIN32
    FILE *file = vm_wfopen_codepage(filename, mode, CP_UTF8);
    if (file)
        return file;

    UINT acp = GetACP();
    if (acp != CP_UTF8)
    {
        file = vm_wfopen_codepage(filename, mode, acp);
        if (file)
            return file;
    }

    if (acp != 936)
    {
        file = vm_wfopen_codepage(filename, mode, 936);
        if (file)
            return file;
    }
#endif
    return fopen(filename, mode);
}

int dirExists(char *path)
{
    struct stat info;

    if (stat(path, &info) != 0)
        return 0; // 路径不存在

    return (info.st_mode & S_IFDIR) != 0; // 是否为目录
}
/**
 * 读取文件
 * 读取完成后需要释放
 */
u8 *readFile(const char *filename, u32 *size)
{
    FILE *file;
    u8 *tmp;
    long file_size;
    u8 flag;
    // 打开文件 a.txt
    file = vm_fopen_host_path(filename, "rb");
    if (file == NULL)
    {
        printf("Failed to open file:%s\n", filename);
        return NULL;
    }

    // 移动文件指针到文件末尾，获取文件大小
    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    *size = file_size;
    rewind(file);
    // 为 tmp 分配内存
    tmp = (u8 *)SDL_malloc(file_size * sizeof(u8));
    if (tmp == NULL)
    {
        printf("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

    // 读取文件内容到 tmp 中
    size_t result = fread(tmp, 1, file_size, file);
    if (result != file_size)
    {
        printf("Failed to read file");
        SDL_free(tmp);
        fclose(file);
        return NULL;
    }
    fclose(file);
    return tmp;
}

int writeFile(const char *filename, void *buff, u32 size)
{
    FILE *file;
    u8 *tmp;
    u8 flag;
    // 打开文件 a.txt
    file = vm_fopen_host_path(filename, "wb");
    if (file == NULL)
    {
        return 0;
    }
    // 移动文件指针到文件末尾，获取文件大小
    fseek(file, 0, SEEK_SET);
    // 读取文件内容到 tmp 中
    size_t result = fwrite(buff, 1, size, file);
    if (result != size)
    {
        printf("Failed to write file\n");
    }
    fclose(file);
    return result;
}
