# 2026-08-06：PixiJS 战斗场景按 mmBattle 还原

phase: pixi-battle-scene  
status: implemented (map bg + HUD shell + FX)

## 目标

浏览器战斗屏对齐 `mmBattleMstarWqvga.cbm` + 用户截图：专用战斗地图、顶栏/底栏 HUD、卷轴指令、服务端 `actioninfo` 特效播放。

## 战斗背景（权威）

JHOnlineData 中战斗地图为 **`b_` + 场景名 `.sce`**，与野外 SCE 配对：

| 野外 | 战斗图 |
|------|--------|
| `17无情谷_01.sce` | `b_17无情谷.sce`（另有 `b_17无情谷m.sce`） |
| `01桃花岛_01.sce` | `b_01桃花岛.sce` |
| … | `b_*.sce` / 对应 `.map` |

解析：`PixiJS_web/src/resources/battleScene.ts`（去 `_NN` 后缀后加 `b_`）。  
管理页 portal field `0x12` 也可直接写 `b_*.sce`（`web_admin_server.c`）。

`DrawBattleSceneBg` 画的是这张战斗图，不是标题 `Background.gif`，也不是野外大地图原样裁切。

## 舞台布局（对照用户截图）

| 区域 | 内容 |
|------|------|
| 顶栏 | 深青条 + `ui_timer.gif` + `time_number.gif` 倒计时（超时默认普攻；多怪时也直接 `4/2`，不进选目标） |
| 中部 | `b_*.sce` 地图铺满；敌左 / 我右；脚下阴影；**无**头顶血条（仅名牌） |
| 卷轴菜单 | 行动：攻击 / 仙术 / 物品 / 逃跑 / 自动 / 查看 |
| 底栏 | 至多 3 个队友槽：`xuetiao.gif` + `ui_hpsp` 青字 + `ui_hpspnum` 数字；头像 + 粉名 |
| 软键 | 确定 / 逃跑 |

站位仍按协议：左≈x40 一带、右≈x200（屏幕坐标按舞台带微调）。

## 技能特效

