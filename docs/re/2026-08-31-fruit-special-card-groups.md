# 水果机特殊牌型（2026-08-31）

## 契约

服务端 `mock_server_fruit.c` 开奖返回：

- `cardGroupType`：牌型代码
- `cardGroup`：中奖格下标 CSV（`0-23`）
- `triggerPos`：跑马灯停格
- `win`：各中奖格押注合计

## 牌型

| 代码 | 中文 | 规则摘要 |
|------|------|----------|
| SINGLE | 单奖 | 停格单格结算 |
| LUCK | 幸运 | 停幸运格且未送灯 |
| BONUS | 奖励 | 单奖后再乘 bonus |
| XSY | 小三元 | 停大铃/芒/橙，亮三格 |
| DSY | 大三元 | 停大瓜/77/星，亮三格 |
| DSX | 大四喜 | 停大苹果，亮四苹果 |
| ZHSH | 纵横四海 | 亮一边七格 |
| XNSH | 仙女散花 | 幸运后随机约 5 格 |
| CONTINUITY | 送灯 | 幸运后随机 1–3 格 |
| HC | 开火车 | 连续 4–6 格 |
| TLBB | 天龙八部 | 随机 8 格 |
| JLBD | 九莲宝灯 | 随机 9 格 |
| DMG | 大满贯 | 全部非幸运格 |

客户端 `fruitSpecialFx.ts` 播送灯/交替换灯/顺序亮/开火车动画；提示用 `fruitCardGroupLabel`。

## 验证

1. 押注多格后连开，观察 tip 是否出现「小三元」等（概率事件，可临时抬高 `vm_mock_fruit_build_special` 权重）。
2. 特殊中奖后底部押注清零、可收分/比大小。
3. 中奖曲不叠加（`FruitSfx` stopWinJingle）。
