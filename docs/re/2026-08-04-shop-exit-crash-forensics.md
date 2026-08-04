# 2026-08-04 商城退出卡住

## 证据（22:27 `shop_open14_trace.log`）

```text
already_open trailing=1 ack=1 mark_src=combo resp=79
```

江湖商城先走 scene-combo（`mark_src=combo`），退出时的**第一次** open14
被当成 trailing，只回 status+money，**未**写 `cbe_mmshop_loading_clear.flag`
→ 主机清条未跑 → 「正在和服务器通信」。

## 契约（当前）

| 时机 | 响应 |
| --- | --- |
| 首次 open / scene-combo | family-4 目录 |
| **任何** already_open（退出 / 尾随） | status+money，**无** actor |
| 同时 | 写 `cbe_mmshop_loading_clear.flag=1` |

主机（net dispatch 前后泵）：清 `*(r9+0x2868)+0x10` + download-busy
（21808/21804/0x9584）。

**禁止** already_open 附 `1/1/14`（SCR 闪退）。

## 验证

1. 重启 server+client；进商城 → 退出。
2. `shop_open14_trace.log`：`host_clear=1`。
3. `client_out`：`mmshop_loading_clear reason=flag`。
4. 不再卡通信条。
