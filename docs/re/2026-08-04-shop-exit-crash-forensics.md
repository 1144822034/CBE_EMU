# 2026-08-04 商城退出卡住 / 闪退

## 2026-08-05 补功能后再次卡「正在加载」

恢复 Aug-3 任务/仓库/梦境项后，商城退出又出现加载条。对照证据：

- `bin/cbe_mmshop_exit_pop.flag` 残留 `2`（过夜 family4 peel+kind2）
- `shop_open14_trace` 已是 family-5（post-test23 包路径）
- `main.exe` 已含 `mmshop_exit_pop_skip` / peel 禁用

根因：过夜 `exit_pop=2` / `exit_relogin` 若被 poll 消费会武装
`coolmallOpenWaitMap`+kind2 `30/1`，与 01769c21 包退出冲突（post-test22 已证
明 peel+kind2 停在过渡壳/DoLoading）。进店时若残留 `coolmallOpenWaitMap` 也会
抢退出路径。

修复：

1. `arm_mmshop_exit_pop/relogin_from_host_flag` 只清 flag + `finish_shop_return_rehydrate`，**禁止 kind2**
2. scene-poll 主动 drain 上述 flag（避免残留）
3. 正常 mmShop `shop-open` 时清 `coolmallOpenWaitMap`

验证：重启 server → 江湖商城 → 返回；期望 `mock_shop_open14 ... actor_state=last`，
无 `mmshop_exit_pop_rehydrate needKind2=1`，地图可走。

---

## 运行时矩阵（debug-25c74d）

| 方案 | 结果 |
| --- | --- |
| already_open + 任意 `1/1/14` actor（含 paint-8=0） | **闪退**（post-fix2/4） |
| already_open status+money + host_clear **回调后** | 清了 load，仍卡通信条（post-fix3） |
| already_open status+money + host_clear **回调前** | 清了 load（post-fix5/6），仍卡；无 `shop_return_rehydrate` |
| skipRestore + post-callback 再清 | snapState=0，仍卡（post-fix6） |
| **退出右软 → peel + 同场景 30/1 重登角色** | 用户要求；购买确认框 确定+返回 会误触发（post-fix7） |
| 退出仅主界面 返回（非确定框）+ 买成功 host_clear | 软键要求「返回」文案过严 → 未触发重登，仍走 0x162C 卡住（post-fix8） |
| open14 二次退出 → peel 重登；软键仅挡确定框 | 已触发 arm+rehydrate，但裸 30/1 无资源（post-fix9） |
| 对齐角色登录：资源+30/1 + fresh_shell 1/1/6 | 包已投递，无户外 ScreenInit，卡在 entry root（post-fix10） |
| peel 后 poll 投递选角包 `1/1/6+1/1/15` | **失败**：deliver resp=456，无户外 ScreenInit（post-fix11） |
| peel + `coolmallOpenWaitMap` 资源族 + kind2 `30/1` | post-fix12：未走到退出（`looksMmshop=0`） |
| mmShop/outdoor 检测扩到 `0x0500xxxx–0x0507xxxx`（delta `0xaa2`/`0x1036`） | 辅助 host_clear；退出应走包 |
| **恢复 01769c21：exit already_open = status+money+live actor（bare 护甲）** | post-fix14：退出仍被标 trailing ack=1 age=361 → 无 actor |
| trailing 增加 shellAge≤90 且消费 markSrc | post-test15：进店 trailing host_clear OK；**无退出 open14**，仍卡通信条 |
| **already_open 一律 status+money+live actor（bare）** | post-test16：**SCR_Render NULL+0x10**（trailing actor resp=300）；client_out mmShop后 resp=1156 同类 |
| **去掉 already_open；open14 全族+actor；禁 vitals 抢包** | post-test17：退出 open14 resp=1342 shell=1 **仍 SCR**（vitals 未抢） |
| **四族无 actor + loading_clear + host 仅 pop mmShop** | post-test19：无 SCR；popped=1 但 **depth=2 停在过渡壳**（未露出 outdoor） |
| **exit pop 继续剥过渡壳** | post-test20：outdoorTop=0（菜单进店已拆 outdoor），停 depth=2 |
| **无 outdoor：剥到 entry root + coolmallOpenWaitMap kind2 30/1** | post-test21：**失败** — kind2 resp=1745 + 30/1 apply 后 WT30/2 清 pending，无 outdoor ScreenInit（停 01056204） |
| **无 outdoor：保留过渡壳 + waitMap kind2；跳过 mark_completed 门控 30/2** | post-test22：**失败** — keep transition `depth=2`，但 host peel 后无 scene poll，服务端无 kind2 |
| **恢复 01769c21：open14 一律 family-5 + full-wire bareDefense actor；禁用 host peel** | post-test23：**退出成功**；返回后 **看不到 NPC** |
| **出店强制非空 27/11（shop-return-force）+ warm 缺失时 poll catalog** | post-test24：用户确认返回后 NPC 已显示 |

## 返回后 NPC 缺失（post-test24）

