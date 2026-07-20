#include "gameLauncher.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../Lib/sdl2-2.0.10/include/SDL2/SDL.h"
#include "fontEngine.h"
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

/* Layout constants */
#define LAUNCHER_TILE_W 100
#define LAUNCHER_TILE_H 36
#define LAUNCHER_GAP 6
#define LAUNCHER_MARGIN_X 8
#define LAUNCHER_HEADER_H 30
#define LAUNCHER_FOOTER_H 26
#define LAUNCHER_SCROLLBAR_W 6

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

bool vm_game_launcher_init(GameLauncherState *state, int viewport_w, int viewport_h)
{
    if (!state || viewport_w <= 0 || viewport_h <= 0) return false;

    memset(state, 0, sizeof(*state));
    state->viewport_w = viewport_w;
    state->viewport_h = viewport_h;
    state->tile_w = LAUNCHER_TILE_W;
    state->tile_h = LAUNCHER_TILE_H;
    state->gap = LAUNCHER_GAP;
    state->margin_x = LAUNCHER_MARGIN_X;
    state->header_h = LAUNCHER_HEADER_H;
    state->footer_h = LAUNCHER_FOOTER_H;
    state->scrollbar_w = LAUNCHER_SCROLLBAR_W;

    int available_w = viewport_w - 2 * state->margin_x - state->scrollbar_w;
    if (available_w < state->tile_w) available_w = state->tile_w;
    state->columns = available_w / (state->tile_w + state->gap);
    if (state->columns < 1) state->columns = 1;

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
        gbk_to_utf8((u8 *)ent->d_name, (u8 *)name_utf8, sizeof(name_utf8));

        snprintf(state->entries[idx].filepath, sizeof(state->entries[idx].filepath),
                 "%s/%s", scan_dir, name_utf8);

        struct stat st;
        if (stat(state->entries[idx].filepath, &st) == 0)
            state->entries[idx].file_size = (u32)st.st_size;

        extract_display_name(state->entries[idx].filepath,
                             state->entries[idx].display_name,
                             sizeof(state->entries[idx].display_name));

        char name_gbk[260];
        utf8_to_gbk((u8 *)state->entries[idx].display_name,
                     (u8 *)name_gbk, sizeof(name_gbk));
        snprintf(state->entries[idx].display_name,
                 sizeof(state->entries[idx].display_name), "%s", name_gbk);

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

static void draw_rounded_rect(SDL_Surface *sfc, int x, int y, int w, int h,
                              u32 color, int radius)
{
    (void)radius;
    u32 mapped = SDL_MapRGB(sfc->format,
        ((color >> 16) & 0xff), ((color >> 8) & 0xff), (color & 0xff));
    if (x + w > sfc->w) w = sfc->w - x;
    if (y + h > sfc->h) h = sfc->h - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; ++row) {
        int ry = y + row;
        if (ry < 0 || ry >= sfc->h) continue;
        u32 *dst = (u32 *)((u8 *)sfc->pixels + ry * sfc->pitch);
        for (int col = 0; col < w; ++col)
            dst[x + col] = mapped;
    }
}

static void draw_rect_border(SDL_Surface *sfc, int x, int y, int w, int h, u32 color)
{
    if (x < 0 || y < 0 || x + w > sfc->w || y + h > sfc->h) return;
    if (w <= 0 || h <= 0) return;
    u32 mapped = SDL_MapRGB(sfc->format,
        ((color >> 16) & 0xff), ((color >> 8) & 0xff), (color & 0xff));
    for (int col = 0; col < w; ++col) {
        ((u32 *)((u8 *)sfc->pixels + y * sfc->pitch))[x + col] = mapped;
        ((u32 *)((u8 *)sfc->pixels + (y + h - 1) * sfc->pitch))[x + col] = mapped;
    }
    for (int row = 0; row < h; ++row) {
        u32 *dst = (u32 *)((u8 *)sfc->pixels + (y + row) * sfc->pitch);
        dst[x] = mapped;
        dst[x + w - 1] = mapped;
    }
}

