#include <assert.h>
#include "config.h"

typedef struct cbeInfo
{
    u32 preCodeIntervalCount;
    u32 codeOffset;
    u32 codeLen;
    u32 codeSign;
    u32 codeSignVerify;

    u32 preBssDataIntervalCount;
    u32 BssDataOffset;
    u32 BssDataLen;
    u32 BssDataSign;
    u32 BssDataSignVerify;

    u32 preRwDataIntervalCount;
    u32 RwDataOffset;
    u32 RwDataLen;
    u32 RwDataSign;
    u32 RwDataSignVerify;

    u32 DF_DataPacakge_Size;
    u32 DF_Data_Pacakage_Offset;

    u32 entryOffset;
    u32 isBiggianProgram;

} cbeInfo;

extern cbeInfo g_cbeInfo;

int parseCbeHeader(u8 *cbeFileBuffer, u32 size);