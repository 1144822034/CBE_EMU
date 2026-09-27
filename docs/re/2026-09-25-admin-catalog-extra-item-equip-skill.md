# 管理后台自定义目录（道具 / 装备 / 技能）

日期：2026-09-25

## 范围

- MySQL `server_item_catalog_extra` / `server_equip_catalog_extra` / `server_skill_catalog_extra`
- 管理端 Tab「目录管理」：新增 / 编辑 / 删除（仅自定义行）
- Pixi：`GET /api/custom-catalog` 合并进本地 item/equip/skill 目录
- **根因（2026-09-25）**：MySQL 行字段无 NUL 结尾，`strtoul(item_id)` 读到下一列 HEX(name) 失败，自定义装备行被整行跳过 → API `equips:[]` → Pixi 只显示 `#id`。已改为 length-safe 解析，且 API/目录页 `force_reload`。
- **不改写** `item.dsh` / `equip.dsh` / `skill.dsh`；CBE 本阶段不保证可见

## ID 分配

创建时服务端取当前物品号（道具+装备共用）或技能目录的 **max+1**，表单不填 ID，保存后只读展示。编辑禁止改号。

## 加载顺序

1. DSH + `ensure_*` 注入
2. `vm_net_mock_catalog_extra_db_load()` 读三表
3. 合并进 shop / item_effect / equipment / skill 内存目录

## Pixi

`PixiJS_web/src/resources/customCatalog.ts` 从 **live gateway**（与 monster-levels 同 host:port）拉取
`GET /api/custom-catalog`，在 `loadItemCatalog` / `loadEquipCatalog` / `loadSkillCatalog` 后 merge。

网关必须代理该路径到管理端 `:19091`（`scripts/ws-gateway.mjs` /
`gateway-release/ws-gateway.cjs`）。拉取失败不阻断登录，且**不永久缓存空结果**，下次 warm 可重试。

API 带 `rev`（后台 create/save/delete 自增）。Pixi 打开背包时重拉；游戏中每 8s /
切回前台时若内容签名变化则合并。拉取失败保留上次成功目录（禁止用空结果剪掉已合并装备名）。

背包/身上显示名走 `lookupItemSync` 实时目录（避免选角时目录尚未就绪只剩 `#itemId`）。

## 验证

1. `GET http://<gateway>:19092/api/custom-catalog` 返回 equips[].name
2. 后台新增装备 → grant → Pixi 背包显示中文名 + 类别图标（不是 `#1234`）
3. 穿戴后属性/槽位正确
4. 新增道具 / 技能同理
5. 连续新建两条，ID 为 max+1、max+2
6. 重启后定义仍在

## 边界

- 特殊协议道具（喇叭、扩容、宝石孔等）仍需改码
- 自定义技能特效复用已有 `effect_index`
- 删除前检查背包 / 装备栏 / 仓库 / 已学技能引用
- **不改写** `item.dsh` / `equip.dsh` / `skill.dsh`；CBE 本阶段不保证可见
