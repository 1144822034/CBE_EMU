#ifndef CBE_SCREEN_LIFECYCLE_H
#define CBE_SCREEN_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

static inline bool vm_screen_add_replaces_active(int requestedIndex,
                                                 int activeIndex)
{
    return requestedIndex >= 0 && requestedIndex == activeIndex;
}

static inline uint32_t vm_screen_add_exit_mode(bool replacesActiveScreen,
                                               uint32_t stackDepth,
                                               uint32_t destroyMode,
                                               uint32_t pauseMode)
{
    if (replacesActiveScreen)
        return destroyMode;
    return stackDepth > 1u ? pauseMode : destroyMode;
}

#endif
