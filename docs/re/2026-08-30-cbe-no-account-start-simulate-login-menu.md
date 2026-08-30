# 2026-08-30 无登录记录「开始游戏」→ 提示先登录

## 目标

Android CBE：无登录记录点「开始游戏」时，提醒玩家先走「登录游戏」，并立刻清掉联网进度条。

## 行为

1. 拦截空凭据 `WT 1/1/12`，不转发
2. 本地投递空 WT（`len=5 objectCount=0`，event 7）→ 标题 net callback 跑完，进度条结束
3. Java 弹窗：`请先登录游戏`

有保存凭据的「开始游戏」仍正常 WT 登录。

## 修改点

- `network-client.c`：`vm_client_deliver_local_empty_wt` + `need_login tip`
- `MainActivity.pollNeedLoginTip`

## 验证

清除登录记录后点「开始游戏」→ 进度条应消失，弹窗「请先登录游戏」；日志 `empty_wt_ack` / `need_login tip`。
