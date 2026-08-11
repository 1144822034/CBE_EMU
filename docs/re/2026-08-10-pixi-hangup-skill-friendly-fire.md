# Pixi 挂机技能误打自己/队友

日期：2026-08-10（结：改客户端碰怪环）

## 结论

挂机改为与手动遇怪同一 `4/1` 路径（见 `2026-08-10-pixi-hangup-client-collide.md`），不再走服务端 subtype-10 / hangup synth，播报侧复杂 wire remap 已撤掉。

旧根因（subtype-10 + 场景线槽 `[2,3,0]`）在新契约下不应再出现。
