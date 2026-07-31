#include "gameLauncher.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#else
#include <strings.h>
#endif
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"
#include "fontEngine.h"
#include "fileIoEngine.h"
#include "mystd.h"

static void launcher_put_pixel(SDL_Surface *sfc, int x, int y, u32 color)
{
    if (!sfc || x < 0 || y < 0 || x >= sfc->w || y >= sfc->h) return;
    u32 *row = (u32 *)((u8 *)sfc->pixels + y * sfc->pitch);
    row[x] = color;
}

static void launcher_draw_font_char(SDL_Surface *sfc, u16 gbCode, int x, int y, u32 color)
{
    int bw = getFontWidth();
    int bh = getFontHeight();
    int bmpSize = (bw * bh) / 8;
    char *bmp = SDL_malloc(bmpSize);
    if (!bmp) return;
    if (!getFontBitMap(gbCode, bmp)) { SDL_free(bmp); return; }
    int lp = bw / 8;
    for (int j = 0; j < bh; j++) {
        int py = y + j;
        if (py < 0 || py >= sfc->h) continue;
        for (int i = 0; i < bw; i++) {
            int px = x + i;
            if (px < 0 || px >= sfc->w) continue;
            int byteIdx = j * lp + i / 8;
            int bitIdx = 7 - (i % 8);
            if ((bmp[byteIdx] >> bitIdx) & 1)
                launcher_put_pixel(sfc, px, py, color);
        }
    }
    SDL_free(bmp);
}

static void launcher_draw_font_string(SDL_Surface *sfc, const char *gbkStr, int x, int y, u32 color)
{
    if (!gbkStr) return;
    int cw = getFontCellWidth();
    int fw = getFontWidth();
    int ri = 0;
    for (u32 i = 0; gbkStr[i] != 0;) {
        u8 ch = (u8)gbkStr[i];
        if (ch < 0x80) {
            launcher_draw_font_char(sfc, (u16)(ch << 8), x + ri, y, color);
            ri += cw;
            i++;
        } else {
            u16 code = (u16)ch | ((u16)(u8)gbkStr[i + 1] << 8);
            launcher_draw_font_char(sfc, code, x + ri, y, color);
            ri += fw;
            i += 2;
        }
    }
}

static int launcher_measure_string_width(const char *gbkStr)
{
    if (!gbkStr) return 0;
    int w = 0;
    int cw = getFontCellWidth();
    int fw = getFontWidth();
    for (u32 i = 0; gbkStr[i] != 0;) {
        u8 ch = (u8)gbkStr[i];
        if (ch < 0x80) { w += cw; i++; }
        else { w += fw; i += 2; }
    }
    return w;
}

/* Layout constants — list style for 240×400 feature-phone viewport */
#define LAUNCHER_TILE_W 220
#define LAUNCHER_TILE_H 44
#define LAUNCHER_GAP 6
#define LAUNCHER_MARGIN_X 10
#define LAUNCHER_HEADER_H 38
#define LAUNCHER_FOOTER_H 30
#define LAUNCHER_SCROLLBAR_W 4

/* Ink / jade palette (ARGB via MapRGB; high byte set by MapRGB) */
#define LC_BG        0x10161c
#define LC_PANEL     0x1a222c
#define LC_TILE      0x1c2630
#define LC_TILE_SEL  0x243848
#define LC_BORDER    0x2e3a46
#define LC_ACCENT    0xc4a35a
#define LC_JADE      0x4a9b8c
#define LC_TEXT      0xe8e0d0
#define LC_MUTED     0x8a9299
#define LC_DIM       0x0a0e12

static int cmp_entries(const void *a, const void *b)
{
    const GameEntry *ea = (const GameEntry *)a;
    const GameEntry *eb = (const GameEntry *)b;
    if (eb->launch_count != ea->launch_count)
        return eb->launch_count - ea->launch_count;
    return strcmp(ea->display_name, eb->display_name);
}

static void extract_display_name(const char *filepath, char *out, size_t out_size)
{
    const char *base = strrchr(filepath, '/');
    if (!base) base = strrchr(filepath, '\\');
    if (base) base++; else base = filepath;

    size_t len = strlen(base);
    if (len > 4 && strcasecmp(base + len - 4, ".CBE") == 0)
        len -= 4;

    if (len >= out_size) len = out_size - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

static int count_cbe_files(const char *dir_path)
{
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t nlen = strlen(ent->d_name);
        if (nlen > 4 && strcasecmp(ent->d_name + nlen - 4, ".CBE") == 0)
            count++;
    }
    closedir(d);
    return count;
}

static float vm_game_launcher_compute_total_height(const GameLauncherState *state)
{
    int total_rows = (state->count + state->columns - 1) / state->columns;
    return (float)(total_rows * (state->tile_h + state->gap));
}

static float vm_game_launcher_compute_max_scroll(const GameLauncherState *state);
static void draw_rounded_rect(SDL_Surface *sfc, int x, int y, int w, int h,
                              u32 color, int radius);
static void draw_rect_border(SDL_Surface *sfc, int x, int y, int w, int h,
                             u32 color);
static void launcher_mark_dirty(GameLauncherState *state)
{
    if (state)
        state->frame_dirty = true;
}

