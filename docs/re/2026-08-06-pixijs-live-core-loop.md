# 2026-08-06：PixiJS_web 实网主循环

phase: pixi-live-core-loop  
status: implemented (thin client)

## 目标

浏览器客户端经 `ws://127.0.0.1:19092` → TCP `:19090` 与本地 `jh-online-server` 交互，打通：

登录选服选角 → 进图 → `2/1` 移动 → 遇怪 `4/1` → operate `4/2` / 逃跑 `4/4` → NPC `26/1` → 挂机 `2/10 Type=2` → 聊天 `3/1`；背包/装备/任务/商城等薄壳可发协议。

## 运行

```powershell
# 终端1：服务端（需 MySQL）
cd bin
.\jh-online-server.exe --mock-service-only --mock-service-bind=127.0.0.1 --mock-service-port=19090

# 终端2：网关
cd PixiJS_web
npm run gateway

# 终端3：前端
npm run dev
# 勾选「优先实网」后登录（失败不静默回落 Demo）
```

## 关键改动

| 模块 | 路径 |
|------|------|
| 会话 | `PixiJS_web/src/game/GameSession.ts` |
| 协议扩展 | `PixiJS_web/src/net/protocolExtras.ts` |
| 传输 | `PixiJS_web/src/net/Transport.ts`（多段 CBMR drain；实网失败可禁 Demo） |
| UI 接线 | `PixiJS_web/src/main.ts` |
| 战斗 | `PixiJS_web/src/ui/BattleScreen.ts` |
| NPC 对话 | `PixiJS_web/src/ui/DialogPanel.ts` |
| 系统薄壳 | `PixiJS_web/src/ui/SystemPanel.ts` |

## WT 覆盖（本阶段）

- 上行：`1/12` `1/4` `1/6` `2/1` `2/3` `2/10` `4/1` `4/2` `4/4` `2/10+25/3` `26/1` `3/1` `7/42` `7/4` `7/8` `6/11` `6/4` `16/1` `29/1` `1/14` `5/1` `5/2`
- 下行解析：`30/1` `30/2` `27/11` `2/10` `2/6` `4/5` `4/10` `4/6` `4/7` `4/8` `4/4` `26/1` `3/3` `25/11` `25/12` + 尽力解析背包 blob

## 地图传送 / 障碍（2026-08-06）

