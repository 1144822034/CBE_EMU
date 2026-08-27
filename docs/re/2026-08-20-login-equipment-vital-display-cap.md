# 登录后装备 HP/MP 当前值按裸装上限显示

> 2026-08-27 修正：本文关于 `30/21 -> 7/7(type=2) -> 7/7(type=3)` 保持为
> 首登重建顺序的描述已失效。完整派生 HP/MP 仍由登录 `actorinfo` 承载；已装备实例的
> 首登对象仍是 `unresolved`，不得复用 item-operation `7/7`。见
> [登录已装备物品误走 7/7 type=2 的 InGame 断言](2026-08-27-login-equipped-item-operation-assert.md)。

## 触发条件

角色已经穿戴会增加最大 HP、MP 的装备，使用恢复道具使 HP/MP 回满后重新登录。人物属性页
能够显示装备属性加成，但剩余 HP、MP 仍按穿戴前的最大值显示。

## 首次偏离与证据

`player-3` 的服务端运行日志在重新登录前后都记录了完整的权威生命值：

```
mock_team_member_row ... role=10036 ... hp=6078/6078 mp=4154/4154
```

恢复道具请求同时记录 `applied=0`，原因是服务端角色在进入该请求前已经是满值，而不是恢复
结果未保存。随后重新登录仍输出同一组 `6078/6078`、`4154/4154`，因此数据库持久化和恢复
处理不是首次偏离位置。

登录 `1/1/6` 的 `actorinfo` 由
`vm_net_mock_build_actor_info()` 依序发送：当前 HP、裸装 HP 最大值、当前 MP、裸装 MP
最大值，以及靠后的 `primaryDisplayMax`、`secondaryDisplayMax`。客户端
`parse_actorinfo_playerinfo_blob`（`JianghuOL.CBE:0x0100FA88`）将这两个靠后字段写入状态
节点的 `+0xC4/+0xC8`；已取证的 HUD 绘制以它们作为 `+0xB4/+0xB8` 当前值的刻度上限。

此前代码已正确把前面的 base-max 保持为裸装，避免客户端在后续 `1/7/7` 装备重建时重复累加。
但靠后的显示上限也错误地默认取了裸装 base-max。这正是服务端保存值正确、登录画面却回退为
裸装上限的首次错误状态。

## 修复

`src/server/mock_server_interaction_login.c` 现在保持前面的 base-max 为裸装值，但默认把
`primaryDisplayMax`、`secondaryDisplayMax` 设置为 `vm_net_mock_role_default_vitals()` 产生的
全装备持久化最大值；若诊断覆盖值低于当前值，则仅将该显示上限抬到当前值。

该改动只修正标准登录响应的显示上限字段：不写入客户端内存、不修改 CBE/CBM，也不更改
战斗、恢复或数据库的权威数值来源。

## 回归验证

扩展了 `scripts/first-login-equipment-attribute-bootstrap-regression.c`。夹具从当前 `equip.dsh`
选择一对分别提升 HP 和 MP 的可用装备，将角色持久化当前值设为全装备后的满值，解析真实
选角 `1/1/6` 响应内的 `actorinfo`，断言：

1. 前置 HP/MP base-max 等于裸装计算值；
2. 当前 HP/MP 等于全装备后的持久化满值；
3. 两个 HUD display-max 也等于全装备最大值；
4. 原有 `30/21 -> 7/7(type=2) -> 7/7(type=3)` 首次登录装备重建顺序仍保持不变。

2026-08-20 的验证命令：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/first-login-equipment-attribute-bootstrap-regression.c obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o obj/server/md5.o '-Wl,--gc-sections' -o tmp/first-login-equipment-attribute-bootstrap-regression.exe -lpthread -liconv -lm -lkernel32 -lws2_32
.\tmp\first-login-equipment-attribute-bootstrap-regression.exe
```

结果：`first-login equipment attribute bootstrap regression passed type3_completion=1`。夹具不监听
端口且不连接 MySQL；输出中的可选经验卡/战斗状态读取错误来自刻意无数据库的隔离环境，未影响
所断言的 ActorInfo 响应字节。