static void launcher_trim(char *text)
{
    size_t len;
    char *start;

    if (text == NULL)
        return;
    start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')
        ++start;
    if (start != text)
        memmove(text, start, strlen(start) + 1);
    len = strlen(text);
    while (len > 0 &&
           (text[len - 1] == ' ' || text[len - 1] == '\t' ||
            text[len - 1] == '\r' || text[len - 1] == '\n'))
    {
        text[--len] = 0;
    }
}

/* Value format: host:port[,account_web_url].  Web URL is optional and
 * ignored by the desktop launcher; Android uses it for account buttons. */
static void launcher_split_endpoint_value(const char *value, char *endpoint,
                                          size_t endpointCap)
{
    const char *comma;
    size_t endpointLen;

    if (endpoint == NULL || endpointCap == 0)
        return;
    endpoint[0] = 0;
    if (value == NULL || value[0] == 0)
        return;
    comma = strchr(value, ',');
    if (comma != NULL)
    {
        endpointLen = (size_t)(comma - value);
        if (endpointLen >= endpointCap)
            endpointLen = endpointCap - 1;
        memcpy(endpoint, value, endpointLen);
        endpoint[endpointLen] = 0;
        launcher_trim(endpoint);
        return;
    }
    snprintf(endpoint, endpointCap, "%s", value);
    launcher_trim(endpoint);
}

static int launcher_load_servers_conf(GameLauncherState *state)
{
    static const char *paths[] = {
        "servers.conf",
        "bin/servers.conf",
        "JHOnlineData/servers.conf"
    };
    char current[VM_GAME_LAUNCHER_SERVER_NAME_CAP];
    FILE *fp = NULL;
    char line[256];
    int defaultIndex = 0;

    if (state == NULL)
        return 0;
    state->server_count = 0;
    state->server_selected_index = 0;
    memset(current, 0, sizeof(current));
    memset(state->servers, 0, sizeof(state->servers));
    for (u32 i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i)
    {
        fp = fopen(paths[i], "rb");
        if (fp != NULL)
            break;
    }
    if (fp == NULL)
        return 0;
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char *eq;
        char key[VM_GAME_LAUNCHER_SERVER_NAME_CAP];
        char value[240];
        char endpoint[VM_GAME_LAUNCHER_SERVER_ENDPOINT_CAP];

        launcher_trim(line);
        if (line[0] == 0 || line[0] == '#' || line[0] == ';')
            continue;
        if ((unsigned char)line[0] == 0xEFu &&
            (unsigned char)line[1] == 0xBBu &&
            (unsigned char)line[2] == 0xBFu)
        {
            memmove(line, line + 3, strlen(line + 3) + 1);
            launcher_trim(line);
            if (line[0] == 0 || line[0] == '#' || line[0] == ';')
                continue;
        }
        eq = strchr(line, '=');
        if (eq == NULL)
            continue;
        *eq = 0;
        snprintf(key, sizeof(key), "%s", line);
        snprintf(value, sizeof(value), "%s", eq + 1);
        launcher_trim(key);
        launcher_trim(value);
        if (key[0] == 0 || value[0] == 0)
            continue;
        if (strcmp(key, "current") == 0)
        {
            snprintf(current, sizeof(current), "%s", value);
            continue;
        }
        if (state->server_count >= VM_GAME_LAUNCHER_SERVER_MAX)
            break;
        launcher_split_endpoint_value(value, endpoint, sizeof(endpoint));
        if (endpoint[0] == 0)
            continue;
        snprintf(state->servers[state->server_count].name,
                 sizeof(state->servers[state->server_count].name), "%s", key);
        snprintf(state->servers[state->server_count].endpoint,
                 sizeof(state->servers[state->server_count].endpoint), "%s",
                 endpoint);
        ++state->server_count;
    }
    fclose(fp);
    if (current[0] != 0)
    {
        for (int i = 0; i < state->server_count; ++i)
        {
            if (strcmp(state->servers[i].name, current) == 0)
            {
                defaultIndex = i;
                break;
            }
        }
    }
    state->server_selected_index = defaultIndex;
    return state->server_count;
}

static bool launcher_is_jianghu_ol(const char *filepath)
{
    static const char jholGbk[] = {(char)0xBD, (char)0xAD, (char)0xBA, (char)0xFE,
                                   'O', 'L', 0};
    const char *base;

    if (filepath == NULL || filepath[0] == 0)
        return false;
    base = strrchr(filepath, '/');
    if (!base)
        base = strrchr(filepath, '\\');
    base = base ? base + 1 : filepath;
    if (strstr(base, "江湖OL") != NULL || strstr(base, "江湖ol") != NULL)
        return true;
    if (strstr(base, jholGbk) != NULL)
        return true;
    if (strstr(base, "Jianghu") != NULL || strstr(base, "jianghu") != NULL ||
        strstr(base, "JHOnline") != NULL || strstr(base, "jhonline") != NULL)
        return true;
    return false;
}

static void launcher_draw_utf8(SDL_Surface *sfc, const char *utf8, int x, int y,
                               u32 color)
{
    char gbk[256];

    if (utf8 == NULL)
        return;
    memset(gbk, 0, sizeof(gbk));
    utf8_to_gbk((u8 *)utf8, (u8 *)gbk, sizeof(gbk));
    launcher_draw_font_string(sfc, gbk, x, y, color);
}

