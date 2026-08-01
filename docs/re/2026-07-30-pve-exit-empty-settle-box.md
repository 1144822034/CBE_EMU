# PvE 结算离场空白提示框 / 结算点不动

Date: 2026-07-30

Status: **rolled back** skip-4/8 default — 非挂机恢复 `4/8+4/11+4/9`

```text
phase: victory 4/7 -> panel -> exit 4/8+4/11 type0+4/9
trigger: 手动战斗结算概率点不动（可不出现操作菜单）
```

## 已失败试修

| 试修 | 结果 |
|------|------|
| `25/12` 清条 | 失败 |
| `fdata=战斗胜利` + `panel_ms=2500` | 失败（日志 `fdata_len=8` 仍空框） |
| 默认 **skip `4/8`**（仅 `4/11+4/9`） | **失败**：结算壳未拆时 `type=0` 挡点击，概率卡死且不一定看得见操作栏 |

用户描述：首场**结算界面退出之后**才出空框 → `4/8` UpdateCharAttrs
刷空壳；另有手动结算**概率点不动**（无可见操作菜单）。

## 当前契约

1. 非挂机 `settlement_exit` 默认 **`4/8+4/11 type=0+4/9`**（`mmBattle:0x7DF6`）。
2. 挂机再入仍 empty-skip type0，由下一场 hangup start prepend 拆场。
3. 空框实验：仅当显式 `CBE_BATTLE_EXIT_SKIP_SUBTYPE8=1` 才 skip `4/8`。
4. 离场后约 3s 内 `25/5` scene-default 回空包（`suppress-post-exit`）仍保留。

## 验证

1. 重启服务端；手动打怪胜利：日志  
   `settlement_exit ... response=4/8+4/11+4/9`（**不是** `4/11+4/9` skip trial）。
2. 结算框可点/可自动收起，能走路、能再遇怪。
3. 若空框回潮：另开取证，**不要**再默认 skip `4/8`。

## 相关

- `2026-07-30-settle-operate-menu-stall.md`
- `2026-07-30-post-battle-encounter-cooldown.md`
- `2026-07-28-hangup-settlement-blank-shell.md`
