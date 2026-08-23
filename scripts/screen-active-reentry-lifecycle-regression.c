/*
 * Deterministic host-side regression for AddScreen lifecycle selection.
 * It does not start the emulator, touch guest memory, or connect to a server.
 */

#include <assert.h>
#include <stdio.h>

#include "../src/screen_lifecycle.h"

int main(void)
{
    const unsigned destroyMode = 10u;
    const unsigned pauseMode = 20u;

    assert(vm_screen_add_replaces_active(3, 3));
    assert(!vm_screen_add_replaces_active(2, 3));
    assert(!vm_screen_add_replaces_active(-1, -1));
    assert(vm_screen_add_exit_mode(true, 2u, destroyMode, pauseMode) ==
           destroyMode);
    assert(vm_screen_add_exit_mode(true, 5u, destroyMode, pauseMode) ==
           destroyMode);
    assert(vm_screen_add_exit_mode(false, 2u, destroyMode, pauseMode) ==
           pauseMode);
    assert(vm_screen_add_exit_mode(false, 1u, destroyMode, pauseMode) ==
           destroyMode);

    puts("screen active reentry lifecycle regression passed");
    return 0;
}