| 功能 | 实现 |
|------|------|
| 边缘/具名传送点 | `parseScePortals`；AABB 中心贴 `n_teleporter`；走进 `2/10`+`2/3+27/11+7/42`，再 `25/5+2/3+27/11+7/42` 领 NPC |
| 切图落脚点 | 权威坐标来自服务端 `posinfo`：`30/1` → `30/2+pos` → `16/3` → `27/12`；软 `30/2` 无坐标只加载场景壳；`2/3` 即使 split 无 pos 的 `30/2` 也须带 `27/12+pos`；`exitID`=源图 `entryId`（SCE 0x07），落点=目标图**同 entryId** 边缘门的 `spawn`（成对回程门）；`mapID` 线字节为 GBK；切图期间停 poll，且 `sameScene` 以已加载地图 `loadedScene` 为准（勿用仅 soft 更新的 `session.scene`，否则会出现旧图+新 NPC） |
| NPC 对话 | `26/1 type=1` 开根对话；解析 `dialog` blob（action/value）；`action=4`→`6/10`→`6/11`/`6/4`；`action=1`→`type=2` 服务（含 EB 传送 `30/1`、EC 挑战 `26/0+30/9`→`30/10`→`4/10`）；**接近/碰撞 NPC** 先弹「是否与…交谈？」再 `26/1` |
| 移动存档 | `2/1 moveinfo` 必须是 `put_object_blob`（内层 BE16+方向帧，WT len=32）；裸 raw 方向服务端解不出字段，空 ACK 不落库 |
| 脱离卡死 | 菜单/U：`1/12/3 {id}` → `12/3+16/3` 落到附近 `n_telestone`（同图优先，否则 sMap/wMap 最近有石场景）；解析后 `setPlayerPos` / 换图 |
| NPC 目录 | 服务端 `27/11`（非 SCE）；首登 `startup-followup`；切图 post-enter 组合包领非空目录 |
| 传送石 | **两套路径**：(1) **地图 UI**（右下角→世界/区域图）可选 **任意** wMap/sMap 节点，确认耗 item **800**：先 `16/4` 必须拿到 `result/value`，再同包 `16/2+16/3+7/1`（应答须含 `7/1|7/7|7/11`，否则 combo 未武装 deferred），等 ≥1 个 100ms tick 后再 scene-poll 收 `30/1`；落点后 `post-enter(exitID=0)` 领非空 NPC（仍空再 `2/3`+`6/1`）；传送中忽略空 `27/11` 整表清空（见 `2026-08-09-pixi-map-teleport-missing-npcs.md`）。无进图报 **进图超时**（勿与真正落点不符混用）。deferred `30/1` 带 **owner clientId**，同账号另一端（CBE/Pixi）的 poll/`stale_map_transfer_clear` 不得抢走或清掉；`16/4`/refine 在 SCE 可读但算不出 entry 落点时回退 sMap 标记/图心（避免泰山等图整段确认失败）；Pixi `sceneKeysMatch` 另用 `数字前缀_槽位` 指纹兼容中文段解码漂移；(2) **场景传送石 NPC** `n_telestone` 点开 `16/1`，目的地受 **exitinfo 列表限制**（非任意）。地图 UI 光标优先 DSH 邻接，邻接为空则按坐标最近方向补全 |
| 切图贴图 | `showScene` 按 `sceneFile|mapFile|尺寸` 换底图；临安多为 400×400，不能只比宽高（否则 _01→_05 底图不换） |
| HUD 地图名 | `sMap.dsh` 第 2 列别名（如 `_06` → `临安-庆阳宫`） |
| 地图装饰 | SCE 装饰表 `01 00 01 00 …` + `b_flowers01.actor` / `FB_*` + 8 字节落点；`showScene` 绘制（非 MAP 地砖） |
| 战斗施法 | 外壳：忽略地址栏高度抖动、中软键占位；固定 240×400；eidolon≤96；pose 纹理缓存；BufferImageSource rgba8unorm；jian/guang 跳过；进/出战清粘键 |
| 障碍 | MAP 高 4 位：`0xF`=实心；边缘 N/W/S/E=`1/2/4/8`，跨格用 CBE exit\|entry（见 `2026-08-09-pixi-map-edge-collision.md`）；桥/岸同方向 fringe 条带接缝放行（见 `2026-08-13-pixi-bridge-fringe-strip.md`）；脚底半径 1 |
| 遇怪清图 | 碰撞/点击 SCE 怪进战即 `suppressSceneMonster`；开战失败立刻还原；战后 `armSuppressedRespawns(10s)` 回出生点（见 `ActorLayer`） |

证据：admin/mock SCE portal 解析；`docs/re/2026-06-25-teleport-stone.md` 高 nibble 碰撞；碰撞可视化 `tmp/facing-probe/coll_*.png`。

## 验证

1. `cd PixiJS_web && npm run build`
2. 实网登录进图；WASD 后服务端日志出现 `moveinfo_store` / `mock_actor_moveinfo_ack`
3. F 开战 → 左软攻击 → 右软逃跑或结算后回图
4. 点 NPC 打开对话；H 挂机；C 聊天

## 实网联调根因（2026-08-06）

首包 `1/12` 曾被服务端 `drop-no-account`（TCP 直接关、无 CBMR）：

1. **字段契约**：`vm_net_mock_is_login_validation_request` 要求 `coreVer/appVer/userName|username/password/imsi`；访客还需空串 `userName`+`password` blob（`valueLen=2` 内嵌 `dataLen=0`），不能只发 `newVer`。
2. **请求线格式**：服务端按 CBE/PHP 解析——无 objectCount、对象头 5 字节（`maj|kind|sub|BE16(len)`）。Pixi 曾误用响应线格式（count + 6 字节头），导致 `get_wt_header_kind_subtype` / `next_request_object` 错位。`WtBuilder.finish()` 已改为请求线；`finishResponse()` 留给 demo。
3. **codeVersion**：本机 `server_code_version.txt=2`，登录包带 `codeVersion=u32(2)`。

复测：`npm run smoke:live` → `servers≥1`（示例：`江湖归来`，`result=3` 访客发号）。

### 选角进图（2026-08-06）

