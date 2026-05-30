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

void utf8_to_gbk(u8 *utf, u8 *gbk, size_t outlen)
{
    iconv_t cd = iconv_open("GBK", "UTF-8");
    if (cd == (iconv_t)-1)
        return;

    size_t inlen = strlen(utf);
    u8 *pin = (u8 *)utf;
    u8 *pout = gbk;

    memset(gbk, 0, outlen);

    iconv(cd, &pin, &inlen, &pout, &outlen);
    iconv_close(cd);
}

void gbk_to_unicode(u8 *gbk, u8 *unicode, size_t outlen)
{
    iconv_t cd = iconv_open("UCS-2LE", "GBK");
    if (cd == (iconv_t)-1)
        return;

    size_t inlen = strlen((char *)gbk);
    u8 *pin = (u8 *)gbk;
    u8 *pout = unicode;

    memset(pout, 0, outlen);

    iconv(cd, &pin, &inlen, &pout, &outlen);
    iconv_close(cd);
}

int strlen_utf16(u16 *utf16)
{
    int len = 0;
    while (utf16[len] != 0)
        len++;
    return len;
}
int strcpy_utf16(u16 *dst, u16 *src)
{
    int len = 0;
    while (src[len] != 0)
    {
        dst[len] = src[len];
        len++;
    }
    dst[len] = 0;
    return len;
}
/* 统计 GBK 字符串的字符数（以字为单位）并且可以选择是否严格验证：
 * strict = 1 -> 如果遇到非法序列，立即返回 (size_t)-1 表示错误
 * strict = 0 -> 遇到非法序列时把该字节当作单字节字符继续统计
 */
int strlen_gbk(u8 *s)
{
    if (s == NULL)
        return 0;
    u32 i = 0;
    u32 count = 0;
    while (s[i] != 0)
    {
        u8 b1 = s[i];
        if (b1 < 0x80)
        {
            count += 1; /* ASCII */
            i += 1;
        }
        /* GBK 双字节首字节范围 0x81 - 0xFE */
        else if (b1 >= 0x81 && b1 <= 0xFE)
        {
            if (s[i + 1] == 0)
                break; /* 不足两字节 -> 不完整 */
            count += 1;
            i += 2;
        }
        else
        {
            count += 1;
            i += 1;
            break;
        }
    }
    return count;
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
int ucs2_to_gbk(u8 *ucs2, u32 ucs2_len, u8 *gbk, u32 gbk_len)
{
    iconv_t cd;
    char *inbuf = (char *)ucs2;
    char *outbuf = gbk;

    u32 inbytesleft = ucs2_len;
    u32 outbytesleft = gbk_len;

    cd = iconv_open("GBK", "UCS-2LE");
    if (cd == (iconv_t)-1)
        return -1;

    memset(gbk, 0, gbk_len);

    if (iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft) == (u32)-1)
    {
        iconv_close(cd);
        return -2;
    }

    iconv_close(cd);
    return 0;
}
