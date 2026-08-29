# player-1 人物信息“装备”为空（2026-08-28）

phase: player1-self-equipment-view
status: superseded-by-2026-08-29-player1-shop-return-ingame-assert

## 1. 现象与边界

- player-1 登录角色后，从“人物信息 -> 装备”查看时，所有装备槽为空；角色持久化数据实际有已穿戴装备。
- 用户确认两天前的版本可以正常显示，因此这必须按近期回归处理，不能归因为早期目标解析的长期差异。
- 本轮只读取历史、源码和现有运行记录；未改动客户端、客户内存、寄存器、响应字节或角色数据。

## 2. 首个导致回归的改动

提交 `a13d1e110ae12d4ce4a26c328a383b1ab7d5902b`（2026-08-27 20:30，
“新增玄晶合成的NPC服务 … 网络回调相关事件，收缩到纯平台事件模拟 …”）删除了首次
`5/10 + 7/7(type=1)` 初始化响应中的两类对象：

1. `WT 1/7/7 { type=2, iteminfo=<durable equipped rows> }`；
2. 随后的空 `WT 1/7/7 { type=3, iteminfo=<0> }` 收尾对象。

删除点在 `vm_net_mock_append_backpack_role_grid_main_objects`。该提交也删除了
`vm_net_mock_append_equipment_login_object` 和
`vm_net_mock_append_equipment_login_type3_completion_object`，所以不是仅改了日志或缓存。

该提交前（包含 2026-08-26 的可用版本），首次登录会把角色已装备行下发给客户端；因此客户端会建立装备实例并在装备页使用。提交后，服务端仍保存已装备行，但首次登录只下发 `30/21`
背包网格和 `7/11` 仓库计数，客户端没有建立对应的装备显示数据，结果就是本次全空。

## 3. 证据

| 证据 | 结论 |
| --- | --- |
| `git diff a13d1e1^ a13d1e1 -- src/server/mock_server_catalog.c` | 明确删除了 `type=2` 装备行和 `type=3` 收尾对象的构造与追加。 |
| 当前文件 `src/server/mock_server_catalog.c:7934` | 注释明确要求将已装备行“保留在服务端并省略”，等待真实首登协议被逆向确认。 |
| `bin/server_out.txt` 当前 player-1 会话 | 记录 `mock_backpack_grid role=10871 ...` 与 `mock_backpack_reservoir_seed`，未记录旧版的 `mock_equipment_login` 或 `mock_login_equipment_type3_completion`。 |
| `docs/re/2026-08-27-login-equipped-item-operation-assert.md` | 说明删除的直接动机是 player-3 会把 `type=2` 走入 `TimerControl_ProcessItem` 并在 `MMORPG_Screen_InGame.c:913` 断言；同时也明确真正的首次装备实例协议仍是 `unresolved`。 |

## 4. 旧结论更正

先前提出的 `6a7d163`（2026-07-18）“人物信息 resolver 比装备查看 resolver 更宽”的差异确实存在，
但它早于用户确认正常的版本，不能解释这次回归，不是本问题的导致提交。该差异只应作为后续
`29/4` 自身目标请求的独立兼容性检查项。

现有日志没有保留本次菜单点击后的 `WT 29/4` 原始请求/响应，因此尚不能把该历史差异与当前
单次菜单点击直接绑定；但首次装备实例初始化被删除已经足以解释“有持久装备、客户端装备页全空”的版本回归。

## 5. 已否定的修复候选

曾尝试把 `7/7(type=2)` 的完整装备行推迟到 CBE 的随后的 `7/7(type=3)` 请求中，以恢复
装备页实例。player-1 商城返回运行再次触发 `MMORPG_Screen_InGame.c:913` 后，这一候选被否定：
`7/7(type=2)` 仍是 CBE 的新增物品通道，而不是可以安全重放已装备行的初始化通道。

后续实现保留同角色返回所需的自然 `30/21` 背包快照与 `1/7/32` 状态响应，但完全禁止该
bootstrap 生成 `1/7/7 { type=2|3 }`。详情见
[`2026-08-29-player1-shop-return-ingame-assert.md`](2026-08-29-player1-shop-return-ingame-assert.md)。

## 6. 取证更正

保存的旧包只能证明解析器会读取该对象，不能证明它是该商城返回 callback 的合法响应。
player-1 的实际断言是更强的反证：把已有装备行重新送入该 parser 会请求新增物品槽并违反
InGame 的生命周期约束。因此装备页初始化协议仍是 `unresolved`；不得以 `7/7(type=2|3)`
补偿该 UI 缺口。

```powershell
make -j2
make first-login-equipment-attribute-bootstrap-regression
.\obj\server\first-login-equipment-attribute-bootstrap-regression.exe
```

该夹具不连接 MySQL、不启动监听器，已经确认 type-2 回复的完整 `30/21` 与 type-3 的原生
装备完成流，但它不运行客户端。仍须使用隔离账号重登，确认日志按
`mock_backpack_full_bootstrap_arm -> mock_backpack_grid attrs=full -> mock_equipment_login ->
mock_login_equipment_type3_completion -> mock_backpack_full_bootstrap_complete` 出现，并保留
人物信息装备页的客户端状态和 `29/4`（如有）请求证据。