void vm_game_launcher_render(GameLauncherState *state, void *surface_ptr)
{
    SDL_Surface *sfc = (SDL_Surface *)surface_ptr;
    if (!sfc || !state || !state->entries) return;

    u32 bg = SDL_MapRGB(sfc->format, 0x12, 0x12, 0x20);
    SDL_FillRect(sfc, NULL, bg);

    int content_x = state->margin_x;
    int content_y = state->header_h;
    int content_w = state->viewport_w - 2 * state->margin_x - state->scrollbar_w;
    int content_h = state->viewport_h - state->header_h - state->footer_h;

    SDL_Rect clip;
    clip.x = content_x;
    clip.y = content_y;
    clip.w = content_w;
    clip.h = content_h;
    SDL_SetClipRect(sfc, &clip);

    draw_rounded_rect(sfc, 0, 0, state->viewport_w, state->header_h, 0x1a1a3e, 0);
    draw_rounded_rect(sfc, 0, state->header_h - 1, state->viewport_w, 1,
        SDL_MapRGB(sfc->format, 0xe9, 0x45, 0x60), 0);
    {
        const char *title = "Game Center";
        int tw = launcher_measure_string_width(title);
        int tx = (state->viewport_w - tw) / 2;
        launcher_draw_font_string(sfc, title, tx, (state->header_h - getFontHeight()) / 2,
            SDL_MapRGB(sfc->format, 0xff, 0xff, 0xff));
    }

    float max_scroll = vm_game_launcher_compute_max_scroll(state);
    if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
    if (state->scroll_offset < 0) state->scroll_offset = 0;

    for (int i = 0; i < state->count; ++i) {
        int row = i / state->columns;
        int col = i % state->columns;

        float tile_y = (float)(row * (state->tile_h + state->gap)) - state->scroll_offset;
        if (tile_y < -state->tile_h || tile_y > content_h) continue;

        int tile_x = content_x + col * (state->tile_w + state->gap);
        int ty = content_y + (int)tile_y;

        u32 tile_bg = SDL_MapRGB(sfc->format, 0x1e, 0x1e, 0x36);
        u32 border = SDL_MapRGB(sfc->format, 0x2a, 0x2a, 0x44);

        if (i == state->selected_index) {
            tile_bg = SDL_MapRGB(sfc->format, 0x1a, 0x30, 0x55);
            border = SDL_MapRGB(sfc->format, 0xe9, 0x45, 0x60);
        }

        draw_rounded_rect(sfc, tile_x, ty, state->tile_w, state->tile_h, tile_bg, 0);
        draw_rect_border(sfc, tile_x, ty, state->tile_w, state->tile_h, border);
        if (i == state->selected_index) {
            draw_rounded_rect(sfc, tile_x, ty + 4, 2, state->tile_h - 8,
                SDL_MapRGB(sfc->format, 0xe9, 0x45, 0x60), 0);
        }
        {
            const char *name = state->entries[i].display_name;
            int nameW = launcher_measure_string_width(name);
            int maxW = state->tile_w - 8;
            int drawX = tile_x + (state->tile_w - (nameW > maxW ? maxW : nameW)) / 2;
            int drawY = ty + (state->tile_h - getFontHeight()) / 2;
            u32 textColor = SDL_MapRGB(sfc->format, 0xff, 0xff, 0xff);
            if (nameW > maxW) {
                const char *p = name;
                int acc = 0;
                while (*p && acc + getFontCellWidth() <= maxW) {
                    u8 ch = (u8)*p;
                    if (ch < 0x80) { acc += getFontCellWidth(); p++; }
                    else { acc += getFontWidth(); p += 2; }
                }
                char trunc[128];
                size_t len = p - name;
                if (len >= sizeof(trunc)) len = sizeof(trunc) - 1;
                memcpy(trunc, name, len);
                trunc[len] = '\0';
                launcher_draw_font_string(sfc, trunc, drawX, drawY, textColor);
            } else {
                launcher_draw_font_string(sfc, name, drawX, drawY, textColor);
            }
        }
    }

    float total = vm_game_launcher_compute_total_height(state);
    float thumb_ratio = (total > 0) ? (float)content_h / total : 1.0f;
    float thumb_h = content_h * thumb_ratio;
    if (thumb_h < 20) thumb_h = 20;
    float scrollbar_track_h = content_h;
    float scroll_ratio = (total > (float)content_h) ?
        state->scroll_offset / (total - (float)content_h) : 0;
    float thumb_y = content_y + scroll_ratio * (scrollbar_track_h - thumb_h);

    u32 sb_color = SDL_MapRGB(sfc->format, 0x2a, 0x2a, 0x40);
    u32 thumb_color = SDL_MapRGB(sfc->format, 0xe9, 0x45, 0x60);
    int sb_x = state->viewport_w - state->scrollbar_w;
    draw_rounded_rect(sfc, sb_x, content_y, state->scrollbar_w, content_h, sb_color, 0);
    draw_rounded_rect(sfc, sb_x, (int)thumb_y, state->scrollbar_w, (int)thumb_h, thumb_color, 0);

    SDL_SetClipRect(sfc, NULL);

    int footer_y = state->viewport_h - state->footer_h;
    draw_rounded_rect(sfc, 0, footer_y, state->viewport_w, state->footer_h, 0x1a1a3e, 0);
    draw_rounded_rect(sfc, 0, footer_y, state->viewport_w, 1,
        SDL_MapRGB(sfc->format, 0x2a, 0x2a, 0x44), 0);
    {
        const char *footer = "Up/Down Select  Enter Launch";
        int fw = launcher_measure_string_width(footer);
        int fx = (state->viewport_w - fw) / 2;
        int fy = footer_y + (state->footer_h - getFontHeight()) / 2;
        launcher_draw_font_string(sfc, footer, fx, fy,
            SDL_MapRGB(sfc->format, 0x66, 0x66, 0x88));
    }

    if (state->loading) {
        SDL_Rect full = {0, 0, state->viewport_w, state->viewport_h};
        SDL_FillRect(sfc, &full, SDL_MapRGB(sfc->format, 0x10, 0x10, 0x20));
        draw_rounded_rect(sfc,
            (state->viewport_w - 160) / 2,
            (state->viewport_h - 60) / 2,
            160, 60, 0x1a1a3e, 1);
        draw_rect_border(sfc,
            (state->viewport_w - 160) / 2,
            (state->viewport_h - 60) / 2,
            160, 60, SDL_MapRGB(sfc->format, 0xe9, 0x45, 0x60));
        const char *label = "Loading...";
        int lw = launcher_measure_string_width(label);
        launcher_draw_font_string(sfc, label,
            (state->viewport_w - lw) / 2,
            (state->viewport_h - getFontHeight()) / 2 - 4,
            SDL_MapRGB(sfc->format, 0xff, 0xff, 0xff));
        const char *name = state->loading_name;
        if (name[0]) {
            int nw = launcher_measure_string_width(name);
            launcher_draw_font_string(sfc, name,
                (state->viewport_w - nw) / 2,
                (state->viewport_h - getFontHeight()) / 2 + getFontHeight() + 2,
                SDL_MapRGB(sfc->format, 0xe9, 0x45, 0x60));
        }
    }

    SDL_SetClipRect(sfc, NULL);
}

