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

#ifdef CBE_PLATFORM_ANDROID
#include "android_gbk_table.inc"

static int lookup_gbk_by_unicode(u16 unicode, u16 *gbk)
{
    int lo = 0;
    int hi = (int)(sizeof(kUnicodeToGbk) / sizeof(kUnicodeToGbk[0])) - 1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (kUnicodeToGbk[mid].unicode == unicode)
        {
            *gbk = kUnicodeToGbk[mid].gbk;
            return 1;
        }
        if (kUnicodeToGbk[mid].unicode < unicode)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

static int lookup_unicode_by_gbk(u16 gbk, u16 *unicode)
{
    int lo = 0;
    int hi = (int)(sizeof(kGbkToUnicode) / sizeof(kGbkToUnicode[0])) - 1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        if (kGbkToUnicode[mid].gbk == gbk)
        {
            *unicode = kGbkToUnicode[mid].unicode;
            return 1;
        }
        if (kGbkToUnicode[mid].gbk < gbk)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

static u32 utf8_next_codepoint(const u8 **cursor)
{
    const u8 *s = *cursor;
    u32 ch;
    if (s[0] < 0x80)
    {
        *cursor = s + 1;
        return s[0];
    }
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80)
    {
        ch = ((u32)(s[0] & 0x1f) << 6) | (u32)(s[1] & 0x3f);
        *cursor = s + 2;
        return ch;
    }
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80)
    {
        ch = ((u32)(s[0] & 0x0f) << 12) | ((u32)(s[1] & 0x3f) << 6) | (u32)(s[2] & 0x3f);
        *cursor = s + 3;
        return ch;
    }
    *cursor = s + 1;
    return '?';
}

static int utf8_write_codepoint(u8 **out, size_t *left, u16 ch)
{
    if (ch < 0x80)
    {
        if (*left < 1)
            return 0;
        *(*out)++ = (u8)ch;
        *left -= 1;
    }
    else if (ch < 0x800)
    {
        if (*left < 2)
            return 0;
        *(*out)++ = (u8)(0xc0 | (ch >> 6));
        *(*out)++ = (u8)(0x80 | (ch & 0x3f));
        *left -= 2;
    }
    else
    {
        if (*left < 3)
            return 0;
        *(*out)++ = (u8)(0xe0 | (ch >> 12));
        *(*out)++ = (u8)(0x80 | ((ch >> 6) & 0x3f));
        *(*out)++ = (u8)(0x80 | (ch & 0x3f));
        *left -= 3;
    }
    return 1;
}

void gbk_to_utf8(u8 *gbk, u8 *utf8, size_t outlen)
{
    u8 *out = utf8;
    size_t left;
    if (!gbk || !utf8 || outlen == 0)
        return;
    left = outlen - 1;
    memset(utf8, 0, outlen);
    for (u32 i = 0; gbk[i] != 0 && left > 0;)
    {
        u16 unicode = '?';
        if (gbk[i] < 0x80)
            unicode = gbk[i++];
        else if (gbk[i + 1] != 0)
        {
            u16 code = ((u16)gbk[i] << 8) | gbk[i + 1];
            lookup_unicode_by_gbk(code, &unicode);
            i += 2;
        }
        else
            ++i;
        if (!utf8_write_codepoint(&out, &left, unicode))
            break;
    }
    *out = 0;
}

void utf8_to_gbk(u8 *utf, u8 *gbk, size_t outlen)
{
    u8 *out = gbk;
    size_t left;
    const u8 *cursor;
    if (!utf || !gbk || outlen == 0)
        return;
    left = outlen - 1;
    cursor = utf;
    memset(gbk, 0, outlen);
    while (*cursor && left > 0)
    {
        u32 ch = utf8_next_codepoint(&cursor);
        if (ch < 0x80)
        {
            *out++ = (u8)ch;
            --left;
        }
        else
        {
            u16 code;
            if (ch <= 0xffff && lookup_gbk_by_unicode((u16)ch, &code) && left >= 2)
            {
                *out++ = (u8)(code >> 8);
                *out++ = (u8)(code & 0xff);
                left -= 2;
            }
            else
            {
                *out++ = '?';
                --left;
            }
        }
    }
    *out = 0;
}

void gbk_to_unicode(u8 *gbk, u8 *unicode, size_t outlen)
{
    size_t pos = 0;
    if (!gbk || !unicode || outlen < 2)
        return;
    memset(unicode, 0, outlen);
    for (u32 i = 0; gbk[i] != 0 && pos + 2 <= outlen;)
    {
        u16 ch = '?';
        if (gbk[i] < 0x80)
            ch = gbk[i++];
        else if (gbk[i + 1] != 0)
        {
            u16 code = ((u16)gbk[i] << 8) | gbk[i + 1];
            lookup_unicode_by_gbk(code, &ch);
            i += 2;
        }
        else
            ++i;
        unicode[pos++] = (u8)(ch & 0xff);
        unicode[pos++] = (u8)(ch >> 8);
    }
}
#else
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
#endif

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
#ifdef CBE_PLATFORM_ANDROID
    u32 out = 0;
    if (!ucs2 || !gbk || gbk_len == 0)
        return -1;
    memset(gbk, 0, gbk_len);
    for (u32 i = 0; i + 1 < ucs2_len && out + 1 < gbk_len; i += 2)
    {
        u16 ch = (u16)ucs2[i] | ((u16)ucs2[i + 1] << 8);
        if (ch == 0)
            break;
        if (ch < 0x80)
            gbk[out++] = (u8)ch;
        else
        {
            u16 code;
            if (!lookup_gbk_by_unicode(ch, &code) || out + 2 >= gbk_len)
                gbk[out++] = '?';
            else
            {
                gbk[out++] = (u8)(code >> 8);
                gbk[out++] = (u8)(code & 0xff);
            }
        }
    }
    if (out < gbk_len)
        gbk[out] = 0;
    return 0;
#else
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
#endif
}