static int launcher_measure_utf8(const char *utf8)
{
    char gbk[256];

    if (utf8 == NULL)
        return 0;
    memset(gbk, 0, sizeof(gbk));
    utf8_to_gbk((u8 *)utf8, (u8 *)gbk, sizeof(gbk));
    return launcher_measure_string_width(gbk);
}

static bool launcher_begin_server_select(GameLauncherState *state,
                                         const char *filepath)
{
    if (state == NULL || filepath == NULL)
        return false;
    if (!launcher_is_jianghu_ol(filepath))
        return false;
    if (launcher_load_servers_conf(state) <= 0)
        return false;
    state->server_select_active = true;
    launcher_mark_dirty(state);
    snprintf(state->pending_filepath, sizeof(state->pending_filepath), "%s",
             filepath);
    state->selected_server_name[0] = 0;
    return true;
}

static const char *launcher_confirm_server_select(GameLauncherState *state)
{
    const GameLauncherServerEntry *row;

    if (state == NULL || !state->server_select_active ||
        state->server_count <= 0 || state->pending_filepath[0] == 0)
        return NULL;
    if (state->server_selected_index < 0 ||
        state->server_selected_index >= state->server_count)
        state->server_selected_index = 0;
    row = &state->servers[state->server_selected_index];
    snprintf(state->selected_server_name, sizeof(state->selected_server_name),
             "%s", row->name);
    state->server_select_active = false;
    launcher_mark_dirty(state);
    return state->pending_filepath;
}

static void launcher_cancel_server_select(GameLauncherState *state)
{
    if (state == NULL)
        return;
    state->server_select_active = false;
    state->pending_filepath[0] = 0;
    state->selected_server_name[0] = 0;
    launcher_mark_dirty(state);
    launcher_mark_dirty(state);
}

static const char *launcher_try_launch_path(GameLauncherState *state,
                                            const char *filepath)
{
    if (state == NULL || filepath == NULL)
        return NULL;
    if (launcher_begin_server_select(state, filepath))
        return NULL;
    state->selected_server_name[0] = 0;
    return filepath;
}

