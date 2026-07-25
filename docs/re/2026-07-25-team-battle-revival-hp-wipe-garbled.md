# 组队复活后队长遇怪乱码且无法再进战斗

## 压缩结论

| 现象 | 根因 | 修复 |
| --- | --- | --- |
| 弹“乱码”且不进战斗 | `reject-dead` 横幅用了 UTF-8，客户端按 GBK 显示 | 横幅改为显式 GBK 字节 |
| 复活后永久遇怪失败 | 复活座位 `battleMemberHp` 故意保持 0；后续 presence/`publish_member_vitals`/dead-wait 的 `publish_role_vitals` 把 0 写回 online/durable HP | leftMask 跳过覆盖；dead-wait 恢复 durable globals；`publish_role_vitals` 拒绝用 0 覆盖非零 HP；新战斗种子优先用 role vitals |

## 验证

重启服务。若账号已被写成 HP=0，需再走一次复活石或重载已修复的角色库。

1. 组队战斗中一人复活石复活后，队长/队员再遇怪应正常进战。
2. 若仍拦截，横幅应为可读中文“您已经死亡，请先使用复活石”，不应再是乱码。
