# 2026-08-29 Pixi 好友列表删除好友

phase: pixi-friend-delete  
status: implemented

## Contract

| 方向 | WT | 字段 | 说明 |
|------|-----|------|------|
| 请求 | `1/10/9` | `id:u32` | 目标角色持久 id |
| 应答 | empty WT | — | 与 `10/3` 同为 fire-and-forget |
| 后续 | `1/10/1` | 分页 | 客户端删后刷新当前好友页 |

证据：

- CBE 菜单/确认文案：`删除好友`、`确定将c1%sc0从好友列表中删除？(删除好友将同时清除友好度)`
- 发包：`JianghuOL.CBE:0x0101A43A`（`kind=10 subtype=9` + 字段 `id`）

## Pixi

- 好友列表操作菜单增加「删除好友」→ 确认框 → `friendDelete` → `10/9` → 再 `openFriends`
- 服务端从 `friendships` 双向删除该对关系

## Verify

- [ ] 好友列表选人 → 删除好友 → 确定 → 行消失且友好度清除
- [ ] 取消确认不发 `10/9`、列表不变
- [ ] `npm run smoke:protocol` 含 `10/9` 断言；`make -j2` 通过
