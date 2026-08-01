# 开战卡在「获取数据」：战斗包已组好但 body 发送失败

```text
phase: scene move -> 4/1 challenge battle start
trigger: 01桃花岛_02.sce 遇怪开战（空 NPC 图冷会话更易复现）
server: mock_challenge_battle_start ... objects=2 resp=185
        response_send_failed stage=body process_ms=5023
client: server request failed target=127.0.0.1:19090 kind=data
```

## 已确认的链路

1. 移动 `2/1` 正常，`process_ms` 个位数。
2. 开战请求进入 `builtin-challenge-interaction`，开战响应 **已组出**（`resp=185`）。
3. 同请求内先后出现：
   - `mock_shop_catalog ... source=item.dsh/equip.dsh`
   - `shop_item_db_load failed error=Malformed MySQL result-set header`
   - `monster_db_load failed error=MySQL socket receive failed`
4. `process_ms=5023`，紧贴客户端游戏请求窗口 `5000ms` 与旧 MySQL `SO_RCVTIMEO=5000`。
5. 客户端先关 socket → 服务端 `response_send_failed stage=body` → `dropped malformed request`。

首个偏离不是开战包字段错误，而是：**开战热路径上冷加载商店/怪物 MySQL 覆盖表，失败后连接被污染并卡住，拖垮整次请求时限**。

## 根因

| 环节 | 契约 / 行为 | 证据 |
|------|-------------|------|
| `vm_net_mock_monster_stats_for_enemy` | 开战需要怪物 HP/攻防；覆盖表来自 MySQL，失败应回落内置 base stats | `mock_server_role.c` |
| 冷加载 | 首次开战才 `monster_db_load`，并间接 `load_shop_catalog`→`shop_admin_db_load` | 开战前紧邻的 catalog 日志 |
| `query_failed` | 原先 **不关闭** 持久连接，半包残留污染后续 COM_QUERY | `mysql-client.c` 旧 `query_failed` |
| 超时耦合 | MySQL 收包超时 5s ≈ 游戏请求超时 5s | `SO_RCVTIMEO` + `VM_MOCK_SERVICE_SOCKET_TIMEOUT_MS` |
| drop 回调 | `monster_db_drop_row` 曾调用 `find_shop_catalog_item`，可在结果集未排空时嵌套 MySQL | 同连接协议错乱的另一诱因 |

与上一轮「空 NPC 漏 27/11」无关：切图已通，本问题是开战请求的 I/O 时序。

## 修复

1. `query_failed` 与 `connection_failed` 一样 `vm_mysql_close()`，避免毒连接。
2. MySQL socket 默认超时改为 `1000ms`（`CBE_MYSQL_TIMEOUT_MS`，钳制 200–5000），失败快退。
3. 服务启动在接客前 `battle_catalog_warmup`：预加载 shop catalog + monster db。
4. drop 行校验改为 `shop_catalog_has_loaded_item`；`monster_db_load` 在打开 drops 结果集前先物化 shop catalog，禁止嵌套查询。

## 验证

- [x] `make -j2`
- [ ] 重启服务后日志有 `battle_catalog_warmup shop_items=... monster_db=ok|base-stats-fallback`
- [ ] 遇怪开战：`mock_challenge_battle_start` 后出现正常 `account=... source=builtin-challenge-interaction process_ms`（应远小于 5000），无 `response_send_failed`
- [ ] 客户端进入战斗 UI，不再停在「获取数据」
