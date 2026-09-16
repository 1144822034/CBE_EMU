# 重铸后源装备洗练穿戴复活（双份洗练）

Date: 2026-09-15

Status: fixed

## 触发与现象

1. 背包已洗练装备 A（`+L`，红字）对同槽同品质 `+0` 装备 B 做「重铸」（强化转移 `29/6`）。
2. 重铸后：A 强化→0、洗练显示默认/空；B 强化→L 并带走洗练（内存正确）。
3. 再穿戴 A（或穿脱/合并触发 coalesce）后：A 洗练又变回重铸前红字。
4. 结果：A、B 两件装备拥有同一套洗练属性。

## 业务链路

1. 客户端 `1/29/6 { seqd, seqs }` → 服务端扣费，源 `enhance=0`、目标 `enhance=L`，
   `enhance_wash_copy(dest, source)` + `enhance_wash_clear(source)`，同包 `7/7+7/11`。
2. 穿戴/互换/卸载：`enhance_wash_coalesce_from_record` — 仅当内存 wash **为空** 时，
   从 `account_role_enhance_wash_record` 按 `item_instance_id`（再退 `bag_seq`）回填。
3. 洗练确认 `29/14`、穿脱等路径会 `upsert`/`relocate` 该表；重铸此前未写。

## 首次偏离

`29/6` 成功路径改了背包行内存 wash，但：

- **未** `delete` 源实例的 wash-record；
- **未** `upsert` 目标实例为转移后的 wash。

故源行内存为空时，穿戴 coalesce 把「重铸前快照」写回源装备；目标内存已有转移 wash，
两侧同词条 → 双份。

客户端次要偏差：`applyEnhanceTransferBagLevels` 只清源 `enhanceAttrs`、不把词条拷到目标，
且未清 `pendingBagEquipEnhance`，会话内也可能短暂/竞态复活旧洗练。

## 根因

强化转移与洗练兜底表的契约不一致：内存 wash 已转移，record 仍挂在源
`item_instance_id`/`bag_seq` 上，`coalesce_from_record` 在空内存时合法回填，违反
「洗练随强化转移、源座清空」的契约。

## 修改

### 服务端 `29/6`

1. 转移前对源 `coalesce_from_record`（空内存也能带走 record 快照）。
2. copy/clear 后：`enhance_wash_record_delete(源 instance + bag_seq)`。
3. 目标非空 → `upsert`；目标空 → `delete`（避免目标旧 record 日后复活）。
4. 立即 `role_db_save("equipment-enhance-transfer")`（失败则 dirty），与 `29/14` 一致。

新增 `vm_net_mock_enhance_wash_record_delete`（`mock_server_enhance_config.inc.c`）。

### 客户端

`applyEnhanceTransferBagLevels`：源 attrs 拷到目标（无则清目标）；删除两侧
`pendingBagEquipEnhance`。

## 验证

- [x] `make -j2`
- [ ] 洗练红字 A `+L` 重铸到 B `+0`：确认后 A 无红字、B 为原洗练；穿戴 A 再卸下仍无该洗练。
- [ ] 重登后 A 仍无、B 仍有；不会出现两件同洗练。
- [ ] 源仅默认档（无洗练 record）重铸：目标不继承虚假红字；源不因 bag_seq 回填出旧数据。
