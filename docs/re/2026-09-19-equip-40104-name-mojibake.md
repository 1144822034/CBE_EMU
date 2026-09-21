# 装备 40104 名字乱码（飓风布靴 → 쫷粼ѥ）

日期：2026-09-19

## 现象

背包/装备详情里物品 **40104** 名字显示成 `쫷粼ѥ`，不是 `飓风布靴`。同系列 40090–40103（飓风巨剑、飓风皮靴等）正常。

## 根因

`PixiJS_web/public/JHOnlineData/equip.dsh` 第 40104 行名字列字节是 GBK `EC AB B7 E7 B2 BC D1 A5`，即「飓风布靴」。

这 8 个字节同时是合法 UTF-8，且含一个汉字 U+7CBC（粼）：

| 字节 | UTF-8 | 实际字符 |
|------|--------|----------|
| `EC AB B7` | U+CAF7 | 한글 쫷 |
| `E7 B2 BC` | U+7CBC | 粼 |
| `D1 A5` | U+0465 | 西里尔 ѥ |

`decodeGbk` 原先在「GBK 有汉字、UTF-8 没有」时才回退 GBK（泰山 `CC A9 C9 BD`）。40104 的 UTF-8 视图**有**汉字，于是走了 UTF-8，名字被画成 `쫷粼ѥ`。

`item.dsh` / `skill.dsh` 以及其余 `equip.dsh` 行没有第二条同类碰撞。

## 修改

`PixiJS_web/src/net/gbk.ts` `decodeGbk`：UTF-8 往返成功且含汉字时，若该视图还混有非汉字文字（谚文、西里尔等），改用 GBK。纯 UTF-8 汉字（`你好`、`07泰山_02`、`等级：55`）仍走 UTF-8。

`src/server/mock_server_core.c` `vm_net_mock_gbk_label_to_utf8` 同步同一规则（管理端标签，不画背包名）。

## 验证

- `decodeGbk(EC AB B7 E7 B2 BC D1 A5)` → `飓风布靴`
- 泰山 GBK `CC A9 C9 BD` 仍为 `泰山`
- UTF-8 `你好` / `07泰山_02` / `等级：55` 不被改判成 GBK
- `font_16_sky.uc3`、`gb16.uc2.uc3`、`font_gb.uc3` 都有飓/风/布/靴字形
- `make -j2` 已重编 `bin/jh-online-server.exe`

## 仍未知

无。背包名来自本地 `equip.dsh`，不依赖服务端另发名字。