- `eidolon.dsh`：`序列号 → f_*.actor`（明文 DSH，非 LZSS）
- `4/6 actioninfo` type1/2 的 `effectIndex` → 加载对应 `f_flame1` / `f_sword2` 等在目标脚点播放
- type0 普攻（玩家或怪物）：只飘伤害，不播 eidolon；怪物技能仅当服务端 `cast_skill` 写成 type1 并带 `effectIndex>0` 时播放
- **怪物技能特效（2026-08-08）**：`playSkillEffects` 在每个 child wire 播 eidolon；wire 解析失败时回退到施法者脚点，避免反击 type1 无锚点时整段特效被跳过；缺 `eidolon.dsh` 行时打 warn
- **玩家死亡退出（2026-08-08）**：HP=0 后服务端只武装复活确认、不下发 4/7/4/8。Pixi 在 actioninfo 播完后弹出死亡面板，发 `WT 1/7/14`（有 801→result=1 复活石；否则/右键→result=2 普通复活）。result=2 的 `20/1+30/1` 场景进入会 `battle.hide()`
- **怪物死亡退场（2026-08-08）**：按 `state.left` 判定敌方，不再用 `facingRight`（施法/治疗会改朝向，导致首个击杀怪留在场上）
- **多角色开战立绘**：以 `selectedRoleId` 解析职业/性别，避免 stale `activeRole`（账号首角色）污染战斗人物
- **遇怪种类一致（2026-08-10）**：subtype-5 多怪不再用 `index+i` 当 left id；`pendingEncounterVisual` + 一次 resolve，场上克隆只显示碰撞到的那种（见 `2026-08-10-pixi-encounter-battle-species-match.md`）
- **选角串角色（2026-08-08）**：`selectRole` / `clearWorldState` 清空背包·装备·技能·队伍·任务·NPC 目录等角色绑定缓存；同镇再进时 `onEnterScene` 清 peers 并强制 `setNpcs`（可空），避免 merge-by-itemId / 同图跳过 clear 把角色 A 带进 B
- **选角地图/组队条（2026-08-08）**：`onRoleSwitch`+`invalidateMap` 丢弃上一角色地图壳，进图只认 `1/6.actorinfo` 服务端 scene（不再 fallback 默认图）；`5/10` `memberCount<2` 视为非组队，左上角队友血蓝强制清空
- **选角又进旧图（2026-08-08）**：同包/`follow-up` 里 deferred `30/1|30/2` 曾优先于或覆盖 `1/6.actorinfo`。现优先 actorinfo；`roleEnterSceneLock` 锁住选角场景；`enterSceneGen` 防止被取代的异步 showScene 回写 `loadedScene`
- **离开游戏清本地 session（2026-08-08）**：`logout` → `resetLocalSession` 断联并清空场景/技能/背包/队伍等全部运行时状态（仅保留账号口令供再登录）；UI 同步清空技能/背包/地图壳，避免切角色残留
- **单位动作**：待机循环 `composeBattlePose`；玩家普攻闪现到怪侧再劈砍（面向目标）；施法**钉死站位**播 anim6/7（风起 2 帧后 eidolon/伤害同拍重叠，鬼道不后退）；受击短闪；怪物普攻闪现到玩家侧；致死后若脚本紧跟 type3，死亡 splash 与特效同拍（见 `2026-08-30-pixi-battle-cbe-timing-continuity.md`）；type3 怪播 `f_death.actor` 后淡出、玩家 death.gif
- **帧拍**：战斗条 / eidolon / `f_death` 默认 **100ms/part**（对齐 mmBattle 常见 tick；DrawBattleCharacter 精确定时仍 unresolved）
- **顶栏「%s行动」（2026-08-21）**：CBE 按当前 `actioninfo` 的 `actor_wire` → battleinfo 单位名切换；Pixi 曾只在进战写本地名。现于 `playActionScript` 每条记录开始时 `paintActingTitleForWire`，菜单再开时 `paintLocalActingTitle`。
- **击杀不跳过攻击动画（2026-08-30）**：单人致死 `4/6+4/7` 不得在播报前把 left HP 清零，否则 `isUnitDefeated` 会跳过本席动作/特效；清零改到 `playActionScript` 之后（见 `2026-08-30-pixi-settle-skips-attack-anim.md`）。
- **回合内出手不抢跑（2026-08-30）**：跨包须 `playbackChain` 串行；同包 skill→death 同拍重叠，仅在下一 *actor* 前短 settle（见 `2026-08-30-pixi-battle-playback-queue.md` / `2026-08-30-pixi-battle-cbe-timing-continuity.md`）。
- **天机待机武器（2026-08-08）**：raw job0=天机→`h_*warrior`+`jian.gif`。战斗待机曾套用地图用 `SIDE_MAP_SKIP(/jian|guang|)` 把剑层剥掉，而幻剑 `bishou`/鬼道 `zhang` 不受影响。现战斗 idle/hit 仅跳过 `guang|death`，保留武器层；地图侧仍用 `SIDE_MAP_SKIP`。
- **飘字**：伤害/回血用 CBE `-num.gif` / `+num.gif`；锚在目标躯干中心。「闪躲」黑字+细绿描边；「暴击」黑字+细青蓝描边并叠伤害数字
- **朝向（资源证据）**：`hW_warrior` 站立 left=anim4 / right=anim5，攻击 left=anim6 / right=anim7（剑刃相对身体质心）；蜂类同 anim 内 p0==p1、p2==p3，扇翅靠 anim0↔1 图层偏移；朝玩家用 part2，禁止 flipX
- 单段攻/技能播放约 **2–3 秒**（含冲撞/施法与特效）
- **解析**：`actioninfo` 与 mock `seq_put_u8/u32` 一致（tagged `00 01` / `00 04`）；仅按 raw u8 读会整包失败，UI 只剩「actioninfo」且无飘字/特效
- **画面胀缩（2026-08-07）**：用户反馈施法/普攻时整屏变大变小。根因更正为 **外壳 CSS 缩放抖动**（非 AABB）：(1) 手机浏览器地址栏伸缩触发 `#stage` ResizeObserver → `fitPhoneWrap` 改 `#phone-wrap` 尺寸；(2) 中软键 `hidden` 切换改变底栏高度。修复：忽略高度抖动、中软键占位不卸布局；另固定 240×400、FX≤96、BufferImageSource `rgba8unorm`、战斗 pose 纹理缓存。攻/施法 pose **恢复** jian/guang（挥砍）；待机/地图仍 skip。

## CBE / mmBattle 怎么播战斗立绘（对照）

