#include "../main.h"
#include "emulator_runtime_internal.h"

bool vm_manager_network_handle(u32 address)
{
    if (!(address >= VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS && address < (VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS + VM_MANAGER_FUNC_LIST_SIZE)))
        return false;

    u32 tmp1, tmp2, tmp3, tmp4, tmp5;
    u32 netR4 = 0, netR5 = 0, netR6 = 0, netR7 = 0, netLr = 0;

    u32 idx = (address - VM_MANAGER_NETWORK_FUNC_LIST_ADDRESS) / 4;
    uc_reg_read(MTK, UC_ARM_REG_R0, &tmp1);
    uc_reg_read(MTK, UC_ARM_REG_R1, &tmp2);
    uc_reg_read(MTK, UC_ARM_REG_R2, &tmp3);
    uc_reg_read(MTK, UC_ARM_REG_R3, &tmp4);
    uc_reg_read(MTK, UC_ARM_REG_R4, &netR4);
    uc_reg_read(MTK, UC_ARM_REG_R5, &netR5);
    uc_reg_read(MTK, UC_ARM_REG_R6, &netR6);
    uc_reg_read(MTK, UC_ARM_REG_R7, &netR7);
    uc_reg_read(MTK, UC_ARM_REG_LR, &netLr);
    DEBUG_PRINT("[probe_net_idx] idx=%u r0=%x r1=%x r2=%x r3=%x last=%x\n", idx, tmp1, tmp2, tmp3, tmp4, lastAddress);
    vm_net_trace("net_idx idx=%u r0=%08x r1=%08x r2=%08x r3=%08x r4=%08x r5=%08x r6=%08x r7=%08x lr=%08x last=%08x\n",
                 idx, tmp1, tmp2, tmp3, tmp4, netR4, netR5, netR6, netR7, netLr, lastAddress);
    if (idx == 0)
    {
        g_netCurrentObject = netR4;
        tmp5 = g_nextNetConnectId++;
        if (tmp5 == 0)
            tmp5 = g_nextNetConnectId++;
        if (tmp4)
            uc_mem_write(MTK, tmp4, &tmp5, 4);
        scheduler_register_net_channel(tmp5, tmp3, tmp4);
        vm_net_trace("open_channel host=%08x type=%u cb=%08x ctx=%08x connect=%u last=%08x\n", tmp1, tmp2, tmp3, tmp4, tmp5, lastAddress);
        u8 netState = 1;
        uc_mem_write(MTK, Global_R9 + 0x9588 + 0x0c, &netState, 1);
        scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
        vm_set_call_result(1);
    }
    else if (idx == 1)
    {
        if (netR4)
            g_netCurrentObject = netR4;
        vm_net_trace("send_call connect=%u len=%u data=%08x r3=%08x last=%08x\n", tmp1, tmp2, tmp3, tmp4, lastAddress);
        vm_net_mock_on_send(tmp1, tmp3, tmp2);
        vm_set_call_result(tmp2);
    }
    else if (idx == 2)
    {
        vm_net_trace("close_channel connect=%u last=%08x\n", tmp1, lastAddress);
        scheduler_unregister_net_channel(tmp1);
        u8 netState = 0;
        uc_mem_write(MTK, Global_R9 + 0x9588 + 0x0c, &netState, 1);
        vm_set_call_result(0);
    }
    else if (idx == 3)
    {
        tmp1 = vm_get_var(Global_R9 + 0x5a3c + 0x10);
        if (tmp1)
        {
            tmp2 = 0;
            uc_reg_write(MTK, UC_ARM_REG_R0, &tmp2);
            vm_bx(tmp1);
            return true;
        }
        vm_set_call_result(0);
    }
    else if (idx == 6 || idx == 7 || idx == 18)
    {
        vm_net_trace("open_channel_ex idx=%u r0=%08x r1=%08x cb=%08x ctx=%08x last=%08x\n", idx, tmp1, tmp2, tmp3, tmp4, lastAddress);
        scheduler_queue_net_task(tmp1, tmp2, tmp3, tmp4);
        vm_set_call_result(1);
    }
    else if (idx == 17)
    {
        vm_net_trace("http_get_ex r0=%08x cb=%08x ctx=%08x last=%08x\n", tmp1, tmp2, tmp3, lastAddress);
        scheduler_queue_net_task(tmp1, 0, tmp2, tmp3);
        vm_set_call_result(1);
    }
    else if (idx == 4 || idx == 19 || idx == 20 || idx == 29 || idx == 30)
    {
        vm_net_trace("net_success_stub idx=%u r0=%08x r1=%08x r2=%08x r3=%08x last=%08x\n", idx, tmp1, tmp2, tmp3, tmp4, lastAddress);
        vm_set_call_result(1);
    }
    else if (idx == 35)
    {
        g_netUpLinkData = 0;
        g_netDownLinkData = 0;
        g_netMockResponseOffset = 0;
        vm_net_trace("net_data_reset\n");
        vm_set_call_result(0);
    }
    else if (idx == 36)
    {
        if (tmp1)
            uc_mem_write(MTK, tmp1, &g_netUpLinkData, 4);
        if (tmp2)
            uc_mem_write(MTK, tmp2, &g_netDownLinkData, 4);
        vm_net_trace("net_get_data up=%u down=%u upPtr=%08x downPtr=%08x\n", g_netUpLinkData, g_netDownLinkData, tmp1, tmp2);
        vm_set_call_result(g_netDownLinkData);
    }
    else if (idx == 5 || idx == 12 || idx == 13 || idx == 21 || idx == 24 || idx == 25 || idx == 33 || idx == 34 || idx == 37 || idx == 39 || idx == 41 || idx == 42)
    {
        vm_set_call_result(0);
    }
    else if (idx == 8 || idx == 9 || idx == 10 || idx == 11 || idx == 14 || idx == 15 || idx == 16 || idx == 22 || idx == 23 || idx == 26 || idx == 27 || idx == 28 || idx == 31 || idx == 32 || idx == 38 || idx == 40)
    {
        vm_set_call_result(0);
    }
    else
    {
        printf("[impl]vmNetWorkManager调用位置:%d\n", idx);
        assert(0);
    }
    uc_reg_read(MTK, UC_ARM_REG_LR, &tmp1);
    vm_bx(tmp1);
    return true;
}
