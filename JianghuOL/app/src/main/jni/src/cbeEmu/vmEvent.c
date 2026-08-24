#include "vmEvent.h"
#define MAX_VM_EVENT_COUNT 512
#define MAX_VM_EVENT_WAIT_COUNT 512

u32 keyRowIdx = 0;
u32 keyColIdx = 0;
vm_event *firstEvent;
u32 vmIsLock;
u32 VmEventWaitCount;
vm_event VmEventHandleList[MAX_VM_EVENT_COUNT];
vm_event VmEventHandleWaitList[MAX_VM_EVENT_WAIT_COUNT];
vm_event currentEvent;
u32 VmEventCount = 0;
bool VmEventMutex;
vm_event *vmEvent;

void InitVmEvent()
{
    firstEvent = &VmEventHandleList[0];
    VmEventCount = 0;
    VmEventWaitCount = 0;
}

int EnqueueVMEvent(u32 event, u32 r0, u32 r1)
{
    u32 i;
    if (VmEventCount < MAX_VM_EVENT_COUNT)
    {
        if (vmIsLock == 0)
        {
            vmIsLock = 1;
            for (i = 0; i < VmEventWaitCount; i++)
            {
                if (VmEventCount >= MAX_VM_EVENT_COUNT)
                    break;
                VmEventHandleList[VmEventCount++] = VmEventHandleWaitList[i];
            }
            VmEventWaitCount = 0;
            vm_event *evt = &VmEventHandleList[VmEventCount++];
            evt->event = event;
            evt->r0 = r0;
            evt->r1 = r1;
            vmIsLock = 0;
        }
        else
        {
            if (VmEventWaitCount < MAX_VM_EVENT_WAIT_COUNT)
            {
                vm_event *evt = &VmEventHandleWaitList[VmEventWaitCount++];
                evt->event = event;
                evt->r0 = r0;
                evt->r1 = r1;
            }
            else
                printf("WARNING:Max VmEventWaitCount\n");
        }
        return 1;
    }
#ifdef sGDB_SERVER_SUPPORT
    else
    {
        printf("max vm event count\n");
        uc_emu_stop(MTK);
        isBreakPointHit = 1;
    }
#endif
    return 0;
}

inline vm_event *DequeueVMEvent()
{
    vm_event *evt;
    vm_event *ta;
    vm_event *tb;
    u32 i;
    if (VmEventCount > 0 && vmIsLock == 0)
    {
        vmIsLock = 1;
        ta = &VmEventHandleList[0];
        currentEvent.event = ta->event;
        currentEvent.r0 = ta->r0;
        currentEvent.r1 = ta->r1;
        evt = &currentEvent;
        --VmEventCount;
        for (i = 0; i < VmEventCount; i++)
        {
            ta = (&VmEventHandleList[i]);
            tb = ta + 1;
            ta->event = tb->event;
            ta->r0 = tb->r0;
            ta->r1 = tb->r1;
        }
        vmIsLock = 0;
    }
    else
        evt = 0;
    return evt;
}

uint64_t handleTick;

static u32 timer_irq_channel = 14;

inline void handleVmEvent_EMU(uint64_t address)
{
    u32 tmp;
    {
        vmEvent = DequeueVMEvent();
        if (vmEvent > 0)
        {
            switch (vmEvent->event)
            {
            case VM_EVENT_EXIT:
                uc_emu_stop(MTK);
                break;
            }
        }
    }
}
