# CBE NPC 对话确定/离开后方向键卡住

Date: 2026-10-10

## 现象

与 NPC 对话后选「确定」或「离开」，地图方向键无响应；再按一次「返回」才能继续操作。

## 日志（`bin/client_out.txt`）

```text
cbe_uplink … 1/26/1          → resp=93   # 打开对话
cbe_uplink … 1/26/1          → resp=11   # 离开/确定 value=0 → 仅 26/0
# 无 screen_mgr remove
cbe_uplink … 1/2/1           # 仍可能发移动，但对话态未退
```

对比：选离开后没有拆对话屏；`26/0` 只清 `r9+21808` busy，不负责关页。

## 根因

`task_hall_activate_selected_entry(0x010492B0)`：

| option.action | 行为 |
|---------------|------|
| `0` | `ResetDownloadState`，**本地关对话页**，不上行 |
| `1` | 发 `26/1 {type=2,id=value}` |
| `4` | 任务详情 `6/10` |

服务端空菜单注入的「离开」和若干 tip「确定」写成了 **`action=1 value=0`**。  
客户端因此上行 `26/1`；服务端回 **`26/0` busy-ack**（`evidence=cbe-arena-rank-close` / serviceValue==0）。  
对话 UI 模式未退出 → 方向键被对话壳吃掉；右软「返回」才本地关页。

证据：`docs/re/2026-07-18-task-test-npc.md`（action=0 只关页）；Pixi 契约「离开勿发 26/1」（`2026-08-06-pixijs-live-core-loop.md`）。

先前宿主 `g_curKeyState` 清粘键是旁路；本日志无 `clear_all_key_state`，且离开后仍见 `2/1`，不足以解释「必须返回」。

## 修改

`src/server/mock_server_scene_sync.c`：

1. 根对话 service 选项：`value==0` → **`action=0`**（离开/关闭）。
2. NPC 服务 tip 选项：`value==0` → **`action=0`**（确定关闭）；挑战项仍 `action=13`。

## 验证

1. 重编并重启 **`jh-online-server`**（只需服务端）。
2. 与仅「离开」的 NPC 对话 → 选离开 → **不应再出现**第二条 `26/1`；方向键立即走路。
3. 服务 tip「确定」关闭后同样可走，无需先按返回。
4. 商店/技能等 `action=1` 非零 value 路径不变。
