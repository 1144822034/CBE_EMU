# 2026-08-05 商城退出过夜调测

Status: **code ready / E2E blocked (MySQL down)**

## 已落地（post-test19，已 `make -j2`）

根因证据链（用户复现 + debug-25c74d）：

| 现象 | 证据 | 结论 |
| --- | --- | --- |
| 退出闪退 SCR NULL+0x10 | post-test16 actor resp=300；post-test17 full+actor resp=1342 | **已开店时再投 `1/1/14` actor → SCR** |
| vitals 抢 open14 | post-test17 `shop_open14_skip_vitals_steal` | 已排除；仍 SCR → 不是抢包 |
| 进店成功 | combo 四族无 actor，`load=0` | 四族可清 loading |

实现：

1. **shell 已激活 open14**：只回 `14/14+14/4+14/5+14/6`（无 actor），并写 `cbe_mmshop_loading_clear.flag`
2. **右软退出**：武装 `g_mmshopExitPopArmed`；loading_clear 后 **只 pop mmShop**（保留户外，不走 entry-root 重登）
3. **服务端**读 `cbe_mmshop_exit_pop.flag` → `finish_shop_return_rehydrate`（**无 kind2 / coolmallOpenWaitMap**）
4. 保留：禁止 vitals 抢商城 open14；mmShop 期间 hold vitals

## 过夜自动跑

- 客户端 autotest：`--server=江湖归来` 编码乱码；改用本地后 **MySQL 连不上**，`jh-online-server` 立即退出：
  `mysql_authority_prepare ... Could not connect to MySQL server`
- 因此未能在无人值守下走到商城进退。

## 早起请你验证（约 1 分钟）

1. 启动 MySQL，再启 `jh-online-server` + 客户端（`bin/` 已编好）
2. 登录 → 江湖商城 → 右软/返回
3. 期望：不闪退、回到地图、不卡「正在和服务器通信」
4. 日志 `debug-25c74d.log` 应有：
   - `shop_open14_shell_family4_no_actor`
   - `mmshop_exit_pop_only` / `mmshop_exit_pop_armed`
   - `mmshop_exit_pop_rehydrate`

脚本：`tmp/run_shop_exit_autotest.ps1`（需 MySQL + 改 `--mock-service=127.0.0.1:19090`）
