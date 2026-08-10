# 2026-08-04 登录全属性汇总（panel overlay）

## Symptom

登录已穿戴时，属性页**部分**有值（三围/护甲/命中来自 actorinfo 六字），
**物攻/闪躲/暴击/抗性**仍为 0。

## Evidence（debug-a1d304）

- `mock_actorinfo_attrs`：`eqAtk=2304` `fullAtk=4499` `w0_str=2195` `w4_armor=883`
- `login_equip_wear_sync_once`：`wear_7_8=0`（假 7/8 会卡登录）
- `attr_rows_realigned`：`atk=0 dodge=0 crit=0 resist=0`，`str=2195 armor=883`

## Contract

| 面板行 | 正常权威 | 登录缺口 |
| --- | --- | --- |
| 力/敏/智/命中/护甲 | actorinfo 六字 | 已发满值 |
| 物攻 | 武器 `item+0xFA` wear-apply → `+0x130` | 登录无 FA |
| 闪躲/暴击 | enhance jump wear-apply | 同上 |
| 抗性 | bare word[5]=0 + fec6 | 同上 |

## Fix（窄）— 已废止

原方案用 `cbe_panel_attr_overlay*.txt` 旁路补登录无 FA 的面板缺口。
2026-08-06 起整段移除（见 `2026-08-06-remove-panel-attr-overlay-file.md`）。
面板只读 actor 半字。

## Unresolved

安全触发登录 wear-apply 的包路径（非 unsolicited 7/8）仍 open；半字粘滞
仍可能存在，面板以 overlay 为准。