static void launcher_dim_surface(SDL_Surface *sfc)
{
    int x, y;
    if (sfc == NULL || sfc->pixels == NULL)
        return;
    for (y = 0; y < sfc->h; ++y)
    {
        u32 *row = (u32 *)((u8 *)sfc->pixels + y * sfc->pitch);
        for (x = 0; x < sfc->w; ++x)
        {
            u32 c = row[x];
            u32 r = ((c >> 16) & 0xff) >> 2;
            u32 g = ((c >> 8) & 0xff) >> 2;
            u32 b = (c & 0xff) >> 2;
            row[x] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

static void launcher_render_server_select(GameLauncherState *state,
                                          SDL_Surface *sfc)
{
    int panel_w;
    int panel_h;
    int panel_x;
    int panel_y;
    int row_h = 34;
    int list_top;
    int title_w;
    const char *title = "选择区服";
#ifdef CBE_PLATFORM_ANDROID
    const char *hint = "轻触进入";
#else
    const char *hint = "双击进入  Esc取消";
#endif

    if (state == NULL || sfc == NULL || !state->server_select_active)
        return;

    launcher_dim_surface(sfc);
    panel_w = state->viewport_w - 28;
    if (panel_w > 212)
        panel_w = 212;
    panel_h = 52 + state->server_count * row_h + 30;
    if (panel_h > state->viewport_h - 24)
        panel_h = state->viewport_h - 24;
    panel_x = (state->viewport_w - panel_w) / 2;
    panel_y = (state->viewport_h - panel_h) / 2;
    draw_rounded_rect(sfc, panel_x, panel_y, panel_w, panel_h, LC_PANEL, 6);
    draw_rect_border(sfc, panel_x, panel_y, panel_w, panel_h,
                     SDL_MapRGB(sfc->format, (LC_ACCENT >> 16) & 0xff,
                                (LC_ACCENT >> 8) & 0xff, LC_ACCENT & 0xff));
    draw_rounded_rect(sfc, panel_x + 1, panel_y + 1, panel_w - 2, 3, LC_ACCENT,
                      0);

    title_w = launcher_measure_utf8(title);
    launcher_draw_utf8(sfc, title, panel_x + (panel_w - title_w) / 2,
                       panel_y + 12,
                       SDL_MapRGB(sfc->format, (LC_TEXT >> 16) & 0xff,
                                  (LC_TEXT >> 8) & 0xff, LC_TEXT & 0xff));
    list_top = panel_y + 36;
    for (int i = 0; i < state->server_count; ++i)
    {
        int row_y = list_top + i * row_h;
        char name_gbk[128];
        u32 bg;
        u32 fg;
        int name_w;
        int tx;

        if (row_y + row_h > panel_y + panel_h - 26)
            break;
        bg = (i == state->server_selected_index) ? LC_TILE_SEL : LC_TILE;
        fg = SDL_MapRGB(sfc->format, (LC_TEXT >> 16) & 0xff,
                        (LC_TEXT >> 8) & 0xff, LC_TEXT & 0xff);
        draw_rounded_rect(sfc, panel_x + 10, row_y, panel_w - 20, row_h - 4, bg,
                          4);
        if (i == state->server_selected_index)
        {
            draw_rounded_rect(sfc, panel_x + 10, row_y + 6, 3, row_h - 16,
                              LC_JADE, 0);
            draw_rect_border(sfc, panel_x + 10, row_y, panel_w - 20, row_h - 4,
                             SDL_MapRGB(sfc->format, (LC_ACCENT >> 16) & 0xff,
                                        (LC_ACCENT >> 8) & 0xff,
                                        LC_ACCENT & 0xff));
        }
        memset(name_gbk, 0, sizeof(name_gbk));
        utf8_to_gbk((u8 *)state->servers[i].name, (u8 *)name_gbk,
                    sizeof(name_gbk));
        /* Only show zone name — hide host:port. */
        name_w = launcher_measure_string_width(name_gbk);
        tx = panel_x + (panel_w - name_w) / 2;
        if (tx < panel_x + 18)
            tx = panel_x + 18;
        launcher_draw_font_string(sfc, name_gbk, tx,
                                  row_y + (row_h - 4 - getFontHeight()) / 2, fg);
    }
    {
        int hw = launcher_measure_utf8(hint);
        launcher_draw_utf8(sfc, hint, panel_x + (panel_w - hw) / 2,
                           panel_y + panel_h - 20,
                           SDL_MapRGB(sfc->format, (LC_MUTED >> 16) & 0xff,
                                      (LC_MUTED >> 8) & 0xff, LC_MUTED & 0xff));
    }
}

static const char *launcher_handle_server_select_mouse(GameLauncherState *state,
                                                       int x, int y,
                                                       int button)
{
    int panel_w;
    int panel_h;
    int panel_x;
    int panel_y;
    int row_h = 34;
    int list_top;

    if (state == NULL || !state->server_select_active || button != 1)
        return NULL;
    panel_w = state->viewport_w - 28;
    if (panel_w > 212)
        panel_w = 212;
    panel_h = 52 + state->server_count * row_h + 30;
    if (panel_h > state->viewport_h - 24)
        panel_h = state->viewport_h - 24;
    panel_x = (state->viewport_w - panel_w) / 2;
    panel_y = (state->viewport_h - panel_h) / 2;
    list_top = panel_y + 36;
    for (int i = 0; i < state->server_count; ++i)
    {
        int row_y = list_top + i * row_h;
        if (x >= panel_x + 10 && x < panel_x + panel_w - 10 && y >= row_y &&
            y < row_y + row_h - 4)
        {
#ifdef CBE_PLATFORM_ANDROID
            state->server_selected_index = i;
            return launcher_confirm_server_select(state);
#else
            u32 now = SDL_GetTicks();
            if (i == state->server_selected_index &&
                now - state->last_click_time <= 300 &&
                state->last_click_index == 1000 + i)
            {
                return launcher_confirm_server_select(state);
            }
            state->server_selected_index = i;
            state->last_click_time = now;
            state->last_click_index = 1000 + i;
            launcher_mark_dirty(state);
            return NULL;
#endif
        }
    }
    return NULL;
}

static const char *launcher_handle_server_select_key(GameLauncherState *state,
                                                     int key_sym)
{
    if (state == NULL || !state->server_select_active)
        return NULL;
    switch (key_sym)
    {
    case SDLK_UP:
    case SDLK_w:
        if (state->server_selected_index > 0)
        {
            --state->server_selected_index;
            launcher_mark_dirty(state);
        }
        return NULL;
    case SDLK_DOWN:
    case SDLK_s:
        if (state->server_selected_index + 1 < state->server_count)
        {
            ++state->server_selected_index;
            launcher_mark_dirty(state);
        }
        return NULL;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_q:
    case SDLK_f:
        return launcher_confirm_server_select(state);
    case SDLK_ESCAPE:
        launcher_cancel_server_select(state);
        return NULL;
    default:
        return NULL;
    }
}

bool vm_game_launcher_init(GameLauncherState *state, int viewport_w, int viewport_h)
{
    if (!state || viewport_w <= 0 || viewport_h <= 0) return false;

    memset(state, 0, sizeof(*state));
    state->viewport_w = viewport_w;
    state->viewport_h = viewport_h;
    state->tile_h = LAUNCHER_TILE_H;
    state->gap = LAUNCHER_GAP;
    state->margin_x = LAUNCHER_MARGIN_X;
    state->header_h = LAUNCHER_HEADER_H;
    state->footer_h = LAUNCHER_FOOTER_H;
    state->scrollbar_w = LAUNCHER_SCROLLBAR_W;
    state->frame_dirty = true;

    /* Single-column list reads better on 240-wide screens. */
    {
        int available_w = viewport_w - 2 * state->margin_x - state->scrollbar_w;
        if (available_w < 80)
            available_w = 80;
        state->columns = 1;
        state->tile_w = available_w;
    }

    int available_h = viewport_h - state->header_h - state->footer_h;
    state->rows_visible = available_h / (state->tile_h + state->gap);
    if (state->rows_visible < 1) state->rows_visible = 1;

    const char *scan_dir = "bin/CBE";
    if (!dirExists((char *)scan_dir)) scan_dir = "CBE";

    int max_entries = count_cbe_files((char *)scan_dir);
    if (max_entries <= 0) max_entries = 64;

    state->capacity = max_entries;
    state->entries = (GameEntry *)calloc(state->capacity, sizeof(GameEntry));
    if (!state->entries) return false;

    DIR *d = opendir(scan_dir);
    if (!d) {
        free(state->entries);
        state->entries = NULL;
        return false;
    }

    int idx = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && idx < state->capacity) {
        size_t nlen = strlen(ent->d_name);
        if (nlen <= 4 || strcasecmp(ent->d_name + nlen - 4, ".CBE") != 0) continue;

        char name_utf8[260];
#ifdef CBE_HOST_UTF8_PATHS
        snprintf(name_utf8, sizeof(name_utf8), "%s", ent->d_name);
#else
        gbk_to_utf8((u8 *)ent->d_name, (u8 *)name_utf8, sizeof(name_utf8));
#endif

        snprintf(state->entries[idx].filepath, sizeof(state->entries[idx].filepath),
                 "%s/%s", scan_dir, name_utf8);

        struct stat st;
        if (stat(state->entries[idx].filepath, &st) == 0)
            state->entries[idx].file_size = (u32)st.st_size;

        extract_display_name(state->entries[idx].filepath,
                             state->entries[idx].display_name,
                             sizeof(state->entries[idx].display_name));

        {
            char name_gbk[260];
            utf8_to_gbk((u8 *)state->entries[idx].display_name,
                        (u8 *)name_gbk, sizeof(name_gbk));
            if (name_gbk[0] != 0)
                snprintf(state->entries[idx].display_name,
                         sizeof(state->entries[idx].display_name), "%s",
                         name_gbk);
        }

        idx++;
    }
    closedir(d);

    state->count = idx;
    vm_game_launcher_load_history(state);
    qsort(state->entries, state->count, sizeof(GameEntry), cmp_entries);

    state->scroll_offset = 0;
    return true;
}

static float vm_game_launcher_compute_max_scroll(const GameLauncherState *state)
{
    int visible_area = state->rows_visible * (state->tile_h + state->gap);
    float total = vm_game_launcher_compute_total_height(state);
    float max = total - visible_area;
    if (max < 0) max = 0;
    return max;
}

void vm_game_launcher_destroy(GameLauncherState *state)
{
    if (!state) return;
    free(state->entries);
    state->entries = NULL;
    state->count = 0;
    state->capacity = 0;
}

static int launcher_round_inset(int r, int d)
{
    int rr = r * r;
    int inset = 0;
    int dx;
    if (r <= 0)
        return 0;
    if (d < 0)
        d = 0;
    if (d >= r)
        return 0;
    for (dx = 0; dx < r; ++dx)
    {
        int ox = r - 1 - dx;
        if (ox * ox + d * d <= rr)
            break;
        inset = dx + 1;
    }
    return inset;
}

static void draw_rounded_rect(SDL_Surface *sfc, int x, int y, int w, int h,
                              u32 color, int radius)
{
    u32 mapped = SDL_MapRGB(sfc->format,
        ((color >> 16) & 0xff), ((color >> 8) & 0xff), (color & 0xff));
    int r = radius;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > sfc->w) w = sfc->w - x;
    if (y + h > sfc->h) h = sfc->h - y;
    if (w <= 0 || h <= 0) return;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int row = 0; row < h; ++row) {
        int ry = y + row;
        int x0 = 0;
        int x1 = w;
        u32 *dst;
        if (r > 0) {
            if (row < r)
                x0 = launcher_round_inset(r, r - 1 - row);
            else if (row >= h - r)
                x0 = launcher_round_inset(r, row - (h - r));
            x1 = w - x0;
        }
        if (x0 < 0) x0 = 0;
        if (x1 > w) x1 = w;
        dst = (u32 *)((u8 *)sfc->pixels + ry * sfc->pitch);
        for (int col = x0; col < x1; ++col)
            dst[x + col] = mapped;
    }
}

