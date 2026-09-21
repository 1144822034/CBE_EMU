# 梦境地图仍见附近玩家

phase: dream-hide-nearby-peers  
status: fixed  
date: 2026-09-20

## 症状

梦境 `29*` 内地图/「周围玩家」仍能看到其他角色。

## 根因

`append_scene_nearby_role_objects` 已对 `29*` 抑制，但：

1. `2/7` 附近页走 `build_scene_list_otherinfo_blob` ← `build_scene_role_seeds`，**未**抑制；同名梦境图上的其他在线角色仍进列表。
2. Pixi `onNearbyPage` 会把 2/7 列表 `syncMapPeersFromNearbyPage` **画到地图**上。

## 修改

- `vm_net_mock_scene_is_dream_instance_name`（含 `b_29*`）
- `build_scene_role_seeds` 对梦境直接 `return 0`
- 2/7 / nearby append 统一用该判定
- Pixi：进梦清 peer；梦境内丢弃附近页与 peer 绘制

## 发布

须发 **jh-online-server** + **Pixi 客户端**。

## 验证

1. 两账号同进同一 `29*` 叶子 → 彼此地图无对方、「周围玩家」×0
2. 户外仍正常看见附近玩家
