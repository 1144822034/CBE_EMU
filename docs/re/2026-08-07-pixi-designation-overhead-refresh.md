# Pixi 装备称号后头顶不刷新

## 症状

称号列表装备成功（★ 切换），玩家头顶称号 GIF 仍为旧值或空白。

## 根因

进图时 `onEnterScene` 用 `session.roleTitleGif` 调一次 `SceneView.setPlayerNameplate`。

`1/23/3` 成功响应带同包或后续 `23/2`（`designationinfo`：actorId + shortTitle + `riches_nameN.gif`/`level_nameN.gif`），Pixi 虽能解析，但未写回 `roleTitleGif`，也未再次刷新 nameplate。

## 修复

- `parseDesignationSceneUpdate`：解析 `23/2` entry 流。
- `GameSession.selectDesignation`：跨 body 合并 `23/3` ack 与 `23/2`，更新 `roleTitle`/`roleTitleGif`，回调 `onDesignationUpdate`；必要时用刷新后的 `23/1` 行补全 gif。
- `main.onDesignationUpdate` → `view.setPlayerNameplate` + `hud.setIdentity`。
- 允许装备 id=0（一贫如洗）。

## 验证

装备任意已解锁称号后，头顶 badge 应立即变为对应 `riches_name*` / `level_name*` GIF。
