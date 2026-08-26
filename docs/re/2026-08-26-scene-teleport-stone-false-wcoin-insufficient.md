# 场景传送石错误显示“酷宝不足”

## 触发与首个偏离

在场景内触碰 `n_telestone.actor`，打开 `WT 1/16/1` 目的地列表后选择一项。
运行中服务端的实际记录为：

```text
mock_teleport_stone_list entries=15 role=10093 item800=94 wcoin=4840
net_send wt=16/1 ... source=builtin-teleport-stone-list
mock_teleport_stone_transfer subtype=2 exit=43 ... response=16/2-confirm-target resp=79
net_send wt=16/2 len=64 source=builtin-teleport-stone-transfer
```

因此角色同时持有传送石与酷宝；提示不是余额或背包库存不足。首个错误状态是服务端
对单独的 `1/16/2` 场景传送石选点返回了 `1/16/2 {result=2,scene,posinfo,exitid}`。

## 客户端契约

`mmGameMstarWqvga.cbm:sub_11CE(0x11CE)` 将 `16/2.result=2` 固定解释为原生
“酷宝不足，请进入商城充值”分支。该对象没有 `value`，因此界面同时把错误的确认状态
显示成“传送石 x0”。这两个文案都不是服务端实际扣款的结果。

已由同一客户端 parser 验证的场景传送石直接进入契约为：

```text
1/16/2 { result:u8=1, scene, posinfo, exitid }
```

客户端经 `sub_11CE -> sub_BCC` 进入目标场景。其他 `16/2` 直达入口可发送运行时
`16/3(type=0)`、`27/11` 与 `7/42`，但本次场景传送石选点的实机流只有裸
`27/11 + 7/42`。`16/3 result=2` 虽然是具名入口和任务传送的有效结果包，但它会被
`sub_11CE` 解释为充值失败，不能复用。

此前错误地把其他直达入口的 ACK 形状套用到场景传送石。最新运行表明，即使选点已正确返回
`16/2 result=1`，客户端依然不发送该 ACK；现有对象流处理器为避免把普通目录请求误识别为传送，
正确地拒绝此裸流并返回空包，客户端于是停留在“场景加载中”。这不是资源文件缺失；最早的未完成
边界是已选择场景的目录请求未被服务端接住。

```text
mock_teleport_stone_transfer ... response=16/2-result1-direct-no-cost
unhandled wt=27/11 ... first=1/27/11:0,1/7/42:0
net_send ... source=ignored-unhandled-server-only resp=0
```

场景传送石目录本身不定义任何货币或物品费用。世界地图的独立 `16/4` 购买确认链才
使用 `value=1`，并在客户端本地消费一枚 `itemId=800`；本次不修改该链路。

## 修复

`vm_net_mock_build_teleport_stone_transfer_response()` 现在只在以下严格条件下走免费直达：

1. 请求为独立的场景传送石 `16/2`，而非已存在 `16/4` 确认目标的世界地图后续；
2. `exitID` 能从资源驱动、SCE 石头锚点与精确 `sMap.dsh` 行组成的目录中解析；
3. 目标位置通过现有场景安全落点逻辑取得。

该分支返回 `16/2 result=1`，保留选定目标为待完成的 direct-enter 状态，明确记录
`item800_cost=0 wcoin_cost=0`。其后只在以下状态同时成立时接住裸目录流：目标仍待完成、
当前角色场景精确等于目标、请求恰好为一个空 `27/11` 后接一个空 `7/42`。响应只包含这两个
客户端请求的目录对象，并保留目标供后续 `WT6/1` 以既有 `30/2(no-posinfo)` 完成加载。
它不伪造库存变更、不扣酷宝，也不写客户端内存。

保留的世界地图分支仍是：

```text
16/4 {result=0,value=1} -> 客户端确认/本地消费 item 800 -> 16/2 + 16/3
```

## 验证

- `make -j2`：客户端和服务端均已编译、链接成功。
- `tmp/teleport-stone-scene-catalog-regression.exe`：通过。该回归从生产 SCE/sMap
  资源重建 15 个目的地，使用太乙峰 `exitID=90` 构造真实形状的 `16/2` 选点，断言响应
仅为 `16/2 result=1`、目标场景和石头锚点正确、且没有 `value` 费用字段；随后构造本次
实机形状的裸 `27/11 + 7/42`，断言仅在待完成的场景传送石目标下返回对应目录对象，并保持
目标状态供后续资源完成请求消费；最后构造真实 39 字节 `WT6/1`，断言响应包含一次
`30/2(no-posinfo)`，并清除该 direct-enter 待完成状态。

仍需人工画面验收：以任意角色在场景传送石选择目的地，应直接进入所选场景，不出现
“酷宝不足”或“传送石 x0”，不再停在“正在更新资源文件”，且传送石与酷宝余额均不改变。
