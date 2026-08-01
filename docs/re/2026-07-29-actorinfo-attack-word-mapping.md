# 2026-07-29 属性页物攻与 actorinfo 六字映射（更正）

## Symptom

属性页物攻一直为 0；把 `playerStats.attack` 写入 actorinfo 六字
`word[4]` 后仍然为 0。战斗伤害正常（服务端用 `playerStats.attack`）。

后续误把 `word[2]` 当智慧、`word[5]` 当暴击后，登录面板出现
**命中=183（实为 wis）**、**抗性=5（实为 crit_word）**。

## Root cause（已交叉验证）

属性绘制 `JianghuOL.CBE:0x010223DE` 从 `actor+0x120` 读 halfword，顺序为
`+2,+4,+0x10,+0x12,+8,+0xa,+0xe,+0xc`。

穿戴 apply 对战斗对的写入（`0x0100FF72` → `0x01028BCE`）：

| 来源 | 目标 | 面板语义 |
| --- | --- | --- |
| 武器类 `item+0xFA`（equip.dsh 攻击） | `actor+0x130`（`#0x10`） | **物攻** |
| 防具类 `item+0xF8`（equip.dsh 护甲） | `actor+0x132`（`#0x12`） | **护甲** |

`parse_actorinfo` 六字写入
`+0x122,+0x124,+0x12a,+0x126,+0x132,+0x12c`，**没有**对 `+0x130` 的
store。

运行时证据（role 34，2026-07-29 登录）：

```text
mock_actorinfo_attrs ... wis=183 ... crit_word=5 hit=5 resist=0
面板：命中=183，抗性=5
```

→ `word[2]`/`+0x12a` 画在 **命中** 行；`word[5]`/`+0x12c` 画在 **抗性** 行。
paint 八列与标签对齐为：**力,敏,物攻,护甲,闪躲,命中,暴击,抗性**
（UI 字符串簇里仍有「智慧」，但其值绑定仍 **unresolved**，暂放
`word[3]`→`+0x126`，不在 paint-8 内）。

## Correct wire / halfword mapping

| UI / paint | actor 偏移 | 来源 |
| --- | --- | --- |
| 力量 | +0x122 | word[0] |
| 敏捷 | +0x124 | word[1] |
| 物攻 | +0x130 | **仅武器** `item+0xFA` |
| 护甲 | +0x132 | word[4] bare + 防具 `item+0xF8` |
| 闪躲 | +0x128 | wear-apply（enhance wire→4） |
| **命中** | **+0x12a** | **word[2] = hit** |
| 暴击 | +0x12e | wear-apply（enhance wire→6） |
| **抗性** | **+0x12c** | **word[5] bare=0 + wear `fec6`（装备抗性）** |
| 智慧 | +0x126? | word[3] provisional / unresolved paint |
| 活力 | — | unresolved |
| 魅力 | EXTRA132→+0x84 | charm |

| 六字下标 | actor 偏移 | 用途 |
| --- | --- | --- |
| 0 | +0x122 | 力量 bare |
| 1 | +0x124 | 敏捷 bare |
| 2 | +0x12a | **命中**（仍含服务端 101+装备；fec6 也会加装备命中，见残留） |
| 3 | +0x126 | 智慧（provisional） |
| 4 | +0x132 | 护甲 bare |
| 5 | +0x12c | **抗性 bare（0）** — 全量由 fec6 再加；见 `2026-07-31-actorinfo-resist-bare.md` |

## 强化 type：详情名表 vs 面板 jump（2026-07-29 夜）

详情 `(+%d)%s+%d`（`0x01032118`）按 **名表** 取名：1力…5护甲…8暴击。
穿戴 jump（`0x010100D0`）是另一套 0-based：0力 1敏 2攻 3甲…

曾把 wire remap 成 jump 下标以修面板，结果详情变成：
`(+4)智慧 350`（甲→3）、`(+255)…`（M(L) unlock=255）、
`(+12)暴击+15%`（法力%→8）。

现约定：**wire 发名表 type**，unlock 用真实 4/8/12/16；**不再下发 M(L)
attr 行**（缩放仍走 F8/武器+0xFA）。各部位 +4/+8/+12/+16 词条种类与品质
定值未改。面板与 jump 名表冲突仍 soft / unresolved。