实网 `1/1/6` 响应是 `1/1/6+1/1/15`（actorinfo 内含 `sceneKey` + grid），**不会**在同包立刻回 `30/1`。后续 WT `5/10+7/7` 与 len=39 的 `6/1+6/13+6/14+2/10{Type=101}+25/5` 才走资源 follow-up（常回 `30/2` 无 posinfo）。

Pixi：`parseRoleSelectEnter` 从 `1/6.actorinfo` 取场景/血蓝；再发 group-type1 + scene-startup follow-up；**同场景 soft 30/2 不再二次 showScene**（避免清空 27/11 NPC）。

`actorinfo` 的 hpMax/mpMax 是 **bare**（装备加成前）；current 可能大于 bare。HUD 用 `max(bare, current)` 作进图初值，开战后再用 `4/5` 右侧 full max 校正。

选角进图 `1/6.actorinfo` 的 sceneKey 在切图后可能是无后缀 `c04临安府_06`（`normalize_scene_name_for_enter`）。Pixi 曾要求 `/\.sce$/` 才进图，导致选角后抛「未解析到场景」。现补 `.sce` 再加载资源。

复测：`PIX_USER=lxh001 PIX_PASS=123456 npm run smoke:world` → ENTER + NPC≥1 + `2/1` + `4/5` + 逃跑 `4/4`。

### 切图落脚点（2026-08-06 晚）

触发：无情谷→临安 `2/3` 只回软 `30/2`（无 pos），post-enter 同样无 `27/12`，客户端落地图中心/旧坐标。

根因（陆续）：

1. Pixi `mapID` 曾发 **UTF-8**；Windows 磁盘叶是 **GBK**，打不开 SCE → `pos=(0,0)`。
2. `download_key` 误当成“缺文件”。
3. **落点绑错 ID**：曾用 `targetEntryId`(0x13)；权威是源 `entryId`(0x07) → 目标图**同 entryId** 回程门的 `spawn`（后台传送点编辑 / 临安成对门 SCE 一致）。

修复：wire→GBK；portal miss 仅真缺文件才 download；`resolve`/`exitID` 按成对 `entryId`；split 可带 `27/12+pos`。

复测：`npx tsx scripts/smoke-portal-landing.ts`（临安_10→_07 期望约 `@51,207`）。

### NPC 对话菜单（2026-08-06）

根对话 `26/1 type=1`；`dialog` blob 选项 `action/value`：

- **任务** `action=4` → `6/10` 详情确认 → 接取 `6/11{taskinfo}+25/5` / 提交 `25/5+6/4{taskid}`
- **功能** `action=1` → `26/1 type=2 id=opcode|…`（商店/修理/技能等嵌套页）
- **离开** `action=1 value=0`（服务端空菜单注入）→ **本地关对话**，勿发 `26/1`（否则 `功能暂未实现(1/26/1)`）
- **传送** `EB|actor` → `30/1` 进副本
- **挑战** `EC|actor` → `26/0+30/9` 确认 → `30/10{agree}` → `4/10`

复测：`npx tsx scripts/smoke-npc-dialog.ts`（临安梦境使者 → 副本传送 → `30/1` 梦境）；仅「离开」的 NPC 应回世界且无未实现 tip。

## 进图 HUD 资源（2026-08-06）

排布以原版 CBE 截图像素为准，见 **`2026-08-06-pixijs-world-hud-layout.md`**（`WORLD_TOP_H=72`）。

| 区域 | 原版资源 |
|------|----------|
| 地点条 | Graphics 圆角条 + 动态地点名 Text（不用 `UI2` 盖状态行） |
| 头像框 | `handkuang.gif` + `ui_h_*` / `ui_hw_*.actor`（框与装备槽同资源） |
| HP/SP 框 | `tiao.gif` + 槽内红/蓝填色 |
| 快捷 | `UIsh1..4.gif` + `UIsh6.gif`（元宝）；x0=91 pitch=31 |
| EXP | `uiexp.gif` + `numberexp.gif` + `exp0.gif`/`exp1.gif` |
| 底栏 | `UIxia1` / `UI7`+「挂机」 / `UIxia3`（参考图无背包计数） |
| 地图角标 | `UIRed` / `training.gif`（修） |

称号来自 `1/6.actorinfo`：`shortLabel` 为人名文案，`previewImage` 为头顶 GIF（`riches_nameN.gif` / `level_nameN.gif`）。Pixi 名牌贴 GIF，不用红字 DrawString。

