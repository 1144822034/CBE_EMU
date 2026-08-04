# 2026-08-04 普攻 pierce 软上限

## 触发

32 级幻剑攻击力约 218，低防怪普攻飘 587+（约 3×攻击）。根因是
`damage_after_defense` 在软减伤后再加满额 `(atk-def/2)*2`。

## 变更

pierce 改为渐近软上限，趋向 `attack * 25%`：

```text
pierceRaw = (attack - defense/2) * 2
pierceCap = attack * 25 / 100
pierce    = pierceRaw * pierceCap / (pierceRaw + pierceCap)
damage    = soft + pierce
```

| 场景 (atk=218) | 旧伤 | 新伤（约） |
| --- | ---: | ---: |
| def=0 | 654 | 266 |
| def=20 | 597 | 229 |
| def=200 | 312 | 120 |

低防普攻约 **1.0～1.25×攻击**；高防仍明显低于攻击。

## 修改点

- `src/server/mock_server_role.c`：`vm_net_mock_damage_after_defense`
- `docs/re/2026-06-28-player-attribute-model.md`

## 验证

- `make -j2`
- 同角色打低防怪：飘字应从 ~3×攻击落到略高于攻击
- 打高防怪：伤应明显低于打低防怪
