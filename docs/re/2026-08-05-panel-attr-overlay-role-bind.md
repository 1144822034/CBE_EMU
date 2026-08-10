# 2026-08-05：面板属性 overlay 未按角色绑定

## 症状

相互（role=455）与阿狸（role=456）分别登录后，PC 属性页力/敏/智/物攻等显示成同一套数值。

## 证据

同会话服务端 `mock_actorinfo_attrs` 已按角色分开发：

- role=456：`str_word=1276 agi_word=1347 wis_word=5504 eq_wis=5213`
- role=455：`str_word=1347 agi_word=4117 wis_word=1346 eq_agi=3896`

权威 actorinfo **已按角色绑定**；串数发生在 host 面板填值层。

## 根因（历史）

当时登录 / 穿戴同步走 `publish_panel_attr_overlay` → 固定路径
`bin/cbe_panel_attr_overlay.txt`。多开模拟器共读共写该文件：后登录角色的
publish 覆盖前者，两边 `vm_host_realign_attr_panel_value_rows` 都整表采用
最后一份 overlay。

## 后续

曾改为按 `roleId` 分文件。2026-08-06 起整段 overlay 机制废止
（见 `2026-08-06-remove-panel-attr-overlay-file.md`）：面板只读 actor 半字，
不再使用旁路文件。
