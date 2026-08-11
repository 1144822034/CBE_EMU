# 2026-08-07：切磋门闩 — 25 级 / 城镇

phase: spar-level-city-gates
status: fixed

## 契约

1. **等级**：发起方或目标任一方 `< 25` → 拒绝切磋（不可切磋、不可被切磋）
2. **城镇**：当前场景为城市键（与挂机禁止相同：`c`+数字 / 单数字+GBK 别名）→ 拒绝

`4/14 result`：

| result | 含义 |
|--------|------|
| 6 | 等级不足 25 |
| 7 | 城镇中无法切磋 |
| 8 | 对方替身人偶抵挡（不进战） |
| 9 | 发起方冷却中（5 分钟） |

人偶与冷却细节见 `docs/re/2026-08-11-spar-substitute-doll-and-initiate-cooldown.md`。

## 修改

| 层 | 改动 |
|----|------|
| `mock_server_guild.c` | `build_nearby_spar_request_response` 先判城再判级再开战 |
| `protocolSocial.parseSparAck` | tip：等级 / 城镇 |
| Pixi `main` | 本机预检城图与自身等级（对端等级仍以服务端为准） |

## 验证

1. 临安城区对附近玩家切磋 → tip「城镇中无法切磋」，不进战
2. 野外、任一方 `<25` → tip「等级不足25级无法切磋」
3. 双方 `≥25` 且野外 → 仍可切磋
