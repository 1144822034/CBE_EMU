# 野外实例键 + 梦境 .map 对照实验（2026-08-02）

## 目的

隔离遇怪 `0x01046C48`：是 **29\* 进图协议**（dream_drain / no-27/12 /
hot518）还是 **梦境 SCE/.map 本体**。

## 资源

| 项 | 值 |
|----|-----|
| 新场景键 | `28野外梦测_01`（web/fs + bin 双子） |
| SCE 体 | 字节拷贝自当前 `29梦境空间_01`（含 kind=3） |
| 内嵌 .map | 仍为 **`29梦境空间_03.map`**（套用梦境地图资源） |
| 脚本 | `scripts/make_field_instance_dream_map.py` |

## 服务端

使者/副本向导 `target_scene=29*` 时，默认重定向到 `28野外梦测_01`
（`CBE_FIELD_DREAM_MAP_INSTANCE=1`，日志 `field_dream_map_instance_redirect`）。
`=0` 恢复纯 29\* 进图。

因场景名不是 `29*`，走野外 post-enter（含 `27/12`），不再 dream_drain。

## 期望对照

| 结果 | 含义 |
|------|------|
| 进图 OK + 遇怪进 mmBattle | 根因在 29\* 协议/壳，不在 .map/SCE 体 |
| 进图 OK + 仍 `0x01046C48` | 更指向 SCE/map/kind3 体或热 DF 共性 |
| 进图闪退/卡条 | 28\* + 梦境 map 组合或 27/12 与 FB SCE 冲突 |

## 复测结果（进图闪退 — 已证伪）

```text
field_dream_map_instance_redirect from=29梦境空间_01 to=28野外梦测_01
queue_data_primary resp=52   # 30/1
# 无 screen_init_ok / 随即客户端闪退
```

**结论**：非 `29*` 键 + 梦境 SCE 体 + 野外 `27/12` 进图路径不可用（进图即崩）。  
已**撤回**服务端 redirect；`28野外梦测_01` 资源可留作手工对照，默认使者仍进 `29*`。

### 登录卡在实验场景

redirect 曾把角色持久化到 `28野外梦测_01`；原
`dream_kind3_login_relocate` **只匹配 `29*`**，冷登 `28*` kind=3 会闪退。  
已扩大：`29*`（kind=3）与 `28野外梦测_01` 登录均迁到 `c04临安府_06`。

遇怪问题继续在可走的 `29*` + `no-27/12` 路径上取证。
