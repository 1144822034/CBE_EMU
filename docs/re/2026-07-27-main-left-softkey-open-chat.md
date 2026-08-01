# 主界面右软键返回打开聊天（2026-07-27）

## 目标

在江湖 OL CBE 主地图 HUD 上，屏幕栈已无法再返回时，按右软「返回」（Android「取消」/skey 13 / 桌面 `e`）快捷打开聊天界面。

## 证据

| 项 | 证据 |
|---|---|
| 右软映射 | Android `cancel`→key 21→`'e'`→skey 13；左软 skey 12 |
| 主菜单项 | `江湖OL.cbe` `@0x0101AD3A`：系统设置 / 个性功能 / **社交聊天** / … |
| 打开路径 | 先合成左软打开「菜单」，再 `下`×2 → `OK` 选「社交聊天」→ `StartSceneTransition(2,1,1)` |
| HUD 软键 | 字符串池「菜单」「返回」；典型主界面左=菜单、右=返回 |
| 不能再返回 | `g_screenStackCount == 1`，非 title `vm_screen_is_entry_root`，右软为空或「返回」 |

不直接调用 CBE `StartSceneTransition`，只合成键盘事件。

## 实现

- `vm_ctrl_draw_softkey_bar` 缓存软键文案
- 右软按下且闸门成立 → 松开后排队：`左软` → `下`×2 → `OK`
- 子界面（栈深 > 1）右软仍走原生返回

修改：`src/main.c`、Android `cbeEmu/main.c`

## 验证

1. 主地图按右软「返回」→ 应进入社交聊天
2. 日志：`right/返回 press ... gate=1` 与 `right-soft/返回 -> chat via menu idx=2`
3. 子界面右软仍返回上一层

## unresolved

- 主 HUD 右软文案若不是「返回」且非空，闸门不触发（看日志 L/R）
- 菜单默认高亮与 tick 延迟可能需实机微调
