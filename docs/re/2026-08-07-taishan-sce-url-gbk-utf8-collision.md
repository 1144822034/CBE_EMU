# 泰山 SCE HTTP 404（`%CC%A9%C9%BD`）

日期：2026-08-07

## 现象

`http://localhost:5173/JHOnlineData/07%CC%A9%C9%BD_02.sce` → 404。  
正确 UTF-8 路径 `07%E6%B3%B0%E5%B1%B1_02.sce` 本可 200。

## 根因

`泰山` 的 GBK 字节 `CC A9 C9 BD` **恰好是合法 UTF-8**（U+0329 / U+027D）。  
`PixiJS_web/src/net/gbk.ts` 的「UTF-8 round-trip 优先」把线上海报场景名解成这两个组合字符，`encodeURIComponent` 再发出 `%CC%A9%C9%BD`，Vite 按 Unicode 文件名找不到 `07泰山_02.sce`。

桃花岛等其它图的 GBK 不是合法 UTF-8 round-trip，所以只有泰山等少数名字中招。

## 修改

1. `decodeGbk`：高字节存在时，若 GBK 解出汉字而 UTF-8 没有 → 用 GBK；真 UTF-8 汉字仍走 UTF-8。
2. Vite `/JHOnlineData`：对 Latin-1/GBK 百分号路径与 `07_02` 指纹做回退解析（兼容旧错误 URL）。
3. `resources/binary.ts` 改为 re-export 同一 `decodeGbk`。

## 验证

刷新（必要时重启 `npm run dev`）后进泰山；Network 应为 `07%E6%B3%B0%E5%B1%B1_02.sce` 且 200。
