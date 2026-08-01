# 组队退出商城闪退（切磋后买复活石）

```text
phase: team online -> spar done -> mmShop buy 801 -> exit mmShop -> mmGame 5/10
trigger: 组队状态下进出商城（日志为切磋后买复活石再退出）
request_shape: shop-open arms shopSceneNpcReseedPending; exit emits 5/10+7/7 type=1
              before shop-return WT6/1 / 30/1
observed: mock_team_groupinfo subtype=10 members=2 ... builtin-group-type1 resp=521;
          无 shop_return_rehydrate / shop_return_scene_enter；随后闪退
client_parser: scene_draw_team_member_status_list 0x01014168 -> null avatar +24
               at 0x01014388 (same class as team-roster HUD crash)
```

## 首个偏离

买石本身正常（`map_revived=0`、`hp=342/342`，仅入库）。闪退发生在**退出商城**
重建 mmGame 时：

1. `shopSceneNpcReseedPending` 仍武装；
2. 客户端先发 `5/10 + 7/7(type=1)`；
3. 服务端回**完整双人** `5/10`（`members=2`）；
4. 新壳尚未走 shop-return `30/1`，HUD 绘制队友头像 → 空回调崩溃。

这不是 801 / 复活确认契约问题（`revival_confirm_pending=0`）。

## 根因

`builtin-group-type1` 在商城返回完成前无条件下发全队 `5/10`。`5/10` 只
`AddRoleToList`；非本机行会进组队 HUD。mmShop→mmGame 壳在场景进入完成前没有
可用头像资源回调。

## 修改

1. `shopSceneNpcReseedPending` 且仍在队：`5/10` 只回本机一行（登录形态），
   置 `shopReturnTeamPeersPending`。
2. `finish_shop_return_rehydrate`：若该标志置位，为每名队友排队 `5/5`
   （与邀请同意增量同路径），再清标志。

## 验证

1. 两人组队；任一方进商城买 801（满血）再退出。
2. 日志：`mock_group_type1_shop_return_solo_roster defer_peers=1` →
   `shop_return_rehydrate` → `shop_return_team_peers_queue peers=1` →
   `team_member_join_deliver ... update=5/5`。
3. 不闪退；组队 HUD 恢复两人；可继续切磋/遇怪。
4. `make -j2`。
