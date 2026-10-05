# CBE 模拟器：临安府江湖商城退出 `vm_free` 闪退

Date: 2026-10-05

Status: implemented

## 1. 当前卡点

- 可见现象：CBE 连怀旧服，从江湖商城点返回后进程直接退出。
- 触发方式：桃花岛挂机 → 传送石到 `c04临安府_06.sce` → 开江湖商城 →（左软键一次 `14/3`）→ 右下返回。
- 本轮最小目标：钉死第一次偏离点；在未确认 `mmShop:0x9DE` 调用 `parse_actorinfo` 的 `a2` 之前不改发包。

## 2. 运行时证据

`bin/client_out.txt` 末段（同一会话）：

1. 进商城：`cbe_uplink … 1/2/10,1/14/14,1/14/4,1/14/5,1/14/6`，`queue_data resp=235`。无 `1/1/14` actor。
2. 左下软键：`cbe_uplink len=39 hdr=1/14/3`，`resp=33`（单对象买回执量级），随后 host `mmshop_loading_clear reason=pre-callback`。
3. 右下返回：`cbe_uplink len=25 hdr=1/1/14`，`queue_data resp=499`。
4. `screen_mgr remove requested=010536b4`（商城顶屏）`caller=0502d1b0`（mmShop destroy 量级）。
5. `[vm_free] INVALID addr=0x00630000` → `src/vmMalloc.c` `assert(0)`。

`bin/shop_open14_trace.log` 末行与本次 `resp=499` 对齐：

`open14 family-5 actor=238 shell=1 page5=0 resp=499`

对照历史可退出包：同 `actor=238` 时 `resp≈1420`。差值来自目录页（商城 `14/6..13` 已停售装备，`14/6` 空页），不是 actor blob 长度变了。

已知会发生的下一步：`0x162C → 0x11F0 → 0x9DE` 吃完 family-5 末尾 `1/1/14` 后 CBE 自己 pop 商城，再靠 shop-return 补户外图。

实际缺失的下一步：pop 已开始，destroy 把非 VM 堆指针交给 `vm_free`。崩溃 PC 是 host 断言，不是 CBE parser 里第一次写坏。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `mmShopMstarWqvga.cbm` | `sub_162C` / `0x162C` | 返回发 `1/1/14(actorId)` | 已有：只置 loading 再 `sub_11F0`，不另发 `14/5`/`14/6` |
| `mmShopMstarWqvga.cbm` | `sub_9DE` / `0x9DE` | 解析退出包 | 已有：`14/*` 分页；`1/1/14` 读 `revivetype/ruffianflag/type` + 共享 actorinfo，清 loading 后 **立即 return**（actor 必须在最后） |
| `江湖OL.CBE` | `parse_actorinfo_response` / `0x0100FA88` | `a2==0` 全量 vs `a2!=0` live 子集 | **本轮未重读 IDA**。`a2!=0` 会跳过 sceneKey 等读且不消费 wire |
| `mmShopMstarWqvga.cbm` | destroy ≈ `0x0502D1B0` | 日志 `caller=0502d1b0` | 应对齐：释放哪些字符串/页行指针 |
| `mmShopMstarWqvga.cbm` | `sub_7BC` / `0x7BC` | 空 `14/6` 是否仍按 `totalnum` 扫 `iteminfo` | 已有：先 `totalnum` 再 raw `iteminfo` 行数 |

## 4. 调用链 / 业务流程

1. 菜单进江湖商城会先 `remove` 户外屏 `01053450`，栈顶落到过渡壳 `0105A814` 再 push mmShop（本次同样）。
2. 开店 combo 由 `14/14+14/4+14/5+14/6` 填页；`0x9DE` 靠这四族关 loading，**不经 actor 分支**。
3. `14/3` 成功：parser 走买完成；服务端另写 `cbe_mmshop_loading_clear.flag`，host 在回调前清 `*(r9+0x2868)+0x10`。
4. 返回：`0x162C` 再发 `1/1/14`。服务端 `mock_shop_open14` 仍按 01769c21 回 family-5（含空 `14/6` + full-wire `actorinfo`）。
5. `0x9DE` 扫过页对象后命中 `1/1/14`，调共享 actorinfo，清 loading，return；CBE pop mmShop。
6. destroy 对某字段 `vm_free` → `0x00630000` 不在 VM 块表。

## 5. 结构体 / 状态字段笔记