```text
4/5|4/10 HandleBattleStartMsg
  → 单位表：id/name/HP/MP + sprite 指针（已加载的 .actor）
4/6  HandleBattleActionMsg
  → action 队列（type0 普攻 / type1|2 技能+effect_index / type3 死亡…）
播放帧
  → mmBattle 改单位状态 / 坐标 / 当前 anim
  → 主 CBE PicLib blit（DrawPicLibSpriteAlpha 等）画出 sprite
  → type1|2：effect_index → eidolon.dsh → f_*.actor（AnimEffect 路径）
站位
  → CalcTargetSideIndex：怪左 / 我右（wire ↔ slot）
```

Pixi 不复刻 CBM 内部状态机，而是：**同一套 `.actor` 资源** + `composeBattlePose` 选 anim/part（怪物待机优先 part-strip / 蜂类双 anim 扇翅），再由 `BattleScreen.playAttackMotion` 做冲撞/回位。

**资源契约（对照 CBE 播法，非 walk 交替）**：

| 类型 | 例 | 待机 | 攻击 |
|------|----|------|------|
| stand 末层扇 | `e_ratB` / `e_stoneJY` | 身固定，交替末层 | anim1 / slash |
| stand 呼吸 | `e_believer*` / `e_solider*` ×8 | p0↔p4 offset 轻起伏 | anim1 slash |
| offset 双生扇 | `e_beesY` 同 rect 只改位移 | 固定 part2，anim0↔1 | 同 |
| 无 offset 对 | `e_kirin`（stand 内 rect remap） | stand 朝向（可静） | walk/slash |

**怪物类型 0–11（2026-08-10 取证）**：后台「怪物类型」`family` 0–11（胶质…首领）只服务端属性公式，**未**进入 `battleinfo` / 待机选帧。mmBattle 里 `cmp #0xb/#0xc` 是席位循环（最多 12）或 UI jump-table，不是 family→anim。SCE field-16（visual hint）取值稀疏 `{0,5,6,17,18,25,66,68,70}`（绝大多数 `5`；`e_ratB=18` 而 `e_ratG=5`），与 0–11 family 不对齐，且 subtype-10 误当玩家 visual 会画错肖像。结论：待机差异看 `.actor` 图层结构，不是类型开关。

**unresolved（IDA 未挂上）**：`DrawBattleCharacter` 精确选帧计时、近战位移是插值还是瞬移、unit 结构里 anim/part 字段偏移。磁盘 CBM 相对 IDA VA 需 `file[1:]` 对齐（见 `2026-07-28-hangup-entry-countdown-3s.md`）。

## 仙术列表（角色绑定 + 服务端权威）

- 已学技能 ID：WT **`1/12/1`** `{ learnednum, learnedskill }`（登录尾 / 场景技能刷新）
- 名称 / 耗蓝 / 技能图片：本地 **`skill.dsh`**，按 actorinfo 职业 rawJob `0..2` 过滤
- 战斗菜单不再使用客户端硬编码技能表；`Operate = skillId + 2`（普攻 `0`）
- 选中仙术后：软键「确定」或仙术模式下中键 → `4/2`；空列表提示等 `12/1`
- **请求契约**：首场景 follow-up 必须为 WT **len=49**（空 `12/1`+`7/42`+任务族）；仅发 len=39 时服务端 `includeSkillBooks=0`，不会下发已学技能。开战前若仍空会再发 len=14 的 `12/1+7/42` 刷新

证据：`docs/re/2026-06-28-role-skills.md`、`JianghuOL.CBE:0x01010594`

## 验证

1. `npx tsx scripts/smoke-battle-sce.ts`
2. `npx tsc --noEmit && npx tsx scripts/smoke-protocol.ts`
3. `npx tsx scripts/smoke-battle-monster-idle.ts`（鼠/蜂 idle 有动效；骷髅 idle 无 `wuqi1`）
4. 无情谷遇怪：背景应为 `b_17无情谷`；仙术列表应为本职业已学技能；确定后应有 `4/2` 与 actioninfo 特效；待行动应摇尾/扇翅，不应挥砍

## 未决

- 卷轴是否另有独立 GIF（现用 Graphics 仿截图；`UI_menu2.gif` 为 75×75 小卷轴框，未强绑）
- 查看未接；物品 `4/3` 已接（见 `2026-08-08-pixijs-battle-item-medicine-flask.md`）
- 战内 `[自动]`：客户端开关（不发 `4/11`），与挂机分离，见 `2026-08-08-pixijs-battle-auto-vs-hangup.md`
- 多怪站位与回合进度条未逐像素对齐截图
