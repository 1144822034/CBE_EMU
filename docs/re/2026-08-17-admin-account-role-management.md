# 后台账号角色管理

账号管理的每个角色行现在以持久 `role_id` 作为操作目标，而不是当前活动角色。

- 角色行显示职业，并可修改名称。提交的 UTF-8 表单名称会转换为客户端使用的 GBK，限制为 2 至 31 个有效字节；同一账号不允许重名。
- 改名在单个 MySQL 事务中同步 `account_roles`、好友目标名称、帮派帮主/成员名称和待处理帮派申请名称。历史世界聊天记录保留发送时的名称。
- 设置等级前，若该账号存在在线游戏会话，服务端先走标准离线生命周期并清除会话绑定，再将指定角色的 `level`、累计 `exp` 和派生 HP/MP 写入数据库。
- 位置重置同样先断开目标角色、重新读取离线后的持久快照，再写入从指定 SCE 解析出的安全落点；不会在保存后才断线。
- 普通钱币增加改用全量账号角色快照持久化。此前的活动角色快速保存路径会遗漏非首个/非活动角色的金额变更。

关键日志：

```text
[info][mock-admin] role_name_set ... action=commit
[info][mock-admin] role_level_set ... disconnected=<n> action=commit
[info][mock-service] account_money_add ... id=<role_id> ...
```
