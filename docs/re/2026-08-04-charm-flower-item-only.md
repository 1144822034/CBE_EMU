# 2026-08-04 魅力仅由道具（花）增加

## 现象

属性页 realign 后：天机魅力显示约 80，幻剑/鬼道为 0。

## 根因

服务端曾用占位公式播种 `EXTRA132`（→ `actor+0x84` 魅力）：

```text
charm = jobCharmBase(level) + money / 100000
```

天机高金钱/等级角色因此显示非零；未走完整 actorinfo 或 +0x84 仍为 0 的角色显示 0。  
与产品契约不符：**魅力只应通过使用花类道具增加**。

## 修复

- `vm_net_mock_role_charm`：默认返回 **0**（`CBE_ACTOR_CHARM` 可测）
- `build_player_stats` / actorinfo `EXTRA132` 不再加等级表或金钱
- 成长表文档去掉 charm 列；见 `2026-06-28-player-attribute-model.md`

## 仍未知

- 花道具 ID、使用请求、单次加值、是否封顶、角色库持久化字段
- 客户端局内升级对 `+0x84` 的 flat 加成与「仅花」契约的最终对齐方式

## 验证

- 三职未用花：魅力行均为 0；力/敏/智仍按职业成长
- `make -j2`
