# 60+ 人在线：组队邀请很慢 / 解包错误

日期：2026-08-04

## 现象

服务器人数多（约 60+）时：

1. 发起组队后对方很久才收到邀请；
2. 或客户端弹出「解包错误」。

## 契约

`江湖OL.CBE:0x01012E4C` → `event_packet_init(packet, 10, 19)`；  
`event_packet_parse_WT(0x0103467A)` 在 **对象数 > 10** 时返回错误 3 → UI「解包错误」。  
已有取证：`docs/re/2026-07-20-new-role-login-object-cap.md`。

组队邀请投递：`5/2` + 本机还原 `1/1/14` = **2 个对象**，挂在目标客户端的 scene-sync poll 上。

## 首个偏离

人多时单次 scene-sync 常见构成：

| 内容 | 对象数 |
|------|--------|
| nearby baseline（otherinfo + moveinfo×≤3 + remove） | 约 2–5 |
| 世界/本地聊天（`CHAT_POLL_MAX=10`，按剩余额度填满到 10） | 可占满剩余 |
| 交易 | +1 |
| 组队邀请 `5/2`+`1/1/14`（**未参与额度预算**） | +2 → **冲破 10** |

两条后果：

1. **解包错误**：`objects≥11` 进客户端 parser。  
2. **邀请很慢**：poll 被 loading-clear / 战斗 / NPC / 附近人基线等提前 `return`；即便走到社交追加，额度已满则邀请继续挂起，等下一次有空位的 poll。60 人时协议锁竞争 + 聊天洪峰，空位更少。

## 修改

1. **优先投递**：`modal_social_invite_poll` —— 地图 shell 就绪后、nearby/chat 之前，单独投递模态邀请（队/友/交易/切磋）。  
2. **额度预留**：chat 预算扣除邀请预留槽；baseline/delta 在额度内先投模态邀请，再聊天；不足 10 才追加非模态社交。  
3. 超限则本轮跳过社交（通知仍排队），避免写坏包。

## 续记：3 人仍要 2–4 秒（2026-08-04）

运行时（本机 3 端）：

- `VM_CLIENT_POLL_MIN_INTERVAL_MS = 2000`：客户端 scene-poll **最少隔 2s** 才拉一次。
- 邀请只能挂在对方的 poll 上投递 → 理论下限 ≈0–2s，再叠一层就到 2–4s。
- `trade-invite` / `team-hsp` 日志：`age_ticks=8` 且 HSP 优先于邀请；旧 picker 先抽 HSP，
  模态早投路径若抽到 HSP 还曾 `return 0` 丢掉已编码通知。

修改：

1. 社交选择：LEAVE / JOIN / RESULT → **模态邀请** → HSP 等其它 bypass。
2. 模态早投：非邀请通知改为正常下发（不再 discard）；邀请不再被 HSP 饿死。
3. 客户端 poll 下限 **2000→500ms**（`network-client.c`）。

验证：两端需同时换新 `main.exe` + `jh-online-server.exe`；邀请日志
`age_ticks` 应接近 0–1，体感应在约半秒内。