bool vm_game_launcher_handle_mouse(GameLauncherState *state, int x, int y, int button)
{
    if (!state || !state->entries) return false;

    int content_x = state->margin_x;
    int content_y = state->header_h;
    int content_w = state->viewport_w - 2 * state->margin_x - state->scrollbar_w;
    int content_h = state->viewport_h - state->header_h - state->footer_h;
    int sb_x = state->viewport_w - state->scrollbar_w;

    if (button == 0 && state->_internal_is_scrolling) {
        state->_internal_is_scrolling = false;
        return true;
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
        }
        return true;
    }

    if (state->_internal_is_scrolling && button == 1) {
        float total = vm_game_launcher_compute_total_height(state);
        float new_offset = ((float)(y - content_y - state->tile_h / 2) / content_h) * (total - (float)content_h);
        if (new_offset < 0) new_offset = 0;
        if (new_offset > total - (float)content_h) new_offset = total - (float)content_h;
        state->scroll_offset = new_offset;
        return true;
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
                if (button == 1) return true;
                return true;
            }
        }
    }

    if (button == 4) {
        state->scroll_offset -= 30;
        if (state->scroll_offset < 0) state->scroll_offset = 0;
        return true;
    }
    if (button == 5) {
        float max_scroll = vm_game_launcher_compute_max_scroll(state);
        state->scroll_offset += 30;
        if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;
        return true;
    }

    return false;
}

bool vm_game_launcher_handle_key(GameLauncherState *state, int key_sym, bool is_down)
{
    if (!state || !state->entries) return false;
    if (!is_down) return false;

    float max_scroll = vm_game_launcher_compute_max_scroll(state);
    int step = state->tile_h + state->gap;

    switch (key_sym) {
    case SDLK_UP:
        if (state->selected_index > 0) state->selected_index--;
        break;
    case SDLK_DOWN:
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
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return true;
    case SDLK_ESCAPE:
        return true;
    default:
        return false;
    }

    int row = state->selected_index / state->columns;
    int col = state->selected_index % state->columns;
    float tile_abs_y = (float)(row * step) - state->scroll_offset;

    if (tile_abs_y < 0)
        state->scroll_offset = (float)(row * step);
    else if (tile_abs_y > state->viewport_h - state->header_h - state->footer_h - state->tile_h)
        state->scroll_offset = (float)(row * step) -
            (state->viewport_h - state->header_h - state->footer_h - state->tile_h);

    if (state->scroll_offset < 0) state->scroll_offset = 0;
    if (state->scroll_offset > max_scroll) state->scroll_offset = max_scroll;

    return true;
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
