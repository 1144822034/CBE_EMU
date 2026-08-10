# 2026-08-06：取消 host 面板 overlay 文件

## 决定

`cbe_panel_attr_overlay*.txt` 不是协议必需件，仅是模拟器 host 用本地文件
补登录无 FA wear-apply 时属性页物攻/闪躲/暴击等缺口。远端 Android 读不到，
多开时还曾串角色。

已整段移除：

- 服务端 `publish_panel_attr_overlay`
- PC / Android host `read_panel_attr_overlay` 与 realign 整表覆盖

## 现状契约

属性页字符串只来自 actor 半字（actorinfo 六字 + 客户端 wear-apply）。
登录未走 FA 时物攻等仍可能为 0，属客户端路径缺口，不再用旁路文件掩盖。

相关旧文档：`2026-08-04-login-full-attr-overlay.md`、
`2026-08-05-panel-attr-overlay-role-bind.md`（历史记录，机制已废止）。