static void draw_rect_border(SDL_Surface *sfc, int x, int y, int w, int h, u32 color)
{
    if (w <= 0 || h <= 0) return;
    u32 mapped = SDL_MapRGB(sfc->format,
        ((color >> 16) & 0xff), ((color >> 8) & 0xff), (color & 0xff));
    for (int col = 0; col < w; ++col) {
        int px = x + col;
        if (px < 0 || px >= sfc->w) continue;
        if (y >= 0 && y < sfc->h)
            ((u32 *)((u8 *)sfc->pixels + y * sfc->pitch))[px] = mapped;
        if (y + h - 1 >= 0 && y + h - 1 < sfc->h)
            ((u32 *)((u8 *)sfc->pixels + (y + h - 1) * sfc->pitch))[px] = mapped;
    }
    for (int row = 0; row < h; ++row) {
        int py = y + row;
        u32 *dst;
        if (py < 0 || py >= sfc->h) continue;
        dst = (u32 *)((u8 *)sfc->pixels + py * sfc->pitch);
        if (x >= 0 && x < sfc->w) dst[x] = mapped;
        if (x + w - 1 >= 0 && x + w - 1 < sfc->w) dst[x + w - 1] = mapped;
    }
}

void vm_game_launcher_render(GameLauncherState *state, void *surface_ptr)
{
    SDL_Surface *front = (SDL_Surface *)surface_ptr;
    SDL_Surface local;
    SDL_Surface *sfc;
    static u32 backbuf[240 * 400];
    int use_back = 0;
    u32 textColor;
    u32 mutedColor;
    int content_x, content_y, content_w, content_h;
    float max_scroll;
    float total;
    float thumb_h;
    float scroll_ratio;
    float thumb_y;
    int sb_x;
    int footer_y;

    if (!front || !state || !state->entries) return;

    sfc = front;
#ifdef CBE_PLATFORM_ANDROID
    /* Compose offscreen, then copy once — Java samples finalLayerBuffer. */
    if (front->w == 240 && front->h == 400 &&
        (size_t)front->w * (size_t)front->h <= sizeof(backbuf) / sizeof(backbuf[0]))
    {
        local = *front;
        local.pixels = backbuf;
        sfc = &local;
        use_back = 1;
    }
#endif

    SDL_FillRect(sfc, NULL, SDL_MapRGB(sfc->format, (LC_BG >> 16) & 0xff,
                                       (LC_BG >> 8) & 0xff, LC_BG & 0xff));

    content_x = state->margin_x;
    content_y = state->header_h;
    content_w = state->viewport_w - 2 * state->margin_x - state->scrollbar_w;
    content_h = state->viewport_h - state->header_h - state->footer_h;
    textColor = SDL_MapRGB(sfc->format, (LC_TEXT >> 16) & 0xff,
                           (LC_TEXT >> 8) & 0xff, LC_TEXT & 0xff);
    mutedColor = SDL_MapRGB(sfc->format, (LC_MUTED >> 16) & 0xff,
                            (LC_MUTED >> 8) & 0xff, LC_MUTED & 0xff);

    draw_rounded_rect(sfc, 0, 0, state->viewport_w, state->header_h, LC_PANEL, 0);
    draw_rounded_rect(sfc, 0, state->header_h - 2, state->viewport_w, 2, LC_ACCENT, 0);
    {
        const char *title = "游戏中心";
        int tw = launcher_measure_utf8(title);
        int tx = (state->viewport_w - tw) / 2;
        launcher_draw_utf8(sfc, title, tx,
                           (state->header_h - getFontHeight()) / 2, textColor);
    }

    max_scroll = vm_game_launcher_compute_max_scroll(state);
    if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
    if (state->scroll_offset < 0) state->scroll_offset = 0;

    for (int i = 0; i < state->count; ++i) {
        int row = i / state->columns;
        int col = i % state->columns;
        float tile_y = (float)(row * (state->tile_h + state->gap)) - state->scroll_offset;
        int tile_x;
        int ty;
        u32 tile_bg;
        const char *name;
        int nameW;
        int maxW;
        int drawX;
        int drawY;
        int padL;

        if (tile_y < -state->tile_h || tile_y > content_h) continue;
        tile_x = content_x + col * (state->tile_w + state->gap);
        ty = content_y + (int)tile_y;
        if (ty + state->tile_h <= content_y || ty >= content_y + content_h)
            continue;

        tile_bg = (i == state->selected_index) ? LC_TILE_SEL : LC_TILE;
        draw_rounded_rect(sfc, tile_x, ty, state->tile_w, state->tile_h, tile_bg, 5);
        draw_rect_border(sfc, tile_x, ty, state->tile_w, state->tile_h,
                         SDL_MapRGB(sfc->format,
                                    ((i == state->selected_index ? LC_ACCENT : LC_BORDER) >> 16) & 0xff,
                                    ((i == state->selected_index ? LC_ACCENT : LC_BORDER) >> 8) & 0xff,
                                    (i == state->selected_index ? LC_ACCENT : LC_BORDER) & 0xff));
        if (i == state->selected_index) {
            draw_rounded_rect(sfc, tile_x + 3, ty + 8, 3, state->tile_h - 16, LC_JADE, 0);
        }

        name = state->entries[i].display_name;
        padL = (i == state->selected_index) ? 14 : 12;
        maxW = state->tile_w - padL - 10;
        nameW = launcher_measure_string_width(name);
        drawX = tile_x + padL;
        drawY = ty + (state->tile_h - getFontHeight()) / 2;
        if (nameW > maxW) {
            const char *p = name;
            int acc = 0;
            char trunc[128];
            size_t len;
            while (*p && acc + getFontCellWidth() <= maxW) {
                u8 ch = (u8)*p;
                if (ch < 0x80) { acc += getFontCellWidth(); p++; }
                else { acc += getFontWidth(); p += 2; }
            }
            len = (size_t)(p - name);
            if (len >= sizeof(trunc)) len = sizeof(trunc) - 1;
            memcpy(trunc, name, len);
            trunc[len] = '\0';
            launcher_draw_font_string(sfc, trunc, drawX, drawY, textColor);
        } else {
            launcher_draw_font_string(sfc, name, drawX, drawY, textColor);
        }
    }

    total = vm_game_launcher_compute_total_height(state);
    thumb_h = (total > 0) ? (content_h * content_h / total) : (float)content_h;
    if (thumb_h < 18) thumb_h = 18;
    if (thumb_h > content_h) thumb_h = (float)content_h;
    scroll_ratio = (total > (float)content_h) ?
        state->scroll_offset / (total - (float)content_h) : 0;
    thumb_y = content_y + scroll_ratio * (content_h - thumb_h);
    sb_x = state->viewport_w - state->scrollbar_w;
    draw_rounded_rect(sfc, sb_x, content_y, state->scrollbar_w, content_h, LC_BORDER, 0);
    draw_rounded_rect(sfc, sb_x, (int)thumb_y, state->scrollbar_w, (int)thumb_h, LC_ACCENT, 0);

    footer_y = state->viewport_h - state->footer_h;
    draw_rounded_rect(sfc, 0, footer_y, state->viewport_w, state->footer_h, LC_PANEL, 0);
    draw_rounded_rect(sfc, 0, footer_y, state->viewport_w, 1, LC_BORDER, 0);
    {
#ifdef CBE_PLATFORM_ANDROID
        const char *footer = "轻触选择游戏";
#else
        const char *footer = "双击进入";
#endif
        int fw = launcher_measure_utf8(footer);
        int fx = (state->viewport_w - fw) / 2;
        int fy = footer_y + (state->footer_h - getFontHeight()) / 2;
        launcher_draw_utf8(sfc, footer, fx, fy, mutedColor);
    }

    if (state->loading) {
        launcher_dim_surface(sfc);
        draw_rounded_rect(sfc,
            (state->viewport_w - 168) / 2,
            (state->viewport_h - 64) / 2,
            168, 64, LC_PANEL, 6);
        draw_rect_border(sfc,
            (state->viewport_w - 168) / 2,
            (state->viewport_h - 64) / 2,
            168, 64, SDL_MapRGB(sfc->format, (LC_ACCENT >> 16) & 0xff,
                                (LC_ACCENT >> 8) & 0xff, LC_ACCENT & 0xff));
        {
            const char *label = "加载中...";
            int lw = launcher_measure_utf8(label);
            launcher_draw_utf8(sfc, label,
                (state->viewport_w - lw) / 2,
                (state->viewport_h - getFontHeight()) / 2 - 6, textColor);
        }
        if (state->loading_name[0]) {
            int nw = launcher_measure_string_width(state->loading_name);
            launcher_draw_font_string(sfc, state->loading_name,
                (state->viewport_w - nw) / 2,
                (state->viewport_h - getFontHeight()) / 2 + getFontHeight(),
                SDL_MapRGB(sfc->format, (LC_ACCENT >> 16) & 0xff,
                           (LC_ACCENT >> 8) & 0xff, LC_ACCENT & 0xff));
        }
    }

    if (state->server_select_active)
        launcher_render_server_select(state, sfc);

#ifdef CBE_PLATFORM_ANDROID
    if (use_back)
        memcpy(front->pixels, backbuf, (size_t)front->w * (size_t)front->h * sizeof(u32));
#else
    (void)use_back;
    (void)backbuf;
#endif
}

