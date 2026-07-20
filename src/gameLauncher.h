#pragma once
#include <stdbool.h>
#include "config.h"

typedef struct {
    char filepath[260];       /* e.g. "bin/CBE/江湖 OL.CBE" */
    char display_name[128];   /* extracted name without dir/suffix */
    u32 file_size;
} GameEntry;

typedef struct {
    GameEntry *entries;
    int count;
    int capacity;
    int selected_index;
    float scroll_offset;
    int columns;
    int rows_visible;
    int tile_w;
    int tile_h;
    int gap;
    int margin_x;
    int margin_y;
    int header_h;
    int footer_h;
    int viewport_w;
    int viewport_h;
    int scrollbar_w;
    bool active;
    bool is_scrolling;
} GameLauncherState;

bool vm_game_launcher_init(GameLauncherState *state, int viewport_w, int viewport_h);
void vm_game_launcher_destroy(GameLauncherState *state);
void vm_game_launcher_render(GameLauncherState *state, void *surface_ptr);
bool vm_game_launcher_handle_mouse(GameLauncherState *state, int x, int y, int button);
bool vm_game_launcher_handle_key(GameLauncherState *state, int key_sym, bool is_down);
int vm_game_launcher_get_selected_index(const GameLauncherState *state);
const char *vm_game_launcher_get_selected_filepath(const GameLauncherState *state);
bool vm_game_launcher_is_active(const GameLauncherState *state);
void vm_game_launcher_set_active(GameLauncherState *state, bool active);
