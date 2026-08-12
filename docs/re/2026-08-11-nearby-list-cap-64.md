# 周围玩家列表：去掉 3 人上限

## 现象

同图超过 3 个其他玩家时，`周围玩家` 列表与地图可见 peer 最多只出 3 个。

## 根因

`mock_server_guild.c` 里 `VM_NET_MOCK_SCENE_NEARBY_ROLE_MAX = 3`，
`vm_net_mock_build_scene_role_seeds` 采种封顶；`2/7` / `2/10` 共用该上限。
`2/7` 的 `otherInfo[512]` 也偏紧，多人时容易装不下。

## 修改

- `NEARBY_ROLE_MAX`：**3 → 64**（同图 peer 种子上限）
- 占位偏移表改为循环取模，不再绑死在 MAX=3
- `2/7` / `2/10` 的 `otherInfo` / `plus16Info` 缓冲区加大（8192 / 512）
- Pixi `SocialPanel` 本就按 `LIST_PAGE_SIZE=10` 本地翻页；服务端 page0 一次下发全量缓存（`allpgs=1`）

## 验证

1. `make -j2` 通过
2. 同图 ≥4 人：列表人数应等于同图其他在线角色（≤64）；↑↓ 翻页
3. 地图上也应能看到多于 3 个 peer（同 cap）
