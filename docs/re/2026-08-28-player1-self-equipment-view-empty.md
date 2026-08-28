# player-1 人物信息“装备”为空（2026-08-28）

phase: player1-self-equipment-view
status: fix-implemented-build-blocked-by-existing-worktree-conflict

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

## 5. 实施的修复

- 恢复 `vm_net_mock_append_backpack_role_grid_main_objects` 的一阶段、一次性首登装备
  bootstrap：`30/21`（背包）后追加完整 `1/7/7 type=2 { iteminfo }`，并紧随零行
  `1/7/7 type=3 { iteminfo=00 }`。
- `type=2` 行只从角色持久化的有效装备槽构造，序号固定为 `slot + 1`，携带耐久和完整强化属性；
  不复用用户穿戴/卸下操作的 pending 状态。
- `type=3` 只在至少有一条装备行时发送一次，用于客户端原生的装备流收尾与状态重算；没有恢复
  8 月 26 日曾存在的宿主拆包、双事件投递或任何客户端内存／寄存器写入。
- 回归场景改为断言首包中的完整 `type=2` 行、紧随的零行 `type=3`、重复 group 请求不重放、
  重新选角后仅重新发送一次。

## 6. 取证冲突与构建状态

8 月 27 日的断言记录把 `7/7 type=2` 概括为物品操作路径并因此删除了该链路；但保存的
player-3 原始首登包和只读客户端记录给出了更强的反证：`type=2` 的完整装备行在
`mmGame:sub_D04` 中建立八条 category-15 装备实例，空 `type=3` 随后走原生状态重算；
8 月 26 日用户确认正常的版本也保留这套顺序。故本次恢复的是该已捕获的、受限的首登对象序列，
不是把任意 `7/7 type=2` 当作通用物品操作回包。

本次修改后的 `make -j2` 已执行，但被工作区原有的分文件重构阻断：`src/server/mock_server.h`
已经将 `vm_net_mock_get_object_entry_field`、`vm_net_mock_guild_find_role_membership`、
`vm_net_mock_open_server_data_resource`、`vm_net_mock_append_scene_room_roles_object`、
`vm_net_mock_build_scene_list_otherinfo_blob` 声明为外部函数，而对应实现仍是 `static`。
这些文件和声明均不是本修复修改点，未擅自覆盖。解决该已有冲突后，应依次运行：

```powershell
make -j2
make first-login-equipment-attribute-bootstrap-regression
.\obj\server\first-login-equipment-attribute-bootstrap-regression.exe
```

随后使用隔离账号重登，确认日志有 `mock_equipment_login`、
`mock_login_equipment_type3_completion`，并保留人物信息装备页的客户端状态和 `29/4`（如有）请求证据。
