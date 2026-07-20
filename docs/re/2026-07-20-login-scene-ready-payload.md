# 登录场景 NPC 与系统消息同步完成

## 问题

角色登录后地图先显示，约数秒后才出现 NPC 和系统欢迎消息。服务日志表明：

- NPC `27/11` 已经位于客户端 `scene_runtime_init_and_sync(0x01012FB4)` 发出的首次场景资源/任务跟进响应中，但该响应的服务端构建耗时约 5.7 秒。
- 欢迎消息原先只进入场景轮询队列，还要再等待下一次 poll。

## 协议调整

- 首次场景资源/任务跟进在客户端地图、Actor、HUD 和聊天管理器均已初始化的阶段下发。
- 在这个响应中先附加 `27/11 npcinfo`，再通过客户端聊天处理路径
  `0x010126C6` 附加 `1/3/3 type=5` 系统消息。
- 会话在响应完成前切换为 scene-ready，因此欢迎消息无需等待下一次场景轮询；仍保持一次性语义。

## 延迟根因与优化

Windows 独立 mock service 原先把 `stdout` 设置为无缓冲。首次场景响应会记录二十多条协议证据，每条日志都同步写入重定向文件，日志 I/O 本身占用了数秒，而且发生在响应发送之前。

现在：

- 仅 `--mock-service-only` 使用批量 stdout 缓冲；模拟器客户端继续无缓冲，崩溃日志行为不变。
- 响应正文发送并释放协议锁后统一 `fflush(stdout)`，日志仍能及时看到，但不再阻塞客户端响应。
- 登录场景请求内复用一次任务状态快照和 NPC 选择结果，避免同一响应重复查询 MySQL、重复扫描场景 NPC 目录。
- 启动场景坐标未变化时不再重写整套角色、装备和背包关系表。
- TCP MySQL 连接启用 `TCP_NODELAY`，避免小型协议包被 Nagle 合并等待。

## 回归

快速登录回归：

```text
login scene ready regression passed elapsed_ms=27 response_len=614
```

服务端关键路径：

```text
scene_resource_followup_timing ... total_ms=3 lifecycle_ms=2 objects_ms=0
account=guest00023 request=39 response=614 ... state_hold_ms=3 process_ms=3
```

完整 NPC 生命周期回归通过：

```text
scene npc lifecycle regression passed startup_len=614 shop_return_len=500 steady_refresh_len=221 relogin_preinit_poll_len=0 relogin_startup_len=614
```

覆盖首次登录、商城返回重播、稳定刷新不重播、返回标题后同连接重登；首次登录与重登响应均断言同时包含非空 `27/11` NPC 列表和系统消息。验证日志为：

`tmp/mock-service-19090.login-scene-final.20260720-161336.stdout.log`，对应 stderr 为 0 字节。