名牌/世界聊天/NPC/怪物名字使用 `bin/font_gb.uc3`（与 `src/fontEngine.c` 同库）。

## 场景人物 / 怪物动画（2026-08-06）

`.actor` 地图播放契约（`composePartStacked` / `composeMapActor`）：

- 每个 **part** 内 `frames[]` 为叠层（影/身/武器）
- **玩家**（`player4`）：朝向按 **anim**（**禁止** anim8/9，二者含 `death.gif`）  
  - 下(S)：`anim1` 正面（走/站都用，走时 cycle parts）  
  - 上(W)：`anim3` 背面（同上）  
  - 右(D)：走 `anim0`；停步收脚用 **walk part6**（非 stand anim5 part0，后者腿分开）  
  - 左(A)：走 `anim2`；停步收脚用 **walk part6**（非 stand anim4 part0）  
  - 侧向 stand `anim4`/`anim5` 去剑光仍可用于战斗/就绪姿，地图停步不用 part0  
  - 注意：`anim0`/`anim1`、`anim2`/`anim3` **不是** 同向 walk/stand 对；`anim0`=右走、`anim2`=左走
- **怪物**：SCE 地图精灵几乎全是 `e_ghostfire*`（census 无其它 map actor）  
  - **4 个 unique part = 闪烁帧，不是四向** → `partMode=frames`，按 `phase` 轮播，忽略朝向  
  - 战斗体（蜜蜂/蝙蝠等）仍用 field `0x11`，不进地图  
  - 其它 `e_*`（若直出）：`dirFlap`（2 姿×扑动）或 `dir`（真四向）
- **NPC**：单 part 多图层静止

### 地图怪物来源与巡逻

野外怪在 **SCE kind=3**（不在 `27/11`）。

**双 Actor 契约**（户外 census / `mock_server_scene_task`）：

| 字段 | 资源 | 用途 |
|------|------|------|
| field `0x11` | 如 `e_beesY.actor` | **战斗**外观 |
| field `3` dual | 如 `e_ghostfireR.actor` | **地图**精灵（一团火） |

无情谷北：地图应是鬼火，进战才是黄色蜜蜂群。Pixi 地图加载 `mapActorResource`（dual）。

本地在刷怪点半径内随机走停（视觉巡逻）。

## 战斗数据权威（2026-08-06）

**服务端算，客户端播。** 客户端只上传操作项（`4/2` Operate / `4/4` 逃跑），伤害与结算一律来自下行包；禁止本地扣血或伪造 `100/100`。

| 角色 | 行为 |
|------|------|
| 客户端 | 选目标 wire → `4/2`；解析 `actioninfo` 播放飘字/技能效果；套用 `valueA` 到 UI HP |
| 服务端（含 DemoTransport 替身） | 计算伤害、反击、死亡、`4/7` 经验金钱 |

| 包 | 用途 |
|------|------|
| `1/2/2 moveinfo` blob+44/+48/+52/+56 (LE) | subtype `4/5` 左侧怪物 hp/mp/max |
| `4/5` / `4/10` `battleinfo` 右侧行 | 玩家开战 vitals |
| `4/6 actioninfo` | 回合脚本：type/actor/children/`valueA`（有符号）；flag `2` 暴击 `3` 闪躲 |
| `4/6 teaminfo` | 玩家 MP |
| `4/7` | 结算；确认回图，`4/8` 由 heartbeat 消化 |

Pixi：`parseSceneMoveSeeds` + `parseBattleStart(ctx)`；`parseActionInfoScript` → 飘字播放；已移除 `applyLocalDamage`。

战斗场景视觉见 **`2026-08-06-pixijs-battle-scene.md`**：背景为 JHOnlineData **`b_场景名.sce`**（如 `17无情谷_01` → `b_17无情谷`），非野外大地图裁切。

## 未决 / 延后

- 背包 `iteminfo` 字段布局未完全取证，列表可能显示 `unresolved` 数字
- 组队连打、梦境、coolmall、原生银行不在本阶段
- 遇怪 `4/1` 的 id/index 在无怪物目录时仍 fallback `9001`
- `codeVersion` 现硬编码为 2；与服端文件不一致时需对齐或改为读 `client_code_version.txt`
- 多怪 wire 表（N=2/3）与技能列表仍待实网逐条回归
