# 登录背包空：满包 30/21 iteminfo 溢出导致 5/10+7/7 整包失败

日期：2026-07-26

## 触发与现象

账号 `AAA2008DNF` / 角色 `27`：MySQL `account_role_backpack` 有 45 行且
`backpack_item_count=45`，但进游戏背包看不到物品。

日志：

```text
mock_backpack_grid_reseed role=27 reason=title-role-select next=group-type1-30/21
unhandled wt=5/10 len=35 objects=1 first=1/5/10:11,1/7/7:10
source=ignored-unhandled-server-only response=0
...
mock_backpack_items role=27 capacity=45 rows=45 stored_rows=45 iteminfo_len=768
```

选角已武装 `30/21` 播种，但随后的登录复合请求 `5/10 + 7/7(type=1)` 返回空包；
场景 followup 里的 `17/1` 仍能编出 45 行（768 字节），说明内存背包有数据，缺的是
登录网格播种。

## 根因

`seq_put_*` 使用带 tag 的编码（`u32=6`、`i16=4`、`u8=3`）：

| 对象 | 每行 | 45 行 |
| --- | --- | --- |
| `17/1` | itemId+extra = 17，外加 rowcount u8 tag = 3 | **768**（&lt; 1024） |
| `30/21` | itemId+seq+count+extra = **27** | **1215**（&gt; 1024） |

`append_backpack_grid_object` 使用 `u8 itemInfo[1024]`，`build_backpack_grid_iteminfo_blob`
写满失败 → `append_backpack_role_grid_main_objects` 失败 →
`build_group_type1_response` 返回 **0** → 客户端从未走
`HandleItemGridResponse(0x01039952)`，主物品管理器为空。

首次偏离：服务端 30/21 编码缓冲区过小，不是库表丢数据。

## 修改

- `VM_NET_MOCK_BACKPACK_ITEMINFO_SCRATCH = 8 + MAX_ITEMS*27`，供 `17/1` 与 `30/21` 共用。
- 编码失败打 `mock_backpack_grid_encode_failed` /
  `mock_group_type1_backpack_seed_failed`，禁止再静默 `response=0`。

## 验证

1. `make -j2`，重启服务。
2. 同账号重登：应见 `builtin-group-type1` 与
   `mock_backpack_grid role=27 gridnum=45`，**不应**再出现
   `unhandled wt=5/10`。
3. 客户端背包应显示库内物品（满包 45/45）。
