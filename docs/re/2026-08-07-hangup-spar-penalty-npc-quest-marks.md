# 挂机按钮 / 切磋惩罚提示 / NPC 任务头顶图标

Date: 2026-08-07

## 1. 挂机按钮误点

- **原因**：`hangHit` 宽 100px（x=70..170），远大于 UI7 按钮，聊天区易误触。
- **修复**：按钮锚到 `HANG_BTN_X=48`，热区缩到 52px。

## 2. 切磋失败惩罚提示

- **契约**：负方已扣当前等级进度经验 10% + 铜钱 5%。
- **修复**：会话记录 `sparDeathExpPenalty` / `sparDeathMoneyPenalty`；
  `4/7.fdata` 与延后 `1/3/3` 文案为
  `挑战失败 经验-N 金钱-N`。
- Pixi 结算板：切磋失败显示「切磋失败」及带符号的经验/金钱变化。

## 3. NPC 头顶任务图标

- **CBE**：`scene_refresh_interact_prompt_types` 写 `node+326`：
  - `1` 黄问号（可提交）
  - `2` 黄感叹号（可接）
  - `3` 灰问号（未完成）
- **资源**：`fuhao.actor` + `fuhao.gif` 32×16。CBE 用 actor 矩形裁切，**不是**均匀 8×16：
  - rect0 `0..10` 灰?；rect1 `10..20` 黄?；rect2 `20..26` 黄!；rect3 `26..32` 灰!。
  - anim0/1/2 → prompt 类型 1/2/3（黄? / 黄! / 灰?）。
- Pixi：解析场景/轮询中的 `6/1`+`6/14`，按 NPC 名匹配后经 `fuhao.actor` 合成显示于名牌上方。
- **错乱根因（2026-08-08）**：曾按 8×16 均分 + `flipX` 裁切，黄! 会切到黄? 右半 + 感叹号左半，呈左右拼合碎片。权威边界以 `fuhao.actor` 为准。
- **提交后黄? 不消（2026-08-08）**：CBE `6/4` 成功后会移除活动任务并 `scene_refresh_interact_prompt_types`；Pixi 只弹聊天未更新 `acceptedTasks`/`npcTaskPrompts`。现提交成功立即本地剔除该 taskId 并重拉 `6/1+6/14`；接取同样 refresh。
- **小猴子接近提示**：`e_monkey` 等挑战 NPC 不得再被 `isMonsterLike(/^e_/)` 排除出 `liveInteractNpcs`；SCE 野怪仍走碰撞开战。
- **双图标压名（2026-08-09）**：`upsert` 与 `setTaskPrompts` 并发 `refreshQuestMark`，await 后各自 `addChild`，未跟踪的旧精灵停在立绘加载前的名牌高度，立绘升高后正好挡住 NPC 名；跟踪中的那枚仍在名字上方。现用 `questMarkGen` 丢弃过期挂载，并 `purgeOrphanQuestMarks`。
