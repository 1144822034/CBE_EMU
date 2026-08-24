#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "config.h"

enum
{
    VM_GAME_LAUNCHER_SERVER_MAX = 16,
    VM_GAME_LAUNCHER_SERVER_NAME_CAP = 64,
    VM_GAME_LAUNCHER_SERVER_ENDPOINT_CAP = 96
};

typedef struct {
    char filepath[260];       /* e.g. "bin/CBE/江湖 OL.CBE" */
    char display_name[128];   /* extracted name without dir/suffix */
    u32 file_size;
    int launch_count;         /* usage frequency for sorting */
} GameEntry;

typedef struct {
    char name[VM_GAME_LAUNCHER_SERVER_NAME_CAP];
    char endpoint[VM_GAME_LAUNCHER_SERVER_ENDPOINT_CAP];
} GameLauncherServerEntry;

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
    int header_h;
    int footer_h;
    int viewport_w;
    int viewport_h;
    int scrollbar_w;
    bool active;          /* Public: whether the launcher is currently active. */
    bool loading;         /* Public: rendering "Loading..." overlay, awaiting VM start. */
    char loading_name[128]; /* Name of the game being loaded (shown in overlay). */
    u32 last_click_time;  /* Timestamp of last mouse click for double-click detection. */
    int last_click_index; /* Selected index at last click. */
    bool _internal_is_scrolling;  /* Internal: scrollbar thumb drag in progress. */
    bool frame_dirty;     /* Redraw only when UI state changes (avoids flicker). */

    /* Jianghu OL physical endpoint picker (servers.conf), shown before launch. */
    bool server_select_active;
    char pending_filepath[260];
    char selected_server_name[VM_GAME_LAUNCHER_SERVER_NAME_CAP];
    int server_selected_index;
    int server_count;
    GameLauncherServerEntry servers[VM_GAME_LAUNCHER_SERVER_MAX];
} GameLauncherState;

bool vm_game_launcher_init(GameLauncherState *state, int viewport_w, int viewport_h);
void vm_game_launcher_destroy(GameLauncherState *state);
void vm_game_launcher_render(GameLauncherState *state, void *surface_ptr);
/* Mouse/key handlers return filepath when launch should start, else NULL. */
const char *vm_game_launcher_handle_mouse(GameLauncherState *state, int x, int y, int button);
const char *vm_game_launcher_handle_key(GameLauncherState *state, int key_sym, bool is_down);
int vm_game_launcher_get_selected_index(const GameLauncherState *state);
const char *vm_game_launcher_get_selected_filepath(const GameLauncherState *state);
const char *vm_game_launcher_get_selected_server_name(const GameLauncherState *state);
bool vm_game_launcher_is_active(const GameLauncherState *state);
void vm_game_launcher_set_active(GameLauncherState *state, bool active);

void vm_game_launcher_record_launch(const char *filepath);
void vm_game_launcher_load_history(GameLauncherState *state);
