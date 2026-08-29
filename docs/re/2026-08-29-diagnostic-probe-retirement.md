# 已完成调查的诊断探针退役（2026-08-29）

## 范围

本次仅删除已经完成调查、没有当前自动化断言或产品平台语义依赖的宿主侧只读取证。没有
改写 CBE/CBM、客户机内存、寄存器、PC/LR、输入队列、网络响应、callback 或 screen
生命周期。

## 已删除

- 世界地图的逐帧延迟、图片 API 耗时、native LCD 路径、render block 和 scene-tick 快照，
  以及 player-1/player-3 启动器中的相应环境变量；世界地图根因和修复已记录在
  `2026-07-18-world-map-read-lag.md`。
- `CBE_TRACE_LEGACY_RUNTIME_FORENSICS`。它没有任何当前启动器、自动化场景或回归脚本的
  调用方，却会重新为整个客户机地址空间注册逐指令/读写内存 callback。完整范围现在只在
  已启用的仓库自动化或 GDB 调试下使用。
- `CBE_TRACE_SCENE_BATTLE_CONTROL_STATE` 的 64 次 Global_R9+23682 写入快照。它只由旧的
  player-3 启动器设置，未被自动化脚本或业务状态机读取。
- `CBE_TRACE_SCENE_ASSET_LIFECYCLE` 及其环境变量、记录上限和 player-3 默认启用项。它同样
  没有自动化调用方，且只记录资产表的历史调查数据。

## 保留边界

`CBE_TRACE_SCE_ENTITY_CALLBACK` 和 `CBE_TRACE_SCENE_BATTLE_COLLISION` 暂不退役：
`run-linan-scene-battle-direct-enter-test.ps1` 与
`run-linan-scene-battle-trace.ps1` 仍以它们的日志验证真实客户端场景战斗路径。保留的
自动化观察仍只在已声明的场景运行时开启，输入和协议继续走客户端的正常路径。

## 验证

- `rg` 已确认源代码、启动器和脚本不再引用已删除的环境变量或探针符号。
- `make -j2` 通过。
- `bin\\world-map-lcd-native-blit-regression.exe` 通过。
