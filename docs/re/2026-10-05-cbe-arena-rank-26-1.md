# CBE 比武排行榜 1/26/1 未实现

Date: 2026-10-05

Status: implemented

## 现象

英雄榜 NPC 点「比武排行榜」后，CBE 提示 `功能暂未实现(1/26/1)`。

## 根因

1. 该选项 opcode 是 `0xf6000003`。Pixi 在客户端拦截后改走 `41/2` 面板，**不发** `26/1 type=2`。
2. CBE 按 `task_hall_activate_selected_entry(0x010492B0)` 发 `26/1 {type=2,id=0xf6000003}`。
3. 旧服务只回一句「请打开比武排行榜…」+「确定」`value=0`。CBE 再点确定会发 `type=2 id=0`，检测器不认，落到 `unimplemented-handler-notice`。

## 契约

请求：单对象 `1/26/1 type=2`。

| id | 行为 |
|---|---|
| `0xf6000003` | 打开默认职业×段位榜（与 41/2 同源：0 分不上榜） |
| `0xf8 \| page \| job<<8 \| bracket<<16` | 切页/职业/段位 |
| `0xf6000006` | 领奖（与 `41/3` 同一发放逻辑） |
| `0xf6000007` 或 `id=0` | 回 `26/0` 关对话，避免 unimplemented |

响应：`1/26/1 hidebtn + dialog`（`ParseNPCDialogData 0x010380E8`）。正文列名前 5 名；选项含领取/切换/翻页/关闭。

## 修改

- `mock_server_scene_sync.c`：服务检测放宽 type 编码、放行 `id=0`；RANK/VIEW/CLAIM/CLOSE
- `mock_server_arena.c`：`npc_rank_fill` / `try_claim`
- Pixi 仍拦截 `ARENA_NPC_RANK`，面板路径不变

## 验证

- [ ] CBE 对英雄榜点「比武排行榜」出榜单，不再提示 `1/26/1` 未实现
- [ ] 切换职业/段位、翻页
- [ ] 结算后前五可领奖；背包满拒绝
- [ ] 关闭回地图，不弹 unimplemented
- [ ] Pixi 英雄榜仍开自定义面板
- [ ] `make -j2`
