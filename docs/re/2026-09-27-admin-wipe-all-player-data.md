# 管理后台全服清档

## 背景

开服测试或正式服重置时需要清空全部玩家进度，同时保留怪物/任务/NPC/商城/战斗公式等服务器配置。此前后台仅有按角色「清除跑环」等局部操作，没有全服清档入口。

## 入口

- Tab：`服务器维护`
- Action：`POST .../action`，`action=wipe-all-player-data`
- 确认：表单字段 `confirm` 必须等于 `清档`
- 前置：必须先开启维护模式（禁止新登录）

## 行为

1. 设置玩家数据写回阻断（`vm_mock_player_data_writes_are_blocked`），避免延迟 persist 把脏内存写回 MySQL。
2. 踢出残留在线连接且**不 flush**。
3. 释放全部 `account_state` 堆缓存，清空进程内 role DB 全局。
4. `SET FOREIGN_KEY_CHECKS=0` 后 `DELETE` 玩家相关表，再恢复外键检查。
5. `role_id_sequence` 清空后 `AUTO_INCREMENT=10001`。
6. 解除写回阻断。

## 清除范围（玩家相关）

账号/角色及附属：`accounts`、`account_role_*`（含 `account_role_monster_reward_cooldowns`、`account_role_offline_exp`、`account_role_practise`、`account_role_battle_entry_state`、`account_role_rapid_battle_entry_audit`、`account_role_training_books` 等）、`account_cultivation_books`、`wcoin_recharge_orders`、`role_id_sequence`

社交与组织：`friendships`、`enmity_kills`、`guilds`、`guild_members`、`guild_applications`、`world_chat_messages`

玩法进度：`server_arena_*`（sessions/scores/titles/wager）、`server_ladder_progress`/`_daily`、`server_guess_bets`/`_rounds`、`server_auction_listings`/`_bids`/`_claims`、`server_fruit_session`、`server_instance_enter_daily`、`server_registration_daily`

可选表（不存在则跳过）：`account_role_enhance_crystal_spent`、`account_role_tianxia_worship_daily`、`account_cultivation_books`、`account_role_state_payload_backup`、`server_fruit_session`、`server_instance_enter_daily`

## 保留范围（配置/内容）

`server_admin_config`、`server_payment_config`、各类 `server_*_config`、怪物/任务/NPC/商城/目录/宝箱/强化池、登录服与网关区划、维护配置本身等。

## 验证

1. 开启维护 → 输入「清档」→ 页面顶部绿色提示条显示成功文案。
2. 账号管理列表为空；`role_id_sequence` 下一角色从 10001 起。
3. 关闭维护后可重新注册/创角，配置 Tab 中的怪物与任务仍在。
4. 未开维护时点「执行清档」应弹出「请先开启维护模式」；确认字错误应 alert / 红色提示条，且不改库。

## UX 说明（2026-09-28）

原先未开维护时按钮带 `disabled`，样式与可点按钮几乎相同，点击完全无反馈，表现为「没反应」。现改为按钮始终可点，在 `onsubmit` 与服务端双重校验；结果用醒目 notice 条展示。

## 风险

- 不可恢复；生产环境务必先备份 MySQL。
- 清档瞬间若有未进写回阻断的 in-flight persist，理论上可能短暂回写，随后仍被 DELETE 清掉；维护 + 写回阻断用于压缩该窗口。
