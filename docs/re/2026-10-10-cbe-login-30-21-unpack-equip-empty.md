# CBE 登录解包错误 + 装备不显示（30/21 交付帽）

Date: 2026-10-10

## 现象

连 live 怀旧区选角进场景后弹「解包错误」，装备栏空。

## 日志（`bin/client_out.txt` 2026-10-10 17:08）

```text
cbe_30_oversized_full_attr len=9213 cap=4096 rows=139 target_rows=61
cbe_backpack_clamp object=30/21 gridnum=61 … iteminfo_len=10473->3155 mode=full-attr-row-cap
cbe_77_row … (8 worn) + cbe_backpack_clamp object=7/7 type=2 worn_shaped=1
cbe_login_bag_order … list=5/10,10/26,30/21,7/7,7/11,7/20,7/31 resp=4199
queue_data … resp=4199 deliver_ms=63
```

主机侧旧缓解（4096 full-attr-row-cap、调序、7/7 去宝石）均已打出，仍复现。

## 根因陈述

触发：登录 group-type1 同包 `30/21` + `7/7 type=2`。  
被违反契约：HandleItemGrid / `event_packet_parse_WT` 必须完整吃完复合包，才能执行 mmGame `sub_D04` type=2 → +104 装备栏。  
首个错误状态：`30/21` 交付仍过大或行内混入 `attr=0` compact → 解包早退（`deliver_ms=63`，远低于历史同档秒级 HandleItemGrid）→ 同包 `7/7` 未生效。  
证据：上表；对照 `docs/re/2026-10-08-cbe-backpack-255-init-patch.md` 续记（compact 3510 仍解包错误；禁止 all-compact）。

已排除：对象数 >10（本包 7）；`cbe_login_equip_peel` / bag-defer（日志无）；7/7 主机改写失败（`type=2` 8 行已打出）。

## 修改

`src/network-client.c`（及 JNI 同名文件）：

1. `VM_CLIENT_CBE_GRID30_DELIVER_CAP`：**4096 → 2048**
2. 行级 full-attr 失败：**丢行**，禁止再写 compact-extra（`extra_fail_skip` 日志）
3. 整包失败：**直接 empty**，删除 `compact-extra-fallback` 路径
4. 仍保留 `cbe_login_bag_order`：`30/21 → 7/7 → 7/11`

## 续记：2048 帽后背包只剩 ~33/139（同日）

### 现象

解包/装备已好，但背包只显示约 33 格（139 的首段）。

### 根因

`full-attr-row-cap` 在 2048 字节内只保留首段行，**丢弃**后续行；`17/1` 只补名称，不能补齐 HandleItemGrid 未播种的格。

### 修订

1. 登录复合包仍只带 **≤2048** 的首段 `30/21`（保同包 `7/7 type=2`）
2. 其余 full-attr 行拆成 complementary `30/21`；**每个溢出块单独 event=7**（同 WT 多 `30/21` 曾出现 139→141）
3. 切分后 `count_rows` 校验 primary/chunk；主包后 `cbe_30_overflow_deliver chunk=` 补种
4. **禁止**整表再发一份（防叠堆）

### 验证（续）

1. 日志：`mode=full-attr-chunk+overflow`、`cbe_30_overflow_arm … evidence=one-30-21-per-event7`
2. 多次 `cbe_30_overflow_deliver chunk=0..`；`primary_rows + overflow_rows` ≈ 139
3. 无 `cbe_30_primary_row_mismatch`；无解包错误；装备有物；背包 **=139**（非 141）
4. 二次选角不卡

## 旁注

本局无 `initMemoryBlock grow 0x14000 -> 0x40000`：主机常以 `VM_MemoryBlock_SIZE=0x400000` 初始化全局池，guest 未必再传 `0x14000`；与本次解包路径无直接因果，另案取证。
