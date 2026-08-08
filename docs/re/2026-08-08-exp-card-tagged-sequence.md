# 经验卡与时效道具：tagged 序号解析

## 触发与首次偏离

在角色 `10024` 的背包中使用四倍经验卡（物品 `810`，背包序号 `17`）时，
客户端停留在等待提示。原始服务日志的第一处偏离是：

```text
[error][network] unhandled wt=7/30 len=35 objects=1 first=1/7/30:26
```

这说明请求没有进入专用道具响应 builder，因而不存在可关闭该等待状态的
`1/7/30` 响应；不是背包刷新或 UI 时序问题。

## 客户端与报文证据

`JianghuOL.CBE:0x01023630` 发送经验卡请求，
`0x01025AE6` 在 `1/7/30 { result, iteminfo }` 成功后自行删除当前背包行。
真实堆叠卡片请求为 35 字节：

```text
WT 1/7/30 {
  itemseq: entry-length 4, tagged-u16 (00 02 00 11),
  num:     entry-length 6, tagged-u32 (00 04 00 00 00 01)
}
```

其中 `00 04` 是 `itemseq` 条目的外层长度，不是数值 tag。旧的通用数值
扫描器先尝试 u32，会把序号 `17` 读成 `0x00020011`，超过客户端序号 u16
范围，导致 `vm_net_mock_parse_special_item_seq_request()` 返回 false。随后
分派层虽已调用 `vm_net_mock_build_timed_special_item_use_response()`，但 builder
返回零，最终记录为 unhandled。

同一 helper 同时负责下列已建模的专用请求，因此它们有相同的解析风险：

| 请求 | 道具类别 | 成功响应 |
| --- | --- | --- |
| `1/7/30` | 809/810/811 双倍、四倍、十倍经验卡 | `1/7/30` |
| `1/22/3` | 829/830 与活动时效攻防道具 | `1/22/3` |
| `1/25/6` | 828 战斗心得 | `1/25/6` |
| `1/7/16`、`1/7/33`、`1/7/35`、`1/7/38`、`1/7/40` | 已确认但部分尚未实现权威效果的特殊道具 | 各自同 subtype |

## 修复

`src/server/mock_server_catalog.c` 的
`vm_net_mock_parse_special_item_seq_request()` 现在使用
`vm_net_mock_get_object_tagged_number_entry()` 解码 `itemseq`/`seq` 与可选
`num`。该 accessor 精确检查 entry 的 `00 01`、`00 02` 或 `00 04` 数字形式，
与既有宝箱和装备强化序号解析一致。

没有改变客户端响应对象、字段顺序或库存归属：

- 成功经验卡仍只返回一个 `1/7/30`，由客户端自己的 handler 删除当前行；
- 829/830、活动糖果和 828 仍各自只返回原生专用对象；
- 未建模权威状态的特殊道具仍走其明确的非消耗响应，不会被伪装为成功。

## 回归验证

执行：

```powershell
make -j2
$env:CBE_AUTOMATION_MYSQL_PASSWORD='123456'
.\scripts\run-timed-item-effect-automation.ps1
```

自动化运行在私有 `jh_online_autotest_<guid>` schema、端口 `19198/19199` 和
独立资源副本中；不会连接或修改用户的 `jh_online` 数据和服务进程。

证据目录：
`artifacts/automation/timed-item-effects-v1-20260808T062737536Z-3644/`。
测试使用与真实日志相同的 35-byte `1/7/30` 编码，依次验证 809/810/811 的
2x/4x/10x 时效记录、背包原子扣除、活跃 `1/7/31` 状态与过期后
`1/7/31 + 1/7/32`；还验证 `1/25/6` 战斗心得的 +20% 经验效果记录，以及
`1/22/3` 时效攻防道具及其战斗数值效果。