const char *vm_game_launcher_handle_mouse(GameLauncherState *state, int x, int y, int button)
{
    const char *path;

    if (!state || !state->entries) return NULL;

    if (state->server_select_active)
    {
        path = launcher_handle_server_select_mouse(state, x, y, button);
        launcher_mark_dirty(state);
        return path;
    }

    int content_x = state->margin_x;
    int content_y = state->header_h;
    int content_w = state->viewport_w - 2 * state->margin_x - state->scrollbar_w;
    int content_h = state->viewport_h - state->header_h - state->footer_h;
    int sb_x = state->viewport_w - state->scrollbar_w;

    if (button == 0 && state->_internal_is_scrolling) {
        state->_internal_is_scrolling = false;
        launcher_mark_dirty(state);
        return NULL;
    }

    if (x >= sb_x && y >= content_y && y < content_y + content_h) {
        float total = vm_game_launcher_compute_total_height(state);
        float thumb_ratio = (total > 0) ? (float)content_h / total : 1.0f;
        float thumb_h = content_h * thumb_ratio;
        if (thumb_h < 20) thumb_h = 20;
        float scroll_ratio = (total > (float)content_h) ?
            state->scroll_offset / (total - (float)content_h) : 0;
        float track_start = content_y + scroll_ratio * (content_h - thumb_h);

        if (button == 1) {
            if (y >= track_start && y <= track_start + thumb_h) {
                state->_internal_is_scrolling = true;
            } else {
                float new_offset = ((float)(y - content_y) / content_h) * (total - (float)content_h);
                if (new_offset < 0) new_offset = 0;
                if (new_offset > total - (float)content_h) new_offset = total - (float)content_h;
                state->scroll_offset = new_offset;
            }
            launcher_mark_dirty(state);
        }
        return NULL;
    }

    if (state->_internal_is_scrolling && button == 1) {
        float total = vm_game_launcher_compute_total_height(state);
        float new_offset = ((float)(y - content_y - state->tile_h / 2) / content_h) * (total - (float)content_h);
        if (new_offset < 0) new_offset = 0;
        if (new_offset > total - (float)content_h) new_offset = total - (float)content_h;
        state->scroll_offset = new_offset;
        launcher_mark_dirty(state);
        return NULL;
    }

    if (x >= content_x && x < content_x + content_w &&
        y >= content_y && y < content_y + content_h) {
        for (int i = 0; i < state->count; ++i) {
            int row = i / state->columns;
            int col = i % state->columns;
            float tile_y = (float)(row * (state->tile_h + state->gap)) - state->scroll_offset;
            if (tile_y < -state->tile_h || tile_y > content_h) continue;

            int tile_x = content_x + col * (state->tile_w + state->gap);
            int ty = content_y + (int)tile_y;

            if (x >= tile_x && x < tile_x + state->tile_w &&
                y >= ty && y < ty + state->tile_h) {
                state->selected_index = i;
                launcher_mark_dirty(state);
                if (button == 1) {
#ifdef CBE_PLATFORM_ANDROID
                    /* Touch: single tap launches (or opens zone picker). */
                    return launcher_try_launch_path(state,
                                                    state->entries[i].filepath);
#else
                    u32 now = SDL_GetTicks();
                    if (now - state->last_click_time <= 300 && i == state->last_click_index) {
                        return launcher_try_launch_path(state,
                                                        state->entries[i].filepath);
                    }
                    state->last_click_time = now;
                    state->last_click_index = i;
#endif
                }
                return NULL;
            }
        }
    }

    if (button == 4) {
        state->scroll_offset -= 30;
        if (state->scroll_offset < 0) state->scroll_offset = 0;
        launcher_mark_dirty(state);
        return NULL;
    }
    if (button == 5) {
        float max_scroll = vm_game_launcher_compute_max_scroll(state);
        state->scroll_offset += 30;
        if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
        launcher_mark_dirty(state);
        return NULL;
    }

    return NULL;
}

