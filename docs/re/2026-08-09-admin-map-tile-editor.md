# 2026-08-09 后台通用地图编辑器

## 背景

管理端原先只能预览场景 BMP、克隆场景、改传送点/刷怪，不能改 `.map` 地砖格子。
本功能在「游戏内容管理」上增加通用地图编辑页，用 JHOnlineData 现有 tile GIF
画砖与改碰撞，写回权威 `.map` 并 publish。

不包含比武大会模板或新美术；截图级红毯/战鼓素材在资源包中不存在。

## 入口

1. 启动 mock 服务，打开 `http://127.0.0.1:<admin-port>/admin-418yz6/`
2. 「游戏内容管理」选中场景 → 预览标题旁「编辑地图」
3. 或直接 `/?tab=mapedit&scene=<场景名>`

## 能力

- 加载当前场景 SCE2 内嵌 `.map`（宽高 / tile 尺寸固定）
- 画砖、矩形填、吸管、碰撞笔刷（`0x0` / `0xF` / 定向 N/W/S/E）
- **加减 tile 图集**：候选来自全服现有场景 `.map` 引用过的 GIF（最多 16 张）
- 显示碰撞叠加
- 保存：LZSS type-2 写权威 `.map` → publish 场景+map+引用 GIF → 清 BMP 预览缓存
- 若其它场景共享同一 `.map`，页内黄色警告

## 图集加减

1. 左侧「从现有场景图集添加」为可筛选缩略图列表（候选=全服场景 `.map` 用过的 GIF）
2. 点击条目预览大图；点条目上的 **添加** 或预览区 **添加此图集** 加入调色板
3. 图集标题旁 **移除**：引用该 sheet 下标的格子会改成 sheet0/tile0（保留碰撞 flags）；其后 sheet 下标前移
4. 至少保留 1 张图集；上限 `VM_MOCK_ADMIN_PREVIEW_IMAGE_MAX`（16）

## API

| 路由 | 作用 |
|------|------|
| `GET /map-edit.json?scene=` | 地图元数据 + `cellsB64` + `imageNames` + `availableSheets` + `sharedBy` |
| `GET /tile-sheet.bmp?name=` | 解码权威目录 GIF → BMP 调色板 |
| `POST /action` `save-scene-map` | `scene` / `map_name` / `cell_count` / `cells_b64` / `image_names`（换行分隔 UTF-8 GIF 名） |

`map_name` 必须等于该场景 SCE2 解析出的叶子。`image_names` 中每一项必须出现在现有场景图集目录且权威目录存在对应 `.gif`。

## 格式契约

与 `PixiJS_web/src/resources/map.ts` / 场景预览一致：

- cell packed LE u32：tileIndex + (sheetIndex<<24) + (flags<<28)
- flags：`0x1` N / `0x2` W / `0x4` S / `0x8` E / `0xF` 实体
- 格子顺序 column-major：`index = gridX * rows + gridY`
- sheetIndex 仅低 4 bit，故最多 16 张图集

## 实现落点

- `src/server/mock_server_equipment_npc.c`：`vm_net_mock_write_map_resource_bytes`
- `src/web_admin_server.c`：图集目录收集、JSON/BMP/保存、`tab=mapedit` 与 `admin.js`

## 验证清单

1. `make -j2` 通过
2. 打开 `36节日庆典_01` → 编辑地图 → 从下拉添加如 `m_castle1.gif` → 调色板出现新 sheet
3. 用新 sheet 画几格 → 保存 → 预览可见新砖
4. 移除一张未在用/在用的图集 → 格子重映射正确 → 再保存
5. 客户端重进场景拉到新 `.map` 与 GIF
6. 共享 map 场景仍有 `sharedBy` 警告

## 风险

- 共享 `.map` 的多场景会一并受影响
- 不改地图尺寸 / SCE 摆件（传送与刷怪仍用原表单）
- 不能导入资源包外自定义 GIF；只能复用现有场景已用过的图集
- 单次 POST 受 `VM_MOCK_ADMIN_REQUEST_MAX`（约 96 KiB）限制
