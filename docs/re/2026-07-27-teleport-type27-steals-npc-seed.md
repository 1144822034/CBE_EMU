# 瞬移至铜雀台 NPC 全无（type27 抢先消费 wait-wt6）

日期：2026-07-27

## 现象

使用瞬移道具进入 `c00蓬莱仙岛_01` 后地图可见，但一个 NPC 都没有。

## 运行日志（首个偏离）

```text
mock_scene_npc_seed_defer ... next=WT6/1 reason=map-stone-shell-not-runtime-ready
mock_scene_npc_poll_hold ... reason=wait-wt6
mock_scene_npc_catalog ... actors=0 selected=0 dynamic=0
mock_scene_npc_seed phase=type27-followup ... npcnum=0 once=1
mock_scene_resource_followup_repeat_ack ... completion=none
```

没有出现 `mock_scene_npc_seed_deliver ... phase=WT6/1`。

## 根因

地图石 `download=0` 路径约定（见 `2026-07-22-teleport-tonguetai-npc-lifecycle.md`）：

1. WT2/3 只回空 `27/11`，置 `wait_wt6`，保留一次性 NPC 目录；
2. 随后 WT6/1 才投递非空 `27/11`。

客户端在 WT6/1 之前还会发 `type27-followup`（25/5+27/11）。该路径原先直接调用
`append_scene_npcs11_once_or_empty`，即使 `npcnum=0` 也会：

- 置 `seeded=1`
- 清 `wait_wt6` / `pending`

于是真正的 WT6/1 变成普通 `resource_followup_repeat_ack`，不再投递 NPC 目录。

这与 poll 侧已有的 `mock_scene_npc_poll_hold ... wait-wt6` 防护不一致。

## 修改

`vm_net_mock_build_type27_followup_combo_response`：当 `wait_wt6` 且 pending 仍指向
当前场景时，只回空 `27/11` 门控对象，**不**消费一次性目录；日志
`mock_scene_npc_type27_hold ... reason=wait-wt6`。

同时把 `current_scene_name()` 拷到局部缓冲，避免静态 normalize 缓冲被嵌套调用覆盖。

空目录时补一条 `mock_scene_npc_collect_empty` 诊断（override 命中数）。

## 验证

1. 瞬移进铜雀台：顺序应为
   `seed_defer` → `type27_hold wait-wt6` → `seed_deliver ... WT6/1` 且 `npcnum>0`。
2. 画面出现郭芙蓉 / 白展堂 / 大侠郭靖（或当前 `server_dynamic_npcs` 中该图启用行）。
3. `make -j2` 通过后重启服务再测。
