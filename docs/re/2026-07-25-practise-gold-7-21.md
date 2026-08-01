# 修炼面板 7/18 / 7/19 / 7/21

Date: 2026-07-25

Status: implemented

## 1. 当前卡点

- 可见现象：修炼相关操作后服务端 `unhandled wt=7/21 len=23`，`source=ignored-unhandled-server-only`。
- 触发方式：打开修炼信息面板后切换黄金修炼（`opengold`）。
- 本轮最小目标：按客户端 parser 契约应答 `7/21`，并补齐同面板的 `7/19` help。

## 2. 运行时证据

- `unhandled wt=7/21 len=23 objects=1 first=1/7/21:14`
- payload 14 字节精确匹配单字段 `opengold` tagged-u8：`1+8+2+3=14`。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `0x0102CBE0` | 修炼网络响应分发 | `kind==7` 后分支 `subtype 0x12/0x13/0x15` |
| `江湖OL.CBE` | `0x0102CC1A` | subtype 18 | 读 `todaypasthour..isgold` |
| `江湖OL.CBE` | `0x0102CC6C` | subtype 19 | 读 `helpinfo` 字符串并弹窗 |
| `江湖OL.CBE` | `0x0102CBFE` | subtype 21 | 读 `result` u8，写入 isgold 槽，提示「已设置!」 |
| `江湖OL.CBE` | `0x0102C4E0` | 发送 7/19 | `Create(7,19)` + `type=0` |
| `江湖OL.CBE` | `0x0102C582` | 发送 7/21 | `Create(7,21)` + `opengold` |
| `江湖OL.CBE` | `0x010501E2` | help 正文 | 黄金修炼说明 GBK 串 |

## 4. 请求 / 响应契约

### 7/18（已有）

- Request: short WT `7/18`（len=9）
- Response: `1/7/18` 字段 `todaypasthour, todaypastmin, getexp, todaylasthour, todaylastmin, alllasthour, alllastmin, isgold`

### 7/19

- Request: `1/7/19`（builder 写 `type=0`）
- Response: `1/7/19 { helpinfo:string }`

### 7/21

- Request: `1/7/21 { opengold:u8 }`（0/1）
- Response: `1/7/21 { result:u8 }`；`result` 写入客户端 isgold，与请求 `opengold` 一致

## 5. 实现

- `builtin-practise-help19` / `builtin-practise-gold21`
- session 字段 `practiseIsGold` 供 7/18 `isgold` 回读；并写入
  `account_role_offline_practise.is_gold`
- 离线修炼银行与上线结算见 `2026-07-27-offline-practise-pill.md`

## 6. 验证清单

- [ ] 切换黄金修炼不再出现 `unhandled wt=7/21`
- [ ] 日志 `source=builtin-practise-gold21`，`opengold/result` 与点击一致
- [ ] 客户端提示「已设置!」且面板 isgold 状态更新
- [ ] 帮助入口应答 `builtin-practise-help19`
