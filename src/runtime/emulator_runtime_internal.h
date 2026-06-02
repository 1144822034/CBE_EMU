#pragma once

#include "../main.h"
#include "../vmEvent.h"

void vm_net_trace(const char *fmt, ...);
uc_err vm_call4_preserve_regs(u32 entry, u32 r0, u32 r1, u32 r2, u32 r3);
void hook_vm_manager_code_callback(uc_engine *uc, uint64_t address, uint32_t size, void *user_data);
bool vm_manager_lcd_handle(u32 address);
bool vm_manager_fileio_handle(u32 address);
bool vm_manager_stdio_handle(u32 address);
bool vm_manager_timer_handle(u32 address);
bool vm_manager_ctrl_handle(u32 address);
bool vm_manager_network_handle(u32 address);
bool vm_manager_screen_handle(u32 address);
bool vm_manager_game_util_handle(u32 address);
bool vm_manager_df_engine_handle(u32 address);
bool vm_manager_df_script_handle(u32 address);
bool vm_manager_gameold_handle(u32 address);
bool vm_manager_game_lcd_handle(u32 address);
bool vm_manager_audio_handle(u32 address);
bool vm_manager_netapp_handle(u32 address);
bool vm_manager_sensor_handle(u32 address);
bool vm_manager_vmim_handle(u32 address);
bool vm_appstore_handle(u32 address);
bool vm_dl_load_handle(u32 address);
bool vm_dl_pay_handle(u32 address);
bool vm_dl_rs_handle(u32 address);
bool vm_dl_image_handle(u32 address);
bool vm_video_handle(u32 address);
void vm_trace_lcd_shape(const char *apiName, int x, int y, int w, int h, u32 color);
void vm_trace_lcd_text(const char *apiName, u32 idx, u32 strPtr, int x, int y, u16 color, const u8 *gbkText);
void vm_trace_lcd_text_call(const char *apiName, u32 idx, u32 strPtr, u32 r0, u32 r1, u32 r2, u32 r3, u32 sp, int x, int y, u16 color, const u8 *gbkText);

extern u32 g_currentFontType;
extern u32 g_screenStackCount;
extern u32 g_screenRemovedWithoutNext;
extern u32 g_screenResumeExisting;
extern u32 g_activeScreenRemovedThisFrame;
extern u32 g_currentScreenThis;
extern u8 g_lastStartupScreenState;
extern u32 g_schedulerTick;
extern u32 g_nextNetConnectId;
extern u32 g_netMockResponseOffset;
extern u32 g_netUpLinkData;
extern u32 g_netDownLinkData;
extern u32 g_netCurrentObject;

int vm_lcd_coord_from_reg(u32 value);
void vm_lcd_draw_line(int x0, int y0, int x1, int y1, u16 color);
int vm_lcd_try_unpack_packed_rect(u32 p0, u32 p1, int *x, int *y, int *w, int *h);
void vm_lcd_sync_cache_rect_to_vm(int x, int y, int w, int h);
int vm_lcd_font_width_for_mode(u32 mode);
int vm_lcd_measure_current_string(const u8 *gbkText);
void vm_lcd_draw_current_string(u8 *gbkText, int x, int y, u16 color);
void vm_lcd_sync_string_to_vm(const u8 *gbkText, int x, int y);

u32 vm_input_wcslen_limit(u32 addr, u32 maxLen);
void vm_input_open(u32 callback, u32 param, int password);
void vm_input_reset(void);
u32 vm_input_is_open(void);
uc_err scheduler_dispatch_input_event(vm_event *evt);

void vm_storage_trace(const char *fmt, ...);
u32 vm_nv_read(u32 reqPtr);
u32 vm_nv_write(u32 reqPtr);
u32 vm_sys_set_setting_profile(u32 profile);
u32 vm_sys_get_setting_profile(void);
u32 vm_sys_get_setting_profile_name(u32 profile, u32 dst, u32 dstLen);

void scheduler_register_net_channel(u32 connectId, u32 callback, u32 context);
void scheduler_unregister_net_channel(u32 connectId);
void scheduler_queue_net_task(u32 r0, u32 r1, u32 callback, u32 context);
void vm_net_mock_on_send(u32 connectId, u32 dataPtr, u32 dataLen);
u32 vm_net_mock_env_u32(const char *name, u32 fallback);
void vm_screen_stack_push(u32 screen, u32 moduleBase);
bool vm_screen_stack_remove(u32 screen, u32 *newTop, u32 *newTopModuleBase);
u32 scheduler_get_tick_ms(void);
u32 scheduler_start_timer(u32 delayMs, u32 callback, u32 context);
u32 scheduler_stop_timer(u32 handle);
u32 vm_df_get_resource_by_id(u32 id);
u32 vm_df_get_resource_by_file_name(u32 namePtr);
u32 vm_df_get_resource_name_by_id(u32 id);
u32 vm_df_get_resource_id_by_file_name(u32 namePtr);
u32 vm_df_get_t_resource(u32 namePtr, int stream);
