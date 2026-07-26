# 加入帮派列表列契约修复（2026-07-26）

## 触发与现象

未加入帮派的角色点击“加入帮派”，客户端发送 `WT 1/10/21`
`{index:u32,pagesize:u8}`，随后在帮派列表中显示：

- “排名”恒为 `1`；
- “人数”列显示创建者的角色名。

这不是界面文本或数据库字段显示问题：两个错误值都来自同一条 `10/21.faction`
行流，且未发生解包异常。

## 取证与首次偏离

客户端 `江湖OL.CBE` 的
`HandleFactionMemberListResponse(0x0103F566)` 按以下顺序读取每行：

1. `guildId`（第一行 raw BE32，后续 tagged-u32）；
2. 数值，写入 `row+40`，在 `0x0103F806` 渲染到 x=5；
3. 字符串，写入 `row+4`，在 `0x0103F858` 渲染到 x=50；
4. 数值，写入 `row+38`，在 `0x0103F8AA` 渲染到 x=160；
5. 字符串，写入 `row+17`，在 `0x0103F8F8` 渲染到 x=205。

从 `bin/CBE/江湖OL.cbe` 同一静态字符串表取得该列表的连续列标题为
“排名、帮派名、等级、人数”。结合 parser 的 `u32,string,u32,string` 类型顺序，
实际 wire contract 是：

```text
guildId, listRank, guildName, guildLevel, memberCountText
```

旧 `vm_net_mock_build_guild_page_response` 把持久化的 `guildLevel` 写入第二项，
并把人数数值和创建者姓名错误地写在第三、第四项。这是客户端收到响应时的首个错误状态：
帮派等级通常为 1，正好被渲染为固定“排名 1”；创建者名则被渲染到“人数”列。

## 修复

`src/server/mock_server_guild.c` 现在按查询顺序计算稳定的一基全局排名
`offset + rowIndex + 1`，并以 `id, rank, name, guild_level, member_count_text`
写入 `faction` blob。`guild_level` 仍在列表的“等级”列及帮派详情 `10/23` 中按其原本
语义使用，人数按客户端字符串槽以 `当前人数/上限` 下发。

修复没有改变客户端、没有抑制 UI，也没有伪造空响应；首行 raw-BE32 和其余 tagged
字段编码规则保持不变。

## 验证

1. 以未入帮角色进入“加入帮派”：每条记录的排名按页从 `index` 开始递增，等级与人数列各自正确。
2. 翻至第二页：第一条排名必须为 `pagesize + 1`，而不是重新从 1 开始。
3. 以已入帮角色打开成员列表（歧义 `10/20`）：同包中的 `10/20` 成员对象仍保持原有字段顺序，
   `10/21` 帮派列表对象按本契约解析。
