# NPC 技能导师分页与遗忘技能

日期：2026-08-18  
状态：已实现，待真实客户端体验验收

## 2026-08-18 遗忘列表等待进度条修正

首次客户端复测的原始日志为：

```text
mock_npc_service action=skill-menu opcode=e4000001 ... options=2 ... resp=167
net_send ... wt=26/1 ... source=builtin-npc-service resp=167
unhandled wt=26/1 len=30 objects=1 first=1/26/1:21
net_send ... source=ignored-unhandled-server-only resp=0
```

菜单响应已经被客户端解析；点击“遗忘已学技能”后客户端正常发出下一条 `26/1`，但旧
detector 仍以交易取消 opcode `0xf1` 作为合法范围上限，新列表 `0xf3` 因此在 builder
之前被拒绝。没有网络事件返回时，客户端等待进度条不会结束。

detector 现改为明确列举所有已经实现的 NPC service opcode，其中包含技能学习列表
`0xf2`、遗忘列表 `0xf3` 和遗忘动作 `0xf4`；相邻但未实现的 `0xf5` 继续拒绝，避免用
扩大数值范围的方式吞掉未知请求。隔离回归同步断言这四个边界。

## 2026-08-18 学习/遗忘确认与遗忘费用

用户复测“道统天地1”时看到的“服务请求无效”不能单独证明请求 ID 错误。运行日志中
`f4|251` 和 `f4|211` 已经完成删除并更新角色技能集合，说明 `26/1 type=2,id=value`
和 `f4` detector 的编码契约成立；旧遗忘分支在删除失败时没有专门错误文本，才会落回通用
提示。现在失败会区分技能状态变化、铜钱不足和持久化失败。

学习和遗忘都改为两阶段的原生 NPC 对话流程。首次点击只建立一次性的服务端确认上下文，
上下文保存角色、NPC、场景、技能 ID、原列表页和报价；客户端收到普通 `26/1.dialog` 的
“确认/返回”两个选项。确认请求消费上下文后重新核对职业、等级、已学状态、技能价值和
铜钱余额，避免列表打开后状态变化或重放请求造成错误操作。遗忘费用与学习相同，均取
`skill.dsh` 的 `价值` 字段；成功后扣除铜钱、追加 `1/12/1` 技能集合和原生钱包对象。

技能持久化或角色保存失败时会恢复内存角色，并补偿刚刚写入或删除的技能行；补偿失败会
留下错误日志，不会伪造成功响应。

## 触发与首个偏离

技能导师使用普通 NPC 对话协议 `WT 1/26/1`。旧服务端进入导师后遍历本职业全部技能，
但只要 `optionCount` 达到服务对话上限 7 就直接跳过后续技能，没有编码页码或上一页、
下一页。因此高等级角色拥有超过 7 个可学技能时，客户端永远无法选择后面的技能。

同一入口只构造“学习技能”列表，也没有从角色权威技能表生成遗忘列表和删除持久化路径。
首个错误状态在服务端列表构造，不是客户端翻页控件或技能资源加载。

## 客户端与协议证据

- `江湖OL.CBE:ParseNPCDialogData (0x010380E8)` 解析 `1/26/1.dialog` 中的
  `name, action, value, description` 选项行，并将 `value` 原样保存为 `u32`。
- `江湖OL.CBE:task_hall_activate_selected_entry (0x010492B0)` 对
  `action=1` 统一发送 `WT 1/26/1 { type=2, id=value }`。因此分页、学习和遗忘都可以
  复用已经由装备商店验证的嵌套对话链，不需要新客户端回调。
- `1/12/1 { learnednum, learnedskill }` 是客户端已有的技能状态同步对象；学习和遗忘成功
  后都必须追加该对象，不能只改变服务端数据库。
- `skill.dsh` 的职业、等级、价值和技能 ID 继续作为目录权威；角色已学集合来自
  `account_role_skills`。

## 修复

技能导师入口改名为“技能修习”，进入后显示两个 `action=1` 选项：

1. 学习新技能：仅列出职业匹配、等级满足且尚未学习的技能。
2. 遗忘已学技能：仅列出当前角色已学习的本职业非初始技能。

两类列表每页最多 5 个技能，余下两个安全槽位用于上一页和下一页。页码只编码在服务端
私有的 `value` 高字节命名空间中，客户端仍发送正常的 `type=2` 请求。列表缩短后页码会
钳制到最后有效页，学习或遗忘完成后也按操作前技能所在页恢复。

遗忘通过 `(account_id, role_id, skill_id)` 精确删除 `account_role_skills`，数据库成功后
才移除会话缓存，并立即追加 `1/12/1`。初始职业技能不进入遗忘列表：当前数据模型用
“没有技能行”表示新角色需要初始化初始技能，允许删到零会导致下次登录又自动补回，形成
不持久的假遗忘。

## 验证

修改后执行：

```text
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 \
  -ffunction-sections -fdata-sections \
  scripts/npc-skill-trainer-pagination-regression.c \
  obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o \
  obj/server/md5.o -Wl,--gc-sections \
  -o tmp/npc-skill-trainer-pagination-regression.exe \
  -lpthread -liconv -lm -lkernel32 -lws2_32
tmp/npc-skill-trainer-pagination-regression.exe

gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 \
  -ffunction-sections -fdata-sections \
  scripts/npc-equipment-confirmation-regression.c \
  obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o \
  obj/server/md5.o -Wl,--gc-sections \
  -o tmp/npc-equipment-confirmation-regression.exe \
  -lpthread -liconv -lm -lkernel32 -lws2_32
tmp/npc-equipment-confirmation-regression.exe
```

隔离回归构造 14 个同职业技能，验证学习列表和遗忘列表均超过一页、第二页定位正确、
初始技能和其他职业技能被排除、列表缩短后的页码钳制，以及两个角色的已学技能筛选互不
污染；同时验证学习/遗忘确认上下文保存技能、页码和报价，只能消费一次，并拒绝不匹配
的 NPC 上下文。装备商店确认回归继续通过，说明扩展上下文种类没有破坏购买/回收流程。
两个回归都不连接 MySQL、不启动监听器、不运行客户端。

真实客户端验收仍需覆盖：导师两项入口、学习列表前后翻页、遗忘列表前后翻页、学习后
技能面板刷新、遗忘后技能面板刷新、重新登录后的持久化状态。
