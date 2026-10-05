# CBE 打开背包不加载传送石（item 800）

Date: 2026-10-05

Status: implemented

## 1. 当前卡点

- 可见现象：CBE 模拟器进图后打开背包，列表不出现传送石等道具。
- 触发：`bin/client_out.txt`，本机 `127.0.0.1:19090`。
- 登录已下发 `30/21`（4 行）和带 `17/1` 的场景 followup；**开背包**另发组合包。

## 2. 运行时证据

开背包上行：

```
cbe_uplink len=49 hdr=1/7/42 objects=1/7/42,1/25/5,1/6/1,1/6/13,1/6/14,1/2/10,1/25/5
```

之后没有 `cbe_backpack_clamp object=17/1`，也没有 `builtin-backpack-open`。
`is_backpack_open_request` 只认单独空 `7/42`（`offset == requestLen`）。

同形 6/* + `2/10 Type=101` 命中 `is_scene_task_subset_followup_request`。
task-subset 的 `includeSkillBooks` 要求同时有 `12/1` 和 `7/42`，此包没有 `12/1`，
因此不调用 `append_login_tail_skill_objects`，**不回 17/1**。

登录 followup 是 `12/1+7/42+…`，才会带 17/1。开背包组合包以 `7/42` 打头。

次因：17/1 曾 `mode=live-rows` 原样拷贝 Pixi 强化词条/宝石 trailer。
`mmGame:0x418C` 按 compact extra 解析；装备行后 itemId 错位，800 等后续消耗品丢失。

## 3. 根因

第一次偏离在服务端 dispatch：开背包 WT 不是单独 `7/42`，被 task-subset 接走，
背包组件 `sub_418C` 收不到 `17/1 iteminfo`。

## 4. 修改

- `mock_server_catalog.c`：`is_cbe_backpack_open_combo_request`（首对象 `7/42`、无 `12/1`、后面还有 companion）。
- `mock_server_dispatch.c`：组合包回角色 `17/1+7/42`（商城 pending 仍只劫持单独 `7/42`）。
- `mock_server_social.c`：task-subset 拒绝「有 7/42、无 12/1」。
- `network-client.c`（及 JNI 副本）：17/1 rewrite 回到 compact extra，并跳过 0x47 gem trailer。

## 5. 验证

1. `make -j2`，重启服与 CBE。
2. 进图后打开背包：应见 `builtin-backpack-open-combo`、`mock_backpack_open … 17/1+7/42`，以及 `cbe_backpack_clamp object=17/1 … mode=compact-extra`。
3. 背包列表含传送石（库内 `itemId=800`）；商城秘宝页与进图 WT6/1（`12/1+7/42`）不受影响。

## 6. 续记：显示出来但数量为 1（2026-10-05）

`17/1` 只负责列表名（`0x418C` + item.dsh）。可见堆叠是登录 `30/21` 的 `count` → `item+0xf2`。
模拟器 `vm_client_cbe_grid_wire_count` 把所有 `count>1` 的消耗品压成 1，并 `patch_common_extra_stack1`。
808/817-819 本就不会进格子；800 等正常堆叠应原样下发（上限 255）。

请**重新选角进图**后再开背包（本登录的 30/21 已经写成 1）。日志 `cbe_backpack_30_21_rows` 里 800 应变为 `800:<真实数量>`。