- `0x00630000` 小端字节 `00 00 63 00`：16 位 `0x0063`（ASCII/`UTF-16` `'c'`）落在指针高半字。
- 当前角色场景 `c04临安府_06.sce`；shop_open14 full-wire 在 live 跳过的尾部仍带 `sceneKey`。
- `parse_actorinfo(a2!=0)` 不读也不消费 `sceneKey`；若 `0x9DE` 走这条，scene 串会错位成指针。
- 01769c21 要求 open14 **必须** full-wire + `bareDefense`，暗示 `0x9DE` 当时按全量消费；与 live `1/1/14` 地图 HUD 不是同一 `a2`。
- confidence：闪退发生在「actor 已吃完、开始 pop」之后 — 高；指针来自 `c` 前缀 sceneKey 错位 — 中；空 `14/6` 单独导致 — 低（开店时已经发过空页且当时未崩）。

## 6. 请求 / 响应契约

### Request（退出）

- WT：`1/1/14`，`len=25`，含 `actorId`。

### Response（当前实际）

- 标签：`mock_shop_open14` family-5，`actorinfo_len=238`，`resp=499`。
- 对象顺序：`14/14` + `14/4` + `14/5` + `14/6(空)` + `1/1/14` actor（full-wire）。

### 开店 combo（对照）

- 请求：`2/10+14/14+14/4+14/5+14/6`。
- 响应：`resp=235`，无 actor。页已在开店时建过。

## 7. 成功路径与失败路径

### Success path

- 退出 `1/1/14` 末对象走 `0x9DE` actor 分支，清 loading，CBE 自 pop。
- 历史 post-test23：family-5 + `build_actor_info_shop_open14`，`resp≈1420`（当时 `14/6` 仍有装备行）。

### Failure path

- 本次：同样 family-5 且 CBE 已 pop，destroy `vm_free(0x00630000)`。
- 更早矩阵：already_open 再塞任意 live/capped actor → `SCR_Render NULL+0x10`（症状不同，同属 actor 污染页/字符串状态）。

## 8. Negative Evidence

- 不是未处理包：有 `queue_data resp=499`，`shop_open14_trace` 命中。
- 不是开店 combo 被挂机吞：可见完整 `14/*` 上行（2026-10-04 那次误吞已排除）。
- 不是 `14/3` 当次带 live actor：`resp=33` 对不上 `14/3+1/1/14`。
- 未在本轮改 host peel / 丢 `1/1/14`：那条会卡通信条或停过渡壳（2026-08-04 已证）。

## 9. Unknowns / Hypotheses

- unknown: `0x9DE` 调 `0x0100FA88` 时 `a2` 是 0 还是非 0。
  - current guess: 退出路径按全量读；若其实是 `a2!=0`，`c04…` sceneKey 会变成 `0x00630000` 这类假指针。
  - next probe: IDA 看 `0x9DE` 调用点的 `a2`；destroy 释放的槽。

- unknown: `14/3` 后 host `loading_clear` 是否让第二次 `0x9DE` actor 写到已完成的页/字符串槽。
  - current guess: 买成功清条 + 立刻退出是本次特有组合。
  - next probe: 不买直接退出是否仍崩；以及关掉 flag 清 loading 是否仍崩（只作对照，不作为修复）。

- unknown: 空 `14/6` 在「已有页状态」上被 family-5 再解析一次是否改 cursor。
  - current guess: 开店已解析过空页未崩，优先级低于 actor。
  - next probe: 对已 open 的退出包只保留末尾 `1/1/14`（需 IDA 证明 `0x9DE` 允许缺页对象）。

## 10. 本轮实现计划

- 计划改动：`mmShopShellActive` 时退出 `1/1/14` **不再重发** `14/5`/`14/6`；仍发 `14/14+14/4+open14 actor`。首次 `0x162C`（shell 未置位）保持 family-5 内联页。
- 目标文件：`src/server/mock_server_interaction_login.c`
- 为什么这次只改这一小块：IDA `sub_7BC` 从 `[page+6]` 追加行；combo `0x1038` 已填页，exit family-5 第二次 `iteminfo` 越界，destroy `vm_free(0x00630000)`。不吞 actor（否则卡通信条），不 host peel。

## 11. 验证清单

- [x] 请求被预期 detector 命中（`mock_shop_open14`）
- [ ] 重启 server 后退出日志 `pages=omit-already-filled objects=3`，无 `vm_free INVALID`
- [ ] 首次仅 `0x162C` 开店仍 `pages=inline objects=5`
- [x] 没有强写客户端全局状态
- [ ] 返回后地图可走、NPC 仍在（shop-return `27/11`）
- [ ] 买一次再退出不闪退