const char *vm_game_launcher_handle_key(GameLauncherState *state, int key_sym, bool is_down)
{
    if (!state || !state->entries) return NULL;
    if (!is_down) return NULL;

    if (state->server_select_active)
        return launcher_handle_server_select_key(state, key_sym);

    float max_scroll = vm_game_launcher_compute_max_scroll(state);
    int step = state->tile_h + state->gap;

    switch (key_sym) {
    case SDLK_UP:
    case SDLK_w:
        if (state->selected_index > 0) state->selected_index--;
        break;
    case SDLK_DOWN:
    case SDLK_s:
        if (state->selected_index < state->count - 1) state->selected_index++;
        break;
    case SDLK_PAGEUP:
        state->selected_index -= state->rows_visible;
        if (state->selected_index < 0) state->selected_index = 0;
        break;
    case SDLK_PAGEDOWN:
        state->selected_index += state->rows_visible;
        if (state->selected_index >= state->count) state->selected_index = state->count - 1;
        break;
    case SDLK_HOME:
        state->selected_index = 0;
        break;
    case SDLK_END:
        state->selected_index = state->count - 1;
        break;
    case SDLK_q:
    case SDLK_f:
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    {
        const char *path = vm_game_launcher_get_selected_filepath(state);
        return launcher_try_launch_path(state, path);
    }
    case SDLK_ESCAPE:
        return NULL;
    default:
        return NULL;
    }

    {
        int row = state->selected_index / state->columns;
        float tile_abs_y = (float)(row * step) - state->scroll_offset;

        if (tile_abs_y < 0)
            state->scroll_offset = (float)(row * step);
        else if (tile_abs_y > state->viewport_h - state->header_h - state->footer_h - state->tile_h)
            state->scroll_offset = (float)(row * step) -
                (state->viewport_h - state->header_h - state->footer_h - state->tile_h);

        if (state->scroll_offset < 0) state->scroll_offset = 0;
        if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
    }
    launcher_mark_dirty(state);
    return NULL;
}

