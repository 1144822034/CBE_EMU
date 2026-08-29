/*
 * Host-only regression for the SDL text-input opening-key release.
 * It starts no VM or window.  The fixture models the already-confirmed CBE
 * input lifecycle around the host boundary: the opening key is held when the
 * CBE asks for text input, its SDL_KEYUP must clear only the host latch, and
 * after input completion the next normal key must still deliver press/release
 * through the existing VM hardware queue.
 */

#include <stdio.h>

#define main cbe_client_program_main
#include "../src/main.c"
#undef main

static int expect_keyboard_event(int key, int isPress)
{
    vm_event *event = DequeueVMEvent();

    if (event == NULL || event->event != VM_EVENT_KEYBOARD ||
        event->r0 != (u32)key || event->r1 != (u32)isPress)
    {
        fprintf(stderr, "unexpected keyboard event key=%d press=%d\n", key,
                isPress);
        return 1;
    }
    return 0;
}

int main(void)
{
    const SDL_Keycode openingKey = SDLK_f;
    const SDL_Keycode nextKey = SDLK_s;

    InitVmEvent();
    vmIsLock = 0;
    isKeyDown = openingKey;
    g_vmInputOpen = 1;

    /* This KEYUP arrives after the CBE host-input API opened the text editor.
     * The release must not reach CBE while text mode owns keyboard input, but
     * it must release the SDL latch that otherwise blocks all future keydowns. */
    vm_host_handle_key_up(openingKey);
    if (isKeyDown != SDLK_UNKNOWN || DequeueVMEvent() != NULL)
    {
        fputs("text-input opening key was not released at the host boundary\n",
              stderr);
        return 1;
    }

    /* vm_input_finish() has already cleared this flag before calling its CBE
     * callback.  Model that documented host state without calling a CBE
     * callback, so the test cannot bypass or alter client business logic. */
    g_vmInputOpen = 0;
    if (isKeyDown != SDLK_UNKNOWN)
    {
        fputs("input completion did not leave the SDL latch available\n", stderr);
        return 1;
    }

    /* Mirror the normal SDL_KEYDOWN gate: a stale opening-key latch would
     * skip this whole block, which is the user-visible failure. */
    if (isKeyDown == SDLK_UNKNOWN)
    {
        isKeyDown = nextKey;
        keyEvent(MR_KEY_PRESS, nextKey);
    }
    vm_host_handle_key_up(nextKey);
    if (isKeyDown != SDLK_UNKNOWN ||
        expect_keyboard_event(18, 1) != 0 ||
        expect_keyboard_event(18, 0) != 0 || DequeueVMEvent() != NULL)
    {
        fputs("normal input did not recover after text submission\n", stderr);
        return 1;
    }

    puts("SDL text-input key-release regression passed: opening release suppressed, next key delivered");
    return 0;
}
