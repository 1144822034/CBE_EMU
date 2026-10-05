# CBE 刚进游戏闪退：登录 30/21 / 7/7

Date: 2026-10-05

Status: implemented

## 现象

选角进 `c04临安府_04` 后立刻 `vMAssert(MMORPG_Screen_InGame.c:913)`。
`pc=c000440` `lr=5016e99`（mmGame，不是 CBE `0x01039952`）。

## 证据

三种 `30/21` extra 都崩在同址，栈上两次 `0x5cd`（1485）：

1. compact extra（resp=972）
2. Pixi 原样透传（resp=1869）
3. 去掉 30/21 宝石 trailer（resp=1700，`mode=cbe-no-gem-trailer`）

登录包固定为 `5/10,10/26,30/21,7/7,7/20,7/32`。

## 根因

`lr=5016e99` 在 mmGame。`30/21` extra 怎么改断言都不变，稳定触发点是同包
`1/7/7`：`sub_D04` 读 `seq + itemId + count + extra`。Pixi 在 attr 后追加
`0x47` 宝石 trailer；mmGame 在 attrCount 停读时，trailer 变成下一格 itemId，
DSH 查找失败 → `Screen_InGame.c:913`。

`30/21` 仍应按 HandleItemGrid 去掉 trailer（避免 CBE 网格错位），但不是 913 的
稳定原因。

## 修改

`network-client.c`（及 JNI）：

- `30/21`：u8 gridnum、去 `0x47`、flag 只留 bit0
- `7/7`：同样去宝石 trailer 后按 sub_D04 重写

## 验证

1. 重启 `bin/main.exe`
2. 日志应有 `cbe_backpack_clamp object=7/7` 与 `cbe_77_row`，不应 913
3. `make -j2`
