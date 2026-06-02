#include "../main.h"
#include "emulator_runtime_internal.h"

bool vm_manager_screen_handle(u32 address)
{
    if (!(address >= VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS && address < (VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;

    u32 idx = (address - VM_MANAGER_SCREEN_FUNC_LIST_ADDRESS) / 4;
    if (idx == 0)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vm_net_trace("screen_manager idx=0 change r0=%08x last=%08x\n", tmp1, lastAddress);
        uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
        tmp2 = 0;
        uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
        screenStructChange = 1;
        g_screenRemovedWithoutNext = 0;
        vm_set_call_result(VM_SCREEN_isInQuit_ADDRESS);
    }
    else if (idx == 2)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
        uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
        if (tmp1 != 0 && tmp1 != vmAddedScreen)
        {
            vm_net_trace("screen_manager idx=2 change r0=%08x r1=%08x r2=%08x r3=%08x depth=%u active=%08x last=%08x\n",
                         tmp1, tmp2, tmp3, tmp4, g_screenStackCount, vmAddedScreen, lastAddress);
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
            tmp2 = 0;
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
        }
        else
        {
            vm_net_trace("screen_manager idx=2 same_current r0=%08x r1=%08x r2=%08x r3=%08x depth=%u active=%08x last=%08x\n",
                         tmp1, tmp2, tmp3, tmp4, g_screenStackCount, vmAddedScreen, lastAddress);
        }
        vm_set_call_result(VM_SCREEN_isInQuit_ADDRESS);
    }
    else if (idx == 4)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        vmAddedScreen = tmp1;
        u32 moduleBase = 0;
        if (lastAddress >= VM_Memory_Pool_ADDRESS && lastAddress < VM_Memory_Pool_ADDRESS + VM_MEMPOOL_TOTAL_SIZE)
        {
            uc_reg_read(MTK, UC_ARM_REG_R9, &moduleBase);
        }
        vm_screen_stack_push(tmp1, moduleBase);
        vm_net_trace("screen_manager idx=4 add r0=%08x depth=%u moduleBase=%08x last=%08x\n", tmp1, g_screenStackCount, moduleBase, lastAddress);
        if (moduleBase)
        {
            g_currentScreenThis = tmp1 - 0x18;
            g_currentScreenModuleBase = moduleBase;
            vm_net_trace("screen_manager idx=4 module_context this=%08x r9=%08x last=%08x\n",
                         g_currentScreenThis, g_currentScreenModuleBase, lastAddress);
        }
        u32 startupObj = 0;
        uc_mem_read(MTK, Global_R9 + 0x9928 + 0x10, &startupObj, 4);
        if (startupObj == 0 && tmp1 != 0 && g_lastStartupScreenState != 0xff)
        {
            tmp2 = 0;
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp1, 4);
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &tmp2, 4);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
            vm_net_trace("screen_manager idx=4 promote_to_change screen=%08x last=%08x\n", tmp1, lastAddress);
        }
        vm_set_call_result(0);
    }
    else if (idx == 5)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        vm_net_trace("screen_manager idx=5 query r0=%08x r1=%08x ret=1 depth=%u last=%08x\n", tmp1, tmp2, g_screenStackCount, lastAddress);
        vm_set_call_result(1);
    }
    else if (idx == 6)
    {
        uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
        uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
        tmp3 = 0;
        tmp5 = 0;
        tmp4 = vm_screen_stack_remove(tmp1, &tmp3, &tmp5) ? 1 : 0;
        vm_net_trace("screen_manager idx=6 remove r0=%08x r1=%08x ret=%u newTop=%08x newTopModule=%08x depth=%u last=%08x\n",
                     tmp1, tmp2, tmp4, tmp3, tmp5, g_screenStackCount, lastAddress);
        if (tmp4 && tmp1 == vmAddedScreen && tmp3)
        {
            g_activeScreenRemovedThisFrame = 1;
            g_screenResumeExisting = 1;
            vmAddedScreen = tmp3;
            g_currentScreenThis = tmp3 - 0x18;
            g_currentScreenModuleBase = tmp5;
            u32 isInQuit = 0;
            uc_mem_write(MTK, VM_SCREEN_nextSubTScreen_ADDRESS, &tmp3, 4);
            uc_mem_write(MTK, VM_SCREEN_isInQuit_ADDRESS, &isInQuit, 4);
            screenStructChange = 1;
            g_screenRemovedWithoutNext = 0;
            vm_net_trace("screen_manager idx=6 promote_current_remove screen=%08x moduleBase=%08x depth=%u resume=%u last=%08x\n",
                         tmp3, g_currentScreenModuleBase, g_screenStackCount, g_screenResumeExisting, lastAddress);
        }
        else if (tmp4 && tmp1 == vmAddedScreen)
        {
            g_activeScreenRemovedThisFrame = 1;
            vmAddedScreen = 0;
            g_screenRemovedWithoutNext = 1;
            vm_net_trace("screen_manager idx=6 active_removed_wait depth=%u last=%08x\n",
                         g_screenStackCount, lastAddress);
        }
        vm_set_call_result(tmp4);
    }
    else
    {
        printf("[impl]vmScreenManager调用位置:%d\n", idx);
        assert(0);
    }
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}
