# 区服网关目录（物理分区，2026-09-10）

## 结论

物理多区靠 **不同 WS 网关 URL** 区分。标题 `WT 1/1/12` /
`server_login_servers` 仍只是逻辑显示名，不换连接地址。

## 契约

```
MySQL server_gateway_zones
       │（后台 ?tab=gateways 保存）
       ▼
GET /api/zones（公开 JSON，gateway 代理）
       │  始终从 FIXED_LIVE_GATEWAY 派生目录 URL
       ▼
Pixi 选区 → localStorage pix_gateway + pix_zone_id
       ▼
LiveTransport 连接所选 ws_url
```

公开响应：

```json
{
  "ok": true,
  "defaultZoneId": 1,
  "zones": [
    {
      "zoneId": 1,
      "name": "江湖归来",
      "label": "推荐",
      "wsUrl": "ws://host:19092",
      "accountWebUrl": ""
    }
  ]
}
```

## 修改点

- `src/server/mock_server_gateway_zones.inc.c`：目录加载/保存/删除
- `src/web_admin_gateways.inc.c` + `web_admin_server.c`：后台 tab、动作、`/api/zones`
- `server/mysql/migrate_add_gateway_zones.sql` / `schema.sql`
- `PixiJS_web/scripts/ws-gateway.mjs`：代理 `/api/zones`
- `PixiJS_web/src/net/zones.ts`、`Transport.ts`、`TitleScreen.ts`、`main.ts`

## 回归

1. 停服后执行 migrate（或依赖启动 `CREATE TABLE IF NOT EXISTS`），`make -j2`，重启 mock + gateway。
2. 后台 `?tab=gateways` 新增第二区不同 `ws_url`，设启用。
3. Pixi 冷启动：多区且无 `pix_zone_id` 时，登录前进入「选择区服网关」；选定后写入网关再登录。
4. 单区：静默选用，splash 不强制选区。
5. 目录拉取失败：回退 `FIXED_LIVE_GATEWAY`，可正常尝试登录。
6. 标题「服务器列表」逻辑区服行为不变。
