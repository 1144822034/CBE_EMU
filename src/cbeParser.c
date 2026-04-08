#include "cbeParser.h"

cbeInfo g_cbeInfo;

int sumCheck(int *memBuffer, int len)
{
    int result = 0;
    len /= 4;
    while (len-- > 0)
    {

        result += *(memBuffer++);
    }
    return result;
}

int sumCheckByBig(u8 *memBuffer, int len)
{
    int result = 0;
    len /= 4;
    while (len-- > 0)
    {

        result += (((memBuffer[0] << 24)) | ((memBuffer[1] << 16) | (memBuffer[2] << 8) | (memBuffer[3])));
        memBuffer += 4;
    }
    return result;
}

static int check_interval_char_in_ram(u8 *ptr)
{
    if (!ptr)
        return 0;
    for (int i = 0; i < 8; ++i)
    {
        if (ptr[i] != 0xFE)
            return 0;
    }
    return 1;
}
int parseCbeHeader(u8 *cbeFileBuffer, u32 size)
{

    int v = 0;
    int n = 0;
    u8 *filePtr = cbeFileBuffer;
    // 头部信息固定0x98字节
    // 共12组信息，前4组固定，然后得到一个偏移值
    u32 params[5];
    u32 pgParamsOffset[6];
    u32 pgParams[6];
    for (int i = 0; i < 5; i++)
    {
        n = i * 12;
        if (!check_interval_char_in_ram(filePtr + n))
        {
            printf("错误的cbe文件");
            assert(0);
            break;
        }
        n += 8;
        params[i] = (filePtr[n++] << 24) | (filePtr[n++] << 16) | (filePtr[n++] << 8) | (filePtr[n++]);
    }
    filePtr = cbeFileBuffer + 68 + params[4];
    // 继续读取6组数值
    for (int i = 0; i < 6; i++)
    {
        n = i * 12;
        if (!check_interval_char_in_ram(filePtr + n))
        {
            printf("错误的cbe文件");
            assert(0);
            break;
        }
        n += 8;
        pgParams[i] = (filePtr[n++] << 24) | (filePtr[n++] << 16) | (filePtr[n++] << 8) | (filePtr[n++]);
    }
    int remainLen = 0x98 - (params[4] + 140);
    filePtr = cbeFileBuffer + 0x98 - remainLen;
    g_cbeInfo.headerInt1 = params[0];
    g_cbeInfo.headerInt2 = params[1];
    g_cbeInfo.headerInt3 = params[2];
    g_cbeInfo.headerInt4 = params[3];

    g_cbeInfo.codeLen = pgParams[0];
    g_cbeInfo.codeSign = pgParams[1];
    g_cbeInfo.BssDataLen = pgParams[2];
    g_cbeInfo.BssDataSign = pgParams[3];
    g_cbeInfo.RwDataLen = pgParams[4];
    g_cbeInfo.RwDataSign = pgParams[5];
    int preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }

    g_cbeInfo.preCodeIntervalCount = preCodeIntervalCount;
    g_cbeInfo.codeOffset = filePtr - cbeFileBuffer; // 代码段的实际起始位置
    filePtr += preCodeIntervalCount;                // 跳过间隔符
    filePtr += g_cbeInfo.codeLen;                   // 跳过代码段

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.BssDataOffset = filePtr - cbeFileBuffer; // BSS段的实际起始位置
    g_cbeInfo.preBssDataIntervalCount = preCodeIntervalCount;
    filePtr += preCodeIntervalCount; // 跳过间隔符
    filePtr += g_cbeInfo.BssDataLen; // 跳过BSS数据段

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.RwDataOffset = filePtr - cbeFileBuffer; // RW段的实际起始位置
    g_cbeInfo.preRwDataIntervalCount = preCodeIntervalCount;

    filePtr += preCodeIntervalCount; // 跳过间隔符
    filePtr += g_cbeInfo.RwDataLen;  // 跳过RW数据段

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.preRwDataIntervalCount = preCodeIntervalCount;
    g_cbeInfo.DF_DataPacakge_Size = (*filePtr++ << 24) | (*filePtr++ << 16) | (*filePtr++ << 8) | *filePtr++;

    preCodeIntervalCount = 0;
    while (*filePtr == 0xfe)
    {
        preCodeIntervalCount++;
        filePtr++;
    }
    g_cbeInfo.DF_Data_Pacakage_Offset = filePtr - cbeFileBuffer;

    int sign = sumCheck(cbeFileBuffer + g_cbeInfo.codeOffset, g_cbeInfo.codeLen);
    int bigSign = sumCheckByBig(cbeFileBuffer + g_cbeInfo.codeOffset, g_cbeInfo.codeLen);
    if (sign == g_cbeInfo.codeSign)
    {
        g_cbeInfo.isBiggianProgram = 0;
        g_cbeInfo.codeSignVerify = sign;
        g_cbeInfo.BssDataSignVerify = sumCheck(cbeFileBuffer + g_cbeInfo.BssDataOffset, g_cbeInfo.BssDataLen);
        g_cbeInfo.RwDataSignVerify = sumCheck(cbeFileBuffer + g_cbeInfo.RwDataOffset, g_cbeInfo.RwDataLen);
    }
    else if (bigSign == g_cbeInfo.codeSign)
    {
        g_cbeInfo.isBiggianProgram = 1;
        g_cbeInfo.codeSignVerify = bigSign;
        g_cbeInfo.BssDataSignVerify = sumCheckByBig(cbeFileBuffer + g_cbeInfo.BssDataOffset, g_cbeInfo.BssDataLen);
        g_cbeInfo.RwDataSignVerify = sumCheckByBig(cbeFileBuffer + g_cbeInfo.RwDataOffset, g_cbeInfo.RwDataLen);
    }
    else
    {
        printf("code段签名验证失败，无论大端序还是小端序");
        assert(0);
    }

    printf("[cbeParser]是否大端序 %d\n", g_cbeInfo.isBiggianProgram);

    printf("[cbeParser]codeOffset %x\n", g_cbeInfo.codeOffset);
    printf("[cbeParser]codeLen %x\n", g_cbeInfo.codeLen);
    printf("[cbeParser]codeSign %x sumCheck %x\n", g_cbeInfo.codeSign, g_cbeInfo.codeSignVerify);

    printf("[cbeParser]BssDataOffset %x\n", g_cbeInfo.BssDataOffset);
    printf("[cbeParser]bssDataLen %x\n", g_cbeInfo.BssDataLen);
    printf("[cbeParser]bssDataSign %x sumCheck %x\n", g_cbeInfo.BssDataSign, g_cbeInfo.BssDataSignVerify);

    printf("[cbeParser]RwDataOffset %x\n", g_cbeInfo.RwDataOffset);
    printf("[cbeParser]rwDataLen %x\n", g_cbeInfo.RwDataLen);
    printf("[cbeParser]rwDataSign %x sumCheck %x\n", g_cbeInfo.RwDataSign, g_cbeInfo.RwDataSignVerify);

    printf("[cbeParser]DF_Data_Pacakage_Offset %x\n", g_cbeInfo.DF_Data_Pacakage_Offset);
    printf("[cbeParser]DF_DataPacakge_Size %x\n", g_cbeInfo.DF_DataPacakge_Size);

    if (g_cbeInfo.BssDataSign != g_cbeInfo.BssDataSignVerify)
    {
        printf("bss段签名校验不通过");
        assert(0);
    }
    if (g_cbeInfo.RwDataSign != g_cbeInfo.RwDataSignVerify)
    {
        printf("rw段签名校验不通过");
        assert(0);
    }

    return 1;
}
