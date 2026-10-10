# CBE 登入慢 + NPC/聊天不显示 + 921 列表空名

Date: 2026-10-10

## 现象

1. 角色登入进图加载很慢（约数秒级卡顿）。
2. 进图后 NPC 不显示。
3. 聊天信息不显示。
4. 背包 921（修炼天书）列表行无道具名（详情 `7/38` 仍可）。

## 运行证据（`bin/client_out.txt`）

```text
cbe_30_overflow_arm primary_rows=43 overflow_rows=95 objs=4
cbe_30_overflow_deliver chunk=0..3   # 同一次 drain 全量投递
queue_data … resp=36 … deliver_ms=7929
# 全程无 queue_scene_poll
cbe_uplink … 1/12/1+7/42… → 17/1（开背包）
# 无 cbe_cultivation_book_list_name 出现在 17/1 之后
```

## 根因

### 登入慢 + NPC/聊天

触发：登录 `30/21` 超 `GRID30_DELIVER_CAP`（2048）→ 拆成 primary + N 个 overflow 块。  
被违反契约：`scheduler_dispatch_net_tasks` **同 tick 处理全部 pending event=7**；旧逻辑在 drain 里一次性 `flush` 全部 overflow → 多段 HandleItemGrid 叠成 `deliver_ms≈8s`。  
首个错误状态：该窗口内 `scheduler_find_pending_net_event(7)` 一直非空 → `vm_net_mock_poll_push_if_due` 不投 scene-poll → 无 `27/11` / `1/3/3` 滴灌。  
证据：上表；对照 `docs/re/2026-10-08-cbe-backpack-255-init-patch.md`（HandleItemGrid 成本）。

### 921 空名

背包行名在 `item+4`（16B GBK）。920/921 的 `item.dsh` **说明为空**，`17/1`→mmGame `0x418C` 填 UI 后常把 `item+4` 留空。  
全局 `UC_HOOK_CODE` **排除整个 VM pool**（防 Android free-storm），故原计划挂在 `0x05184498` / `0x05184538` 的强制写名**从未触发**；仅登录 ProcessItem 路径写了 manager 行，开背包 UI 重建后仍空。

## 修改

| 位置 | 行为 |
| --- | --- |
| `network-client.c` overflow | arm 时 `scene_poll_urgent`；**不再**在 drain 全量 flush |
| `network-client.c` flush/pump | 每次只投 **1** 个 overflow；idle（无 pending event=7 / poll job）才 pump |
| `main.c` host tick | dispatch 后再 `poll_push` → `overflow_pump` → book-name pump |
| `main.c` / `main.h` | `17/1` 下行后 `request`+`pump` 强制 920/921 `item+4` |
| `main.c` hooks | VM pool 内按签名动态窄 hook `base+0x4498` / `base+0x4538`（兼兼容 `0x0518*`） |
| `item.dsh` 921 | 空说明补「修炼天书」+ `rowLen`（四份资源） |
| mock（既有） | `12/1+7/42` 开背包 → `12/1+7/42+17/1`（live 仍靠宿主改写） |

## 验证

1. `make -j2`，重启 CBE。
2. 选角进图：应见 `scene_poll_urgent_arm reason=login-30-21-overflow`、`queue_scene_poll`，以及 `cbe_30_overflow_deliver … left=N` **分散在多 tick**（不应再出现同 tick 打完 4 chunk 后 `deliver_ms≈8s`）。
3. 地图应有 NPC；世界/系统聊天应有 `1/3/3` 可见行。
4. 开背包：921 显示「修炼天书」；日志可有 `cbe_cultivation_book_list_name`（17/1 后）。