- **契约**：mmShop 清客户端 type-21；出店后 NPC 图必须再投非空 `27/11`（`docs/re/2026-07-28-shop-return-path-matrix.md`）。
- **H_N1**：`once_or_empty` 在 once-seed 仍置位时发空 11，却标 `warm_delivered` → 后续 `catalog_skip`。
- **H_N2**：follow-up 未强制投递时，`finish_shop_return_rehydrate` 直接消费 `shopSceneNpcReseedPending`，无后续 nonempty catalog。
- **H_N3**：poll catalog 仍走 `once_or_empty`。
- **修复**：`append_scene_npcs11_shop_return_force`（清 seed 后强制 append）；follow-up / type27 / poll 三路；rehydrate 在 `!warm && npcnum>0` 时武装 `shopReturnNpcCatalogPending`。

## 2026-08-05 商城改动旁路丢失功能恢复

对照 `2026-08-03-instance-guide-bound-task-prereq.md` /
`2026-08-03-instance-guide-split-teleport-challenge.md` /
`2026-08-03-npc-multi-service-options.md` /
`2026-08-03-npc-task-daily-accept-limit.md`：对话路径曾退回「单 kind + EA 合菜单 +
offer 对白不跟 available()」。已恢复：

1. `available()` 前置任务权威 `task_state_load` 兜底；接取对白仅 `canOffer` 时切换。
2. 副本向导根对话按 mask 分「副本传送」(EB)／「挑战守关怪」(EC)；传送受
   `teleport_require_task_id` 门闩；ENTER 再校验。
3. 绑定任务「每天可接取次数」重新接到对话 / `6/14` / `6/11`（含 reoffer 与计数回滚）。

## 根因对照 `01769c21`（post-test23）

| 项 | 01769c21（可退出） | 偏离后 | 修复 |
| --- | --- | --- | --- |
| 退出 open14 | 固定 5 对象，末尾 `1/1/14` actorinfo | shell 上发 family-4 无 actor / 或 live 子集 actor | 恢复 family-5 + `build_actor_info_shop_open14` |
| actor 护甲词 | `bareDefense`（非 bare+eq） | 登录全量 `bare+eq≈38815` → SCR；去 actor → 卡条 | shop 专用 bareDefense + full wire |
| 退出推进 | `0x162C→0x11F0→0x9DE` 清 loading，CBE 自 pop | host peel/kind2 | **禁用** exit-pop；只靠包 |
| **四族无 actor + loading_clear + host 仅 pop mmShop + shop_return rehydrate** | post-test19 待验证（防 SCR，避 entry-root 重登） |

对照 `01769c21`：当时每次 open14 末尾都有 `actor_state`，护甲词为 `bareDefense`（非 `bare+equipment.armor`）。  
偏离点：already_open 去掉 actor → `0x9DE` 不清 loading；登录侧后来把护甲写成全量 → 满配 SCR。

## 护甲 38815 与商城退出

| 假设 | 结论 |
| --- | --- |
| 面板 38115/38815 是算错/乱码 | **否定**：role1 全 +16 紫装，`equip.armor≈37932`（上衣 3202×4=12808 等），`armorWord=bareDefense+eqArmor≈38115` |
| 显示 38815 **直接**导致卡通信条 | **否定**：卡条是因为规避 SCR 后不再发 actor，loading 清不掉 |
| 满配 `w4≈38815` 的 live `1/1/14` → SCR | **成立**（既有 shop_open14_full 证据）；与属性页同源数据 |

护甲数字本身符合当前 `M(L)=4×` 强化契约，不是本次退出修复点。

## 购买成功卡住（post-fix7 证据）

- `open14_uplink` len=39（疑似买）后约 1s 出现 `mmshop_exit_relogin_arm reason=right-soft`。
- 购买确认/返回被当成出店重登，打断 `14/3` 收尾。
- 修复：仅当右键=返回且左键不是确定/购买时才重登；非壶类买成功写 loading_clear flag。

1. live mmShop 上 already_open **禁止**附带 actor-state（SCR）。
2. status+money 不会走 0x9DE；只清 `*[r9+0x2868]+0x10` / busy **不足以**让 `0x162C` 完成退出并走到 `5/10`。
3. 真实退出契约（capped mall `1/1/14`）与 SCR 冲突，尚未找到安全的 actor 子集。

## 当前交付（用户明确要求）

| 触发 | 行为 |
| --- | --- |
| mmShop 顶屏 + 右软键（`e` / 软键返回） | host peel 到 entry root，写 `cbe_mmshop_exit_relogin.flag` |
| mock poll | `rehydrate` + `coolmallOpenWaitMap` 资源族 + kind2 same-scene `30/1`（post-fix12；弃用 title `1/1/6+15`） |

进店 combo trailing 仍用 status+money + `cbe_mmshop_loading_clear.flag`（不走重登）。
购买确认框（左=确定）的右软返回走正常 CBE，不重登。非壶类买成功会写 loading_clear。

## 验证

1. 进商城 → 购买普通道具成功：loading 落下，仍留在商城；日志有 `shop_buy14 result=1`，可有 `mmshop_loading_clear_ok`。
2. 购买确认框点「返回」：应只关确认框，**不应**出现 `mmshop_exit_relogin_arm`。
3. 商城主界面点「返回」：`mmshop_exit_relogin_arm` → 重进地图。
