# 重铸关闭转移洗练时原装备词条被清空/像再随机

Date: 2026-10-10

Status: fixed

## 触发与现象

1. 管理后台「重铸是否转移洗练属性」= 关。
2. 已洗练源装备 A（`+L`）对目标 B 做重铸（`29/6`）。
3. 确认后 A 强化归零，其洗练词条变成默认档（或看起来像重新随机），而非保留原洗练。

## 根因

`transfer_wash_enabled=0` 时 `apply_wash` 误执行「清空源洗练 + 删 wash_record」。
源 `enhance→0` 后 common-extra 按空 wash 回退配置默认里程碑，体感即「原装备词条又变了/再次随机」。

正确契约：「不转移」= 只搬强化等级，两侧 `enhance_wash` 各自不动。

## 修改

- `mock_server_equipment_npc.c`：`transferWash=0` 时 `apply_wash` 直接 return。
- `GameSession.applyEnhanceTransferBagLevels`：`washxfer=0` 时保留源 `enhanceAttrs`。
- 后台选项文案与 `washxfer` 注释对齐。

## 验证

- [ ] 关转移洗练：A 洗练红字在 `+0` 仍在（灰显）；B 原洗练不变；仅 `enhance` 从 A→B。
- [ ] 开转移洗练：仍为源→目标拷贝并清源。
- [ ] `make -j2`
