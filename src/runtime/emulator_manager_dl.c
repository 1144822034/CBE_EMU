#include "../main.h"
#include "emulator_runtime_internal.h"

static bool vm_manager_dl_stub_handle(const char *label, const char *envName, u32 address, u32 base, u32 count)
{
    if (!(address >= base && address < (base + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 idx = (address - base) / 4;
    u32 tmp1 = 0, tmp2 = 0, tmp3 = 0, tmp4 = 0, tmp5 = 0;

    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
    uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp5);

    if (count != 0 && idx >= count)
    {
        printf("[impl]%s调用位置:%u\n", label, idx);
        assert(0);
    }

    if (!vm_net_mock_env_u32(envName, 1))
    {
        printf("[call]%s idx=%u\n", label, idx);
        assert(0);
    }

    vm_net_trace("%s_stub idx=%u addr=%08x r0=%08x r1=%08x r2=%08x r3=%08x lr=%08x last=%08x tick=%u\n",
                 label, idx, address, tmp1, tmp2, tmp3, tmp4, tmp5, lastAddress, g_schedulerTick);
    vm_set_call_result(0);
    vm_bx(tmp5);
    return true;
}

bool vm_dl_load_handle(u32 address)
{
    return vm_manager_dl_stub_handle("vmDlLoadManager", "CBE_STUB_DL_LOAD_MANAGER", address, VM_DL_LOAD_FUNC_LIST_ADDRESS, 11);
}

bool vm_dl_pay_handle(u32 address)
{
    return vm_manager_dl_stub_handle("vmDlPayManager", "CBE_STUB_DL_PAY_MANAGER", address, VM_DL_PAY_FUNC_LIST_ADDRESS, 16);
}

bool vm_dl_rs_handle(u32 address)
{
    return vm_manager_dl_stub_handle("vmDlRsManager", "CBE_STUB_DL_RS_MANAGER", address, VM_DL_RS_FUNC_LIST_ADDRESS, 20);
}

bool vm_dl_image_handle(u32 address)
{
    return vm_manager_dl_stub_handle("vmDlImageManager", "CBE_STUB_DL_IMAGE_MANAGER", address, VM_DL_IMAGE_FUNC_LIST_ADDRESS, 12);
}
