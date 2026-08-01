# codeVersion 登录强制升级门闩

## 目标

服务端比对客户端 `codeVersion`；不匹配时登录失败并提示「请更新客户端」。

## 协议证据

- 启动握手 WT `18/9` 字段含 `version` / `codeVersion`（`JianghuOL.CBE` 字面量；`docs/re/startup-version-to-map.md`）
- 登录失败契约：`result='2'` + `information`（`vm_net_mock_build_login_failure_response`；标题模块解析失败信息）

## 行为

1. WT `18/9`（及带 `version` 的更新握手）解析 `codeVersion`，写入该 `clientId` 会话
2. 登录（含无账号 `1/12`）前比对：
   - **期望值 = 0**：不拦截（兼容旧部署）
   - **期望值 ≠ 0** 且客户端缺失或不等：`result=2`，`information`=GBK「请更新客户端」

## 配置期望值

优先级：

1. 环境变量 `CBE_CODE_VERSION=<u32>`
2. 文件 `JHOnlineData/server_code_version.txt`（单行数字；可用 `CBE_RESOURCE_ROOT` 下同名文件）

## 客户端自配（不改 CBE）

宿主在发包前覆盖 WT 包里的 `codeVersion`（`network-client.c`）：

1. 环境变量 `CBE_CLIENT_CODE_VERSION=<u32>`
2. 游戏根目录 `client_code_version.txt`（Android：`/sdcard/JHOnline/client_code_version.txt`）
3. `JHOnlineData/client_code_version.txt`

未配置则沿用 CBE 原值。日志：`client_code_version_override value=N`。

推荐：根目录文件（APK 升级强制刷新 `JHOnlineData` 时不会被冲掉）。

### Android 解压缺口（2026-07-31）

`assets/client_code_version.txt` 曾未列入 `REQUIRED_ASSET_ROOTS` /
`VERSION_UPGRADE_FORCE_ROOTS`，设备 `/sdcard/JHOnline/` 下无此文件，宿主读到
0，服务端期望值≠0 时登录 `result=2`。标题模块对 `result=2` 走固定「用户名/密码」
失败文案，`information`（「请更新客户端」）不会显示，表现为「用户名密码不对」。

已将 `client_code_version.txt` 加入解压与升级强制刷新，并同步
`assets/JHOnlineData/client_code_version.txt` 作兜底。

## Android「开始游戏」与账号中心

- **无账号** `1/1/12`：Android 直接打开账号中心网页（`:19091/user/login`），
  **不做** codeVersion 探测。网页登录/注册本身也不走游戏门闩。
- **已保存账号**点「开始游戏」：仍发 WT 登录，服务端门闩照常生效。

## 运维步骤

1. 客户端：`/sdcard/JHOnline/client_code_version.txt`（或 assets 解压出的同名文件）
2. 服务端：`web/fs/JHOnlineData/server_code_version.txt` 或 `CBE_CODE_VERSION`（与客户端同值才放行）
3. 两端不一致：登录 /「开始游戏」应失败并提示更新
4. 日志：`client_code_version_override`、`mock_login_code_version_reject`（游戏登录）

## 修改点

- `mock_server_equipment_npc.c`：会话字段 + parse/expected/note/check
- `mock_server_dispatch.c`：版本握手时 note
- `mock_server_interaction_login.c` / `mock_server_transport.c`：登录前 check
- `src/network-client.c` + Android `cbeEmu/network-client.c`：宿主覆盖

## 验证

1. `make -j2`（客户端）与 `make -j2 server`
2. 不配期望值：登录正常
3. 两端都配成同一 N：可登录
4. 仅服务端改大：登录失败并提示更新
