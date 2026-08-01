# 客户端物理端点列表（2026-07-26）

## 为何不做标题选服绑 IP

进服前的版本/更新握手（`WT 18/*`）已经需要一条可用的 TCP 端点。标题
`WT 1/1/12` 的 `serverinfo` 又不携带 host/port，因此不能把“物理连哪台机”
推迟到标题选区之后。

结论仍遵循
[2026-07-24-title-endpoint-disconnect-reconnect-protocol.md](2026-07-24-title-endpoint-disconnect-reconnect-protocol.md)：
IP/端口由宿主启动配置拥有，不写入原生标题协议。

## 方案

宿主读取本地 `servers.conf`，在版本握手前选定物理端点。

- **交互选择**：列表 ≥2 项、stdin 是控制台、且未指定 `--server=` /
  `CBE_SERVER=` / 显式 host:port 时，启动时在 stderr 打印菜单，输入序号回车。
  直接回车使用 `current=`（或第一项）作为默认。
  Game Center 模式下改为窗口选区：选中江湖 OL 后弹出区服 UI，确认后把名称
  写入 CBMS meta；服务端 `WT 1/1/12` 只下发该项，标题内服务器列表与启动器一致。
- **跳过菜单**：`--server=NAME`、`--no-server-select`、`CBE_NO_SERVER_SELECT=1`、
  autotest、或非交互 stdin。
- **无弹窗 SDL UI**：物理选服走 Game Center 区服层；控制台菜单仅 `--no-launcher`。

优先级（高到低）：

1. 显式端点：`--mock-service=host:port`、`CBE_SERVER_ENDPOINT`、
   `CBE_MOCK_SERVICE` 等既有参数
2. 命名项：`--server=NAME` 或 `CBE_SERVER=NAME`
3. 交互菜单（满足条件时）
4. 文件内 `current=NAME`
5. 内置默认 `127.0.0.1:19090`

文件搜索顺序：`--server-list=` / `CBE_SERVER_LIST=`，否则
`servers.conf`、`bin/servers.conf`、`JHOnlineData/servers.conf`。

## 配置格式

```text
current=local
local=127.0.0.1:19090
jianghu-return=203.0.113.10:19090,https://jh.cbhub.top
```

`#` / `;` 行注释。名称与值以第一个 `=` 分隔。值格式为
`host:port[,account_web_url]`：可选的 `,http(s)://...` 只给 Android
注册/管理账号按钮用，宿主连游戏时会剥掉再解析 `host:port`。最多 16 项。

标题后台的「测试一区 / 江湖归来」仍只是逻辑分区显示名，与本文件无强制同名要求。

## Android

`MainActivity` 在资源解压完成后弹出与 Game Center 同源的区服列表
（读 `/sdcard/JHOnline/servers.conf`）。选定后：

1. `Os.setenv(CBE_SERVER, 名称)` 让 native 走同一物理端点
2. 账号网页 URL 取该项的 `account_web_url`，缺省则回退
   `https://jh.cbhub.top`

## 修改点

- `src/main.c`：`vm_client_server_list_*` 与 `vm_mock_service_init_config` 接入
- `bin/servers.conf`：示例列表
- `server/README.md`：客户端启动说明

## 验证

- `make -j2` 通过
- 启动日志出现 `server_list selected name=... endpoint=host:port` 或
  `source=server-list` / `source=endpoint` / `source=default`
