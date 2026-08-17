# 商城返回后背包物品为空

Date: 2026-08-17

Status: protocol fixes implemented; end-to-end runtime validation pending

## 1. 当前卡点

- 可见现象：从商城返回后，背包界面显示为空或无法恢复原有物品。
- 触发方式：登录后进入背包，打开商城，关闭商城并回到背包/强化相关界面。
- 本轮最小目标：确认商城关闭后客户端是否重新创建了主物品管理器，以及该生命周期是否需要再次接收 `30/21`。

## 2. 运行时证据

- `bin/server_out.txt`：登录期收到 `5/10`，服务端在同一响应中发送 `30/21`，日志记录 `gridnum=48 stored_rows=48`；随后独立的 `17/1` 背包对象也记录 `rows=48`。
- 同一会话商城关闭后再次收到 `5/10`，响应长度约 234 字节；服务端日志没有记录新的 `30/21`、`17/1` 或 `7/42` 请求。
- `shop-return-input-v2.log` 的商城返回前后 mmGame 输入管理器地址由 `0x0105a158` 变为
  `0x01056ad8`；返回后的 `pending=60/gate=56` 与旧管理器不同，证明返回流程进入了新的
  mmGame bootstrap。该日志不是主物品管理器 `+32/+36/+40` 的直接读数，主物品管理器是否
  重建仍由客户端回归确认。
- 商城关闭前后的 `1/1/14` 请求已确认是 `ActorInfo-only`，不会触发场景重入，也没有证据表明它应重置背包网格种子。
- `shop-return-input-v2.log` 记录返回后的 `mmGame` logic 仍以主 CBE `R9=01050BD0` 进入；此前按动态代码地址推导独立 `R9` 的尝试导致崩溃，已撤回。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `HandleItemGridResponse(0x01039952)` | `30/21` 的主物品网格构造入口 | 读取 `result`、`gridnum`、`iteminfo`，先释放旧列表，再按响应数量分配并写入主物品管理器。 |
| `mmGameMstarWqvga.cbm` | `sub_418C(0x418C)` | `17/1` 背包完整列表解析 | 读取计数型 `iteminfo`，用于商城/背包列表显示；不是 `30/21` 网格种子。 |
| `mmGameMstarWqvga.cbm` | `sub_0(0x0000)` | 动态模块静态基址 ABI | 模块代码通过 `R9` 访问共享 CBE 数据；不能从重定位后的代码地址推导独立静态基址。 |
| `江湖OL.CBE` | `HandleSceneTransition(0x0100369C)` | 商城关闭后的场景/资源生命周期 | 对支付 CBM 清理后恢复主场景；是否重建物品管理器需运行时确认。 |

## 4. 调用链 / 业务流程

1. 登录/选角阶段服务端在 `5/10` 响应中返回 `30/21`，`HandleItemGridResponse` 构造主物品网格。
2. 背包/商城模块分别解析 `17/1` 与商城目录对象；`1/1/14` 只解析角色状态。
3. 关闭商城后客户端执行支付/场景清理并恢复 `mmGame` logic；当前服务端会抑制重复的登录期 `30/21`。
4. 若客户端在第 3 步重新创建主物品管理器，则缺少新的 `30/21` 会使新管理器为空；若没有重建，则问题应在客户端列表显示/回调状态，而不是网格协议。

## 5. 结构体 / 状态字段笔记

- owner: `江湖OL.CBE` 主物品管理器（`Global_R9 + 24640`，由现有取证宏使用）。
- offset / field: 管理器 `+32` 列表指针、`+36` 当前数量、`+40` 容量；`HandleItemGridResponse` 先清理列表，再按 `gridnum` 分配。
- read site: `HandleItemGridResponse(0x01039952)`；现有 `vm_autotest_note_backpack_parser_pc` 可读取这些字段，但只覆盖旧的固定动态地址。
- write site: CBE 自身的 `HandleItemGridResponse` 和 `InitTimerControl`；宿主不得直接写这些字段。
- current meaning: 已确认登录期有效；商城返回后的管理器身份/数量尚未确认。

## 6. 请求 / 响应契约

### Request

- WT: `5/10`（组信息），可携带客户端后续同步对象。
- 相关对象：登录期响应包含 `30/21`；商城返回后的当前日志只有组信息响应，未观察到新的背包刷新请求。

### Response

- `30/21`: `result`、`gridnum`、`iteminfo`，由主 CBE 构造 live grid。
- `17/1`: `iteminfo` 背包列表，供 `mmGame:0x418C` 显示/刷新。
- `7/42`: 书籍/背包打开附带对象；不能在未观察到请求时盲目追加。

## 7. 成功路径与失败路径

### Success path

- 管理器未重建：返回后继续使用登录期 `30/21` 的 48 行，关闭商城不产生额外背包响应。
- 管理器重建：客户端自然发出或在对应 bootstrap `5/10` 中接收新的 `30/21`，随后列表数量非零。

### Failure path

- 当前日志显示返回后没有新的 `30/21`，但尚未证明客户端是否发生了管理器重建；因此不能把登录期种子抑制器直接改成商城返回时重置。

## 8. Negative Evidence

- 商城 `1/1/14` actor 查询已改为 `ActorInfo-only`；继续把它当场景重入或背包重建信号会制造错误生命周期。
- 按 `codeBase + 0x14000` 给 `mmGame` 设置独立 `R9` 曾导致不可访问地址崩溃，说明该推断不是通用 ABI。
- 仅增加 `30/21` 或 `17/1` 而没有对应客户端 parser/生命周期证据，可能重复插入物品、覆盖选择状态或破坏商城返回栈。

## 9. Unknowns / Hypotheses

