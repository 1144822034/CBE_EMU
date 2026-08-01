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

| type | 排序/分数字段 | myorder 语义 |
| --- | --- | --- |
| 0 | `level` | 名次（UI：当前排名:第） |
| 1 | `money` | 名次 |
| 2 | `account_roles.pk_points` | PK 点数（UI：PK点数为:） |

`pk_points` 默认 0；切磋胜负如何累加仍 `unresolved`，但不再回退到 money。

## 5. 修改

- `builtin-ranking-board23` 强制 `ordernum=3` 并发送 `orderlist`
- 每条 orderlist：`seq_put_u8(index)` + `seq_put_string(tab)`
- topplayerinfo 保持 tagged i32 + 两个字符串
- 启动/查询时 `ALTER` 兼容补齐 `account_roles.pk_points`
- type0/1/2 分别用 level/money/pk_points 排序与显示