int vm_game_launcher_get_selected_index(const GameLauncherState *state)
{
    return state ? state->selected_index : -1;
}

const char *vm_game_launcher_get_selected_filepath(const GameLauncherState *state)
{
    if (!state || !state->entries || state->selected_index < 0 ||
        state->selected_index >= state->count) return NULL;
    return state->entries[state->selected_index].filepath;
}

const char *vm_game_launcher_get_selected_server_name(const GameLauncherState *state)
{
    if (state == NULL || state->selected_server_name[0] == 0)
        return NULL;
    return state->selected_server_name;
}

bool vm_game_launcher_is_active(const GameLauncherState *state)
{
    return state && state->active;
}

void vm_game_launcher_set_active(GameLauncherState *state, bool active)
{
    if (state) state->active = active;
}

static const char *HISTORY_FILE = "game_launcher_history.txt";

void vm_game_launcher_record_launch(const char *filepath)
{
    if (!filepath) return;
    int counts[256];
    char paths[256][260];
    int n = 0;
    FILE *f = fopen(HISTORY_FILE, "r");
    if (f) {
        while (n < 256 && fscanf(f, "%d %[^\n]", &counts[n], paths[n]) == 2) {
            n++;
        }
        fclose(f);
    }
    int found = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(paths[i], filepath) == 0) { found = i; break; }
    }
    if (found >= 0) {
        counts[found]++;
    } else if (n < 256) {
        counts[n] = 1;
        snprintf(paths[n], sizeof(paths[n]), "%s", filepath);
        n++;
    }
    f = fopen(HISTORY_FILE, "w");
    if (f) {
        for (int i = 0; i < n; i++) {
            fprintf(f, "%d %s\n", counts[i], paths[i]);
        }
        fclose(f);
    }
}

void vm_game_launcher_load_history(GameLauncherState *state)
{
    if (!state || !state->entries) return;
    int counts[256];
    char paths[256][260];
    int n = 0;
    FILE *f = fopen(HISTORY_FILE, "r");
    if (f) {
        while (n < 256 && fscanf(f, "%d %[^\n]", &counts[n], paths[n]) == 2) {
            n++;
        }
        fclose(f);
    }
    for (int i = 0; i < state->count; i++) {
        state->entries[i].launch_count = 0;
        for (int j = 0; j < n; j++) {
            if (strcmp(state->entries[i].filepath, paths[j]) == 0) {
                state->entries[i].launch_count = counts[j];
                break;
            }
        }
    }
}
