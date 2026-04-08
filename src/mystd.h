#include "config.h"
#include <iconv.h>

void my_memcpy(void *dest, void *src, int len);
void my_memset(void *dest, u8 value, int len);
u8 my_mem_compare(u8 *src, u8 *dest, u32 len);
void gbk_to_utf8(u8 *gbk, u8 *utf8, size_t outlen);
void gbk_to_unicode(u8 *gbk, u8 *unicode, size_t outlen);
int strlen_gbk(u8 *gbk);
u32 ucs2_strlen(u16 *str);
int ucs2_to_gbk(u8 *ucs2, u32 ucs2_len, u8 *gbk, u32 gbk_len);
