# 角色初始场景规范化

## 目标

新角色及旧初始场景记录统一保存为：

```text
c00蓬莱仙岛_01.sce
```

不再把 `00_蓬莱仙岛01.sce` 作为角色初始场景。

## 实现

- 新角色继续由 `vm_net_mock_role_init_default` 使用
  `vm_net_mock_role_initial_scene_name()` 初始化，目标资源为
  `c00蓬莱仙岛_01.sce`。
- MySQL 角色加载时把旧场景名以及缺少 `.sce` 的同一新场景键规范化为
  完整目标名，并保存回 `account_roles.scene`。
- 移动位置保存同样执行规范化，避免运行时的无扩展名场景键再次覆盖完整值。
- `server/mysql/migrate_initial_scene.sql` 可一次性升级现有数据库；其他场景和
  角色坐标不受影响。

## 资源依据

- `web/fs/JHOnlineData/c00蓬莱仙岛_01.sce` 存在，长度 254 字节。
- `bin/JHOnlineData/c00蓬莱仙岛_01.sce` 存在，长度 254 字节。
- 客户端入场仍通过现有场景字段和正常网络回调推进，不修改 CBE 内存状态。

