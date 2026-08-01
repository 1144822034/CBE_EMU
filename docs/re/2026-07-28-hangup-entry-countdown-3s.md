# 挂机开战短钟常量（Battle.cbm case11 / sub_263b）

Date: 2026-07-28

Status: **resolved (client hardcoded)** — 服务端无法把短钟改成 3 秒

```text
phase: hangup/challenge start + inline 4/11 type=1
trigger: 进场先闪 1→0，再进 ~19/20 回合钟；希望短钟变 3 秒
binary: bin/JHOnlineData/mmBattleMstarWqvga.cbm
```

## 取证方法

- IDA MCP 当时不可用；用 Capstone 对 `mmBattleMstarWqvga.cbm` 反汇编。
- **磁盘 CBM 相对 IDA VA 有 +1 字节错位**：分析时用 `file[1:]` 对齐后，
  Thumb 偶地址才合法。下文地址均为 **IDA 对齐 VA**（与既有文档
  `0x7cb7` / `0x263b` 一致量级）。
- 产物：`tmp/_short_clock_ida_align.txt`、`tmp/_short_clock_re.py`。

## 调用链

```text
HandleServerBattleCmd  @0x7C70
  switch subtype (cmp #0x0C)
  case 11              @0x7CB6  (docs: 0x7cb7)
    read field "result" ; require == 1
    read field "type"
    strb type -> auto flag
    if type == 1:
      toast「开/关自动战斗」类 UI (r1=0x14,r2=0x27,r3=0xC8)
      movs r0, #8
      bl   sub_263A/263C     (docs: sub_263b(8))
    else:
      movs r0, #0
      bl   sub_263A/263C     (docs: sub_263b(0))
```

## 短钟根因

`sub_263C`（文档 `sub_263b`）按 **mode=`r0`** 写战斗 UI 状态
（`r2 = sb + 0x2918` 一带结构）：

| mode (`r0`) | 写入 `[r2,#3]` | 含义（运行时对照） |
|-------------|----------------|-------------------|
| **8**（`4/11 type=1`） | **`#2`** | 开战自动短钟（体感 1→0） |
| 0（关自动，且非挂机态） | `#0x14` (**20**) | 正常回合钟（体感 ~19） |
| 0（仍挂机/自动态） | `#2` | 仍短钟 |
| 5 | 清 0 | 清倒计时相关字节 |

关键指令（对齐 VA）：

```text
26D2: cmp  r0, #8
26D6: movs r3, #2          ; <<<< 短钟秒数常量
26D8: strb r3, [r2, #3]

2672: movs r3, #0x14       ; 正常回合 20 秒
2674: strb r3, [r2, #3]
```

体感「从 1 开始」：常量是 **2**，首帧/首 tick 后 UI 常显示 **1**，再进 0，
随后其它路径把 `[r2,#3]` 重写成 **20**（~19 读秒）。

## 与服务端旋钮的关系

| 旋钮 | 作用 | 能否改短钟显示 |
|------|------|----------------|
| `CBE_BATTLE_AUTO_ENTRY_GAP_MS` | 推迟首击 synth | **否** |
| `CBE_BATTLE_AUTO_TURN_GAP_MS` | 回合间 cancel/hold | **否** |
| `4/11` 字段 | 仅 `result`/`type` | **无秒数字段** |

`type=1` 路径 **写死** `movs r0,#8` 再进 `sub_263C`；包里没有「倒计时秒数」
可写字段。把 `#2` 改成 `#3`/`#4` 属于 **改 CBM 常量**，违反
`AGENTS.md` / CBE RE skill（禁止 patch 客户端逻辑）。

## 结论

1. 短钟常量已定位：`sub_263C` 在 mode==8 时 `strb #2 → [state,#3]`。
2. 正常回合钟常量：同函数 mode==0 分支 `strb #0x14`。
3. **mock 服务端无法合法把短钟改成 3 秒**；`ENTRY_GAP` 只能推迟出手。
4. 若产品必须 3 秒短钟：只能另寻 **未证实** 的旁路包/模式能写
   `[state,#3]`（当前 `unresolved`），或接受客户端契约。

## 验证建议（只读）

1. 挂机开战：确认仍先 1→0 再 ~19（与 `#2` / `#0x14` 一致）。
2. 不要求改 mock 行为；本条为取证关闭「短钟可用环境变量拉长」假设。
