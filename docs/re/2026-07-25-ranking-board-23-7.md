# Ranking Board 23/7 Hang

Date: 2026-07-25

Status: implemented

## 1. 当前卡点

- 可见现象：点击排行榜后先卡住；补包后闪退。
- 触发方式：场景内打开排行榜。
- 本轮目标：`23/7` 回包可被 parser 与 draw 安全消费。

## 2. 运行时证据

### Hang（未处理）

```text
unhandled wt=23/7 len=34 objects=1 first=1/23/7:25
source=ignored-unhandled-server-only response=0
```

### Crash（ordernum=0）

```text
mock_ranking_board23 ... ordernum omitted/0 resp=205
queue_data ... resp=205
地址无法访问:fffffffc type:0 size:19 value:4
pc:102b37e lr:102b7d5
r2 dump @ state+0x80: +0x10 orderlist array = 0
```

根因：draw `0x0102B332` 在 `ordernum==0` 时走 last-tab 分支，对 NULL `orderlist` 做
`ptr + type*4 - 0x40 + 0x3c`，落到 `fffffffc`。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `江湖OL.CBE` | `SendRankingBoardReq 0x0102AC6E` | 请求 | `1/23/7 {type,pageIndex}`，`+0x38=-1` |
| `江湖OL.CBE` | `HandleRankingBoardResponse 0x0102BA42` | 解析 | 填 `ordernum/orderlist/...`，末尾 `+0x38=1` |
| `江湖OL.CBE` | tab draw `0x0102B332` | 闪退点 | 依赖非空 `orderlist[type]` |
| stream reader | docs + `0x01033B16` | blob 行格式 | `+0x28=i8_tagged`，`+0x20=i32_tagged`，`+0x2c/+0x1c=cstr` |

## 4. 响应契约

```text
1/23/7 {
  ordernum:u8                 // tab 数，必须 > 0
  orderlist: blob             // ordernum * (tagged i8 + len16 name)
  myorder:u32
  colnum:u8
  colnames: blob              // colnum * len16
  pagemax:u32
  count:u32
  topplayerinfo: blob         // count * (tagged i32 + name + score)
}
```

当前 tab：`等级榜` / `财富榜` / `PK榜`（文案仍可能非官方原词）。

| type | 排序字段 | 显示分数 | myorder 语义 |
| --- | --- | --- | --- |
| 0 | `exp` DESC（总经验多者在前；并列 `role_id` ASC） | `level` | 名次（UI：当前排名:第） |
| 1 | `money` | `money`（铜总量） | 名次 |
| 2 | `account_roles.pk_points` | `pk_points` | PK 点数（UI：PK点数为:） |

### Pixi UI（2026-08-09）

- 顶部胶囊「排行榜」+ **分类标签条**（同背包/商城：选中全名、未选首字；←→ 切 type 重拉 `23/7`）。
- 财富榜分数：`floor(money/10000)` + `jin.gif`，不显示银/铜。
- 每页条数：`VM_NET_MOCK_RANKING_PAGE_SIZE=20`（`pageIndex=0` 即前 20 名）；列表 ↑↓ 翻屏。

`pk_points` 默认 0；切磋胜负如何累加仍 `unresolved`，但不再回退到 money。

### 2026-08-12 只显示自己

根因：`ranking_board_mysql_row` 对任一角色名 `HEX` 解码失败（常见于满长
`VARBINARY(32)` 名，解码缓冲只留 31 字节）会把整次查询标 `invalid`，随后
`query.count=0` 并回退成仅当前角色。

修复：单行失败跳过/占位，满容软截断；查询失败才回退自己；`topplayerinfo`
首字段改为 `roleId`。

### 2026-08-12 PK 榜出现铜钱数（如 role8 显示 308476）

库证：`role_id=8` 的 `pk_points=0`、`money=216542`；`308476` 是
`role_id=4` 的 `money`。探测回包确认 **线上 topplayerinfo 分数本身就是错的**
（非 Pixi 错位）：role2=`69`、role8=`308476`。

根因：MySQL 文本协议字段**没有 NUL 结尾**，排行榜回调对 `values[i]` 直接
`strtoul`。`pk_points` 在 SELECT 末列且多为 `"0"`（1 字节），最容易把 recv
缓冲里上一包残留数字吞进去，于是 PK 榜出现等级/铜钱碎片。

修复：按 `lengths[i]` 拷到临时缓冲再解析。

处理（此前）：

- 服务端 type≥2 分数硬编码 `pk_points`，回包增加 `type` 回显
- 客户端丢弃 `type` 与请求不一致的 `23/7`；`beginRankFetch` 防止慢回包盖住新榜

## 5. 修改

- `builtin-ranking-board23` 强制 `ordernum=3` 并发送 `orderlist`
- 每条 orderlist：`seq_put_u8(index)` + `seq_put_string(tab)`
- topplayerinfo 保持 tagged i32 + 两个字符串
- 启动/查询时 `ALTER` 兼容补齐 `account_roles.pk_points`
- type0 按 `exp` 排序、分数仍显示 `level`；type1/2 仍为 money/pk_points
- 响应带 `type` 回显；PK 分数不回退 money
- ranking MySQL 数值按 `lengths[]` 有界解析（修 PK 脏读）
