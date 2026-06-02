#include "../main.h"
#include "emulator_runtime_internal.h"

bool vm_manager_audio_handle(u32 address)
{
    if (!(address >= VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS && address < (VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_AUDIO_FUNC_LIST_ADDRESS) / 4;
    idx += 1;
    if (idx == 1)
    {
        DEBUG_PRINT("[call]vMAudioSetVolume\n");
    }
    else if (idx == 2)
    {
        printf("[call]vMAudioPlayByData\n");
        assert(0);
    }
    else if (idx == 3)
    {
        printf("[call]vMAudioPlayWithDataPackage\n");
        assert(0);
    }
    else if (idx == 4)
    {
        DEBUG_PRINT("[call]vMAudioPlayForGame(a1,a2)\n");
        tmp1 = 0;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 5)
    {
        printf("[call]vMAudioPlayForApp\n");
        assert(0);
    }
    else if (idx == 6)
    {
        printf("[call]vMAudioPause\n");
        assert(0);
    }
    else if (idx == 7)
    {
        printf("[call]vMAudioResume\n");
        assert(0);
    }
    else if (idx == 8)
    {
        DEBUG_PRINT("[call]vMAudioStop\n");
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 9)
    {
        tmp1 = 1;
        uc_reg_write(MTK, UC_ARM_REG_R0, &tmp1);
    }
    else if (idx == 10)
    {
        printf("[call]vm_mp3PlayBystream\n");
        assert(0);
    }
    else if (idx == 11)
    {
        printf("[call]vm_mp3PauseByStream\n");
        assert(0);
    }
    else if (idx == 12)
    {
        printf("[call]vm_mp3ResumeByStream\n");
        assert(0);
    }
    else if (idx == 13)
    {
        printf("[call]vm_mp3StopBystream\n");
        assert(0);
    }
    else if (idx == 14)
    {
        printf("[call]vm_mp3PlayByFile\n");
        assert(0);
    }
    else if (idx == 15)
    {
        printf("[call]vm_mp3PauseByFile\n");
        assert(0);
    }
    else if (idx == 16)
    {
        printf("[call]vm_mp3ResumeByFile\n");
        assert(0);
    }
    else if (idx == 17)
    {
        printf("[call]vm_mp3StopByFile\n");
        assert(0);
    }
    else if (idx == 18)
    {
        printf("[call]vMAudioget_progress_time\n");
        assert(0);
    }
    else if (idx == 19)
    {
        printf("[call]vmMp3StreamInit\n");
        assert(0);
    }
    else if (idx == 20)
    {
        printf("[call]CB_AUD_StartPlay_Init\n");
        assert(0);
    }
    else if (idx == 21)
    {
        printf("[call]CB_AUD_StopPlay\n");
        assert(0);
    }
    else if (idx == 22)
    {
        printf("[call]CB_AUD_WriteVoiceData\n");
        assert(0);
    }
    else if (idx == 23)
    {
        printf("[call]vMStartAudioRecord_async\n");
        assert(0);
    }
    else if (idx == 24)
    {
        printf("[call]vMStopAudioRecord_async\n");
        assert(0);
    }
    else if (idx == 25)
    {
        printf("[call]vMSetAmrRecBS\n");
        assert(0);
    }
    else if (idx == 26)
    {
        printf("[call]vm_mp3PlayByFileEx\n");
        assert(0);
    }
    else if (idx == 27)
    {
        printf("[call]vMStartAudioRecordEx\n");
        assert(0);
    }
    else if (idx == 28)
    {
        printf("[call]vMStopAudioRecordEx\n");
        assert(0);
    }
    else if (idx == 29)
    {
        printf("[call]CB_AUD_StartPlay_InitEx\n");
        assert(0);
    }
    else if (idx == 30)
    {
        printf("[call]CB_AUD_StartPlayEx\n");
        assert(0);
    }
    else if (idx == 31)
    {
        printf("[call]CB_AUD_StopPlayEx\n");
        assert(0);
    }
    else
    {
        printf("[impl]vmAudioManager调用位置:%d\n", idx);
        assert(0);
    }

    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}
