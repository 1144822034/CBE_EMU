#include "vmMalloc.h"

VMBlock *vm_head;

void InitVmMalloc()
{
    vm_head = (VMBlock *)SDL_malloc(sizeof(VMBlock));
    vm_head->addr = VM_MALLOC_POOL_ADDRESS;
    vm_head->size = VM_MemoryBlock_SIZE;
    vm_head->used = 0;
    vm_head->next = NULL;
}
// ok
u32 vm_malloc(u32 size)
{
    VMBlock *cur = vm_head;

    // 8字节对齐（很重要）
    size = (size + 7) & ~7;

    while (cur)
    {
        if (!cur->used && cur->size >= size)
        {
            // 分裂 block
            if (cur->size > size)
            {
                VMBlock *new_block = (VMBlock *)SDL_malloc(sizeof(VMBlock));
                new_block->addr = cur->addr + size;
                new_block->size = cur->size - size;
                new_block->used = 0;
                new_block->next = cur->next;

                cur->next = new_block;
            }

            cur->size = size;
            cur->used = 1;

            // printf("[vm_malloc] addr=0x%08X size=%u\n", cur->addr, size);

            return cur->addr;
        }

        cur = cur->next;
    }
    printf("[vm_malloc] FAILED size=%u\n", size);
    assert(0);
    return 0;
}
// ok
void vm_free(u32 addr)
{
    VMBlock *cur = vm_head;

    while (cur)
    {
        if (cur->addr == addr)
        {
            cur->used = 0;

            // printf("[vm_free] addr=0x%08X\n", addr);

            // 合并相邻空闲块
            VMBlock *next = cur->next;
            if (next && !next->used)
            {
                cur->size += next->size;
                cur->next = next->next;
                SDL_free(next);
            }

            return;
        }

        cur = cur->next;
    }
    printf("[vm_free] INVALID addr=0x%08X\n", addr);
    assert(0);
}
