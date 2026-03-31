#include "mystd.h"

void my_memcpy(void *dest, void *src, int len)
{
    memcpy(dest, src, len);
}

void my_memset(void *dest, u8 value, int len)
{
    memset(dest, value, len);
}

/**
 * 内存比较 1 相等 0 不相等
 */
u8 my_mem_compare(u8 *src, u8 *dest, u32 len)
{
    while (*src++ == *dest++)
    {
        len--;
        if (len == 0)
            return 1;
    }
    return 0;
}

void gbk_to_utf8(u8 *gbk, u8 *utf8, size_t outlen)
{
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == (iconv_t)-1)
        return;

    size_t inlen = strlen(gbk);
    u8 *pin = (u8 *)gbk;
    u8 *pout = utf8;

    memset(utf8, 0, outlen);

    iconv(cd, &pin, &inlen, &pout, &outlen);
    iconv_close(cd);
}

void gbk_to_unicode(u8 *gbk, u8 *unicode, size_t outlen)
{
    iconv_t cd = iconv_open("UTF-16", "GBK");
    if (cd == (iconv_t)-1)
        return;

    size_t inlen = strlen_gbk(gbk);
    u8 *pin = (u8 *)gbk;
    u8 *pout = unicode;

    memset(unicode, 0, outlen);

    iconv(cd, &pin, &inlen, &pout, &outlen);
    iconv_close(cd);
}

int strlen_utf16(u8 *utf16)
{
    int len = 0;
    while (utf16[len * 2] != 0 || utf16[len * 2 + 1] != 0)
        len++;
    return len;
}
/* 返回下一个字符的字节长度：
 *  1 -> ASCII 单字节
 *  2 -> 合法 GBK 双字节
 *  0 -> 无效/不完整（例如以单独的 GBK 前导字节结束）
 */
static int gbk_next_char_len(const unsigned char *s, size_t remaining)
{
    if (remaining == 0 || s == NULL)
        return 0;
    unsigned char b1 = s[0];
    if (b1 < 0x80)
    {
        return 1; /* ASCII */
    }
    /* GBK 双字节首字节范围 0x81 - 0xFE */
    if (b1 >= 0x81 && b1 <= 0xFE)
    {
        if (remaining < 2)
            return 0; /* 不足两字节 -> 不完整 */
        unsigned char b2 = s[1];
        /* GBK 次字节范围通常 0x40 - 0xFE，排除 0x7F */
        if (b2 >= 0x40 && b2 <= 0xFE && b2 != 0x7F)
        {
            return 2;
        }
        else
        {
            return 0; /* 非法次字节 */
        }
    }
    /* 0x80 或其他不能作为 GBK 首字节的情况视为非法（返回 0） */
    return 0;
}

/* 统计 GBK 字符串的字符数（以字为单位）并且可以选择是否严格验证：
 * strict = 1 -> 如果遇到非法序列，立即返回 (size_t)-1 表示错误
 * strict = 0 -> 遇到非法序列时把该字节当作单字节字符继续统计
 */
int strlen_gbk(u8 *s)
{
    int strict = 1;
    if (s == NULL)
        return 0;
    u8 *p = (u8 *)s;
    u8 bytes = strlen(s);
    u8 i = 0;
    u8 chars = 0;
    while (i < bytes)
    {
        int len = gbk_next_char_len(p + i, bytes - i);
        if (len == 1)
        {
            i += 1;
            chars += 1;
        }
        else if (len == 2)
        {
            i += 2;
            chars += 1;
        }
        else
        { /* len == 0，非法或不完整 */
            if (strict)
                return (size_t)-1;
            /* 宽松模式：把当前字节当作单字节处理，继续 */
            i += 1;
            chars += 1;
        }
    }
    return chars;
}

int utf16_len(u8 *utf16)
{
    int len = 0;
    while (*utf16++ != 0)
        len++;
    return len;
}

int ucs2_to_utf8(u8 *in, int ilen, u8 *out, int olen)
{
    int length = 0;
    if (!out)
        return length;
    u8 *start = NULL;
    u8 *pout = out;
    for (start = in; start != NULL && start < in + ilen - 1; start += 2)
    {
        unsigned short ucs2_code = *(unsigned short *)start;
        if (0x0080 > ucs2_code)
        {
            /* 1 byte UTF-8 Character.*/
            if (length + 1 > olen)
                return -1;

            *pout = (u8)*start;
            length++;
            pout++;
        }
        else if (0x0800 > ucs2_code)
        {
            /*2 bytes UTF-8 Character.*/
            if (length + 2 > olen)
                return -1;
            *pout = ((u8)(ucs2_code >> 6)) | 0xc0;
            *(pout + 1) = ((u8)(ucs2_code & 0x003F)) | 0x80;
            length += 2;
            pout += 2;
        }
        else
        {
            /* 3 bytes UTF-8 Character .*/
            if (length + 3 > olen)
                return -1;

            *pout = ((u8)(ucs2_code >> 12)) | 0xE0;
            *(pout + 1) = ((u8)((ucs2_code & 0x0FC0) >> 6)) | 0x80;
            *(pout + 2) = ((u8)(ucs2_code & 0x003F)) | 0x80;
            length += 3;
            pout += 3;
        }
    }

    return length;
}