- unknown: 商城关闭路径是否再次执行主物品管理器初始化。
  - current guess: unresolved。
  - why it matters: 只有管理器重建时才应重新发送 `30/21`。
  - next probe: 在 `HandleItemGridResponse`、`InitTimerControl` 和 mmGame 模块初始化入口记录 manager 指针、数量、容量及返回前后调用次数。
- unknown: 返回后背包显示为空是否只是 `mmGame` 输入 gate 未满足导致未触发 `17/1`。
  - current guess: plausible, but no packet evidence yet。
  - why it matters: 该分支应修复输入/生命周期，不应补发背包数据。
  - next probe: 对同一返回运行记录首个 `mmGame` logic gate 与后续真实请求。

## 10. 根因陈述与修复

当前根因判定：商城返回进入新的 mmGame bootstrap，而服务端按“角色已播种”去重，继续省略
`30/21`。若该 bootstrap 同时创建新的主物品管理器（需客户端 `+32/+36/+40` 读数最终确认），
就会出现新管理器列表为空；登录期的 `30/21` 不会自动迁移。

修复位于服务端生命周期边界：

- `1/1/14` 在此前已成功发送商城目录时只返回末尾 ActorInfo；此时按 `actorId` 设置一次性
  `g_netMockBackpackGridReseedPendingRoleId`。
- 后续同角色 `5/10 + 7/7(type=1)` 到达时，消费该标记、清零旧的
  `g_netMockBackpackGridSeededRoleId`，再按既有 builder 发送 `30/21`、必要的 `7/11` 和
  装备对象。
- 该标记随账号 capture/restore 保存，并在标题登录阶段清零；ActorInfo 查询本身不触发场景
  重入，也不直接写客户端状态。

## 11. 本轮实现计划

- 已完成业务改动：`src/server/mock_server_core.c`、`mock_server_equipment_npc.c`、
  `mock_server_interaction_login.c`、`mock_server_catalog.c`。
- 已更新 `scripts/shop-return-routing-regression.c`，验证 ActorInfo-only 查询会 arm 角色绑定的
  一次性网格重播标记、同角色 bootstrap 实际生成 `30/21`，且账号快照不会丢失该标记。
- `make -j2` 与协议回归已通过；完整端到端场景仍受隔离 MySQL 密码缺失限制。

## 12. 验证清单

- [x] 记录返回前后 mmGame 输入管理器地址并确认返回 bootstrap
- [x] 确认返回 bootstrap 缺少 `30/21` 是首个服务端契约偏离
- [x] 新增窄的一次性、按角色绑定的 `30/21` 重播边界
- [x] `make -j2` 通过，运行相关纯回归
- [ ] 使用隔离 MySQL 账号重放完整“背包 -> 商城 -> 返回背包”场景，并确认客户端物品管理器数量/列表非零

本轮验证记录：

- `make -j2`：通过（当前构建无待重编译目标）。
- `tmp/shop-return-routing-regression.exe`：通过；日志包含 `mock_backpack_grid_reseed role=1 reason=shop-return-bootstrap`，并实际生成 `30/21`、`gridnum=1`。
- 完整自动化未运行：`CBE_AUTOMATION_MYSQL_PASSWORD` 未配置，隔离 fixture 无法创建测试数据库。

## 13. 返回 bootstrap 的 NPC 生命周期回归

### 现象与首次偏离

- 在上述 `30/21` 网格重播生效后，用户回到场景时背包已恢复，但原场景 NPC 全部消失。
- `bin/server_out.txt` 的登录首屏包含
  `mock_scene_npc_seed phase=startup-scene-followup-immediate`，并实际发送
  `1/27/11`、`npcnum=3`。
- 商城返回的 `5/10 + 7/7(type=1)` 已重播 `30/21`，紧随其后的
  `scene-task-subset-followup` 只有任务/书籍对象，缺少新的非空 `27/11`。这是
  NPC 消失前的首次服务端契约偏离，不是渲染层故障。

### 根因

商城关闭会重新建立 mmGame bootstrap，客户端会丢弃旧场景的 NPC 节点；但服务端的
`g_vm_net_mock_scene_moveinfo_npc_seeded` 仍将同名场景视为“已播种”。此前新增的
背包重播标记仅在允许传统商城场景重入的 follow-up 中消费；当该 follow-up 同时带有
延迟场景完成状态时，标记未被消费，因而没有为新 bootstrap 重发 `27/11`。

### 修复

- `mock_server_catalog.c`：商城返回的同角色 `5/10` 重播 `30/21` 时，按当前会话和精确
  场景设置一次性 `bootstrap-only` NPC 重播标记。
- `mock_server_interaction_login.c`：匹配该标记的下一次场景资源/任务 follow-up 无条件消费
  `27/11` 生命周期目录，即使当前响应不允许传统商城场景重入；调用既有
  `vm_net_mock_mark_scene_moveinfo_npc_seed_pending()` 清除旧的一次性目录状态。
- `mock_server_equipment_npc.c`：区分传统商城返回（可走原 `30/2` 完成路径）和
  `bootstrap-only`（只允许 `27/11`，绝不触发场景重入）。

### 回归证据

- `scripts/shop-return-routing-regression.c` 新增 bootstrap NPC 协议回归：预先设置旧场景已
  播种状态，模拟商城返回重播标记，并调用真实场景生命周期 builder。
- 断言响应含非空 `1/27/11`（当前 c04 临安府场景为 3 个 NPC），不含 `1/30/2`，并且
  session marker 在响应后清除。
- 该回归不启动客户端、不修改 CBE/CBM 内存或状态；完整 UI 路径仍需隔离 MySQL 配置后再跑。
