#include "../main.h"
#include "emulator_runtime_internal.h"

static u8 g_vmVideoManagerLogged[256];

bool vm_video_handle(u32 address)
{
    if (!(address >= VM_VIDEO_FUNC_LIST_ADDRESS && address < (VM_VIDEO_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 idx = (address - VM_VIDEO_FUNC_LIST_ADDRESS) / 4;
    u32 tmp1 = 0, tmp2 = 0, tmp3 = 0, tmp4 = 0, tmp5 = 0;

    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
    uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp5);

    if (vm_net_mock_env_u32("CBE_STUB_VIDEO_MANAGER", 1))
    {
        if (idx < sizeof(g_vmVideoManagerLogged))
        {
            if (!g_vmVideoManagerLogged[idx])
            {
                g_vmVideoManagerLogged[idx] = 1;
                vm_net_trace("vmVideoManager_stub idx=%u addr=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x last=%08x tick=%u\n",
                             idx, address, tmp1, tmp2, tmp3, tmp4, tmp5, lastAddress, g_schedulerTick);
            }
        }
        else
        {
            vm_net_trace("vmVideoManager_stub idx=%u addr=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x last=%08x tick=%u\n",
                         idx, address, tmp1, tmp2, tmp3, tmp4, tmp5, lastAddress, g_schedulerTick);
        }

        vm_set_call_result(0);
        vm_bx(tmp5);
        return true;
    }

    printf("[call]vmVideoManager idx=%u\n", idx);
    assert(0);
    return true;
}
