# NPC 装备商人信息与二次确认

日期：2026-08-17  
状态：已实现并完成服务端隔离回归

## 现象与最早缺口

动态 NPC 的装备购买与装备回收都使用 `WT 1/26/1` 对话选项。购买列表已经从
`equip.dsh` 生成了基础属性说明，但回收列表只下发 `名称 +强化等级`，因此光标移动时
没有可用的装备属性信息。

同一列表的购买值为 `0xe9000000 | itemId`，回收值为
`0xee000000 | backpackSeq`。两条路径此前在第一次 `action=1` 点击时直接提交角色变更，
这是二次确认缺失的第一处业务契约违反点。

## 客户端与协议证据

- `江湖OL.CBE:task_hall_activate_selected_entry (0x010492B0)` 对选项
  `action=1` 发送 `WT 1/26/1 { type=2, id=value }`。
- `江湖OL.CBE:ParseNPCDialogData (0x010380E8)` 解析 `1/26/1` 中的
  `dialog`：`kind, text, option-count, display-type, name, action, value,
  description, button-count`。每项 `description` 有客户端 `0xC8` 字节缓存，
  是光标选中时的可见信息来源。
- 因此没有发现需要或允许另造“商店确认”回调。确认步骤仍下发普通 `26/1` 对话，避免把
  `7/7`、`17/1` 或系统消息对象混入 task-hall 回调。

## 实现

交易流程现在为：

```text
商品/装备行 action=1
  -> 26/1 {确认, 取消}
  -> 确认 action=1 才执行原有角色持久化交易
```

- 首次点击仅写入当前客户端会话的 `npcTransactionContext`，记录角色、场景、NPC、已下发
  服务掩码、物品 ID/背包序号、分类页和报价，不扣钱、不增加物品、不删除装备。
- 确认值为私有的 `0xf0` 命名空间，取消值为 `0xf1`。两者仍由已有的严格
  `type=2` detector 和 `26/1` parser 链路承载。
- 确认或取消都会消费该上下文；新 NPC 对话、任何非确认服务选择、角色/场景/NPC/服务掩码
  不匹配也会使其失效。确认时重新查询商品或背包装备，并核对当前报价与初始报价，随后才
  执行既有持久化逻辑。
- 商品翻页请求的页码在 `OPEN_CATEGORY_BASE` 的 `value` 高字节中，但点击商品时客户端只
  发送商品 ID；回收行同样只发送背包序号。此前首次点击把这两类确认上下文的 `page` 默认为
  `0`，所以取消或确认成功后的列表总是第一页。现在服务端分别按权威商品目录序号和背包
  装备序号反推出 `ordinal / 5`，取消和确认成功都使用该页重建列表；日志新增 `page=` 字段
  记录实际恢复页。
- 回收行复用购买行的 `equip.dsh` 基础属性格式化函数，并附加实例的“强化+N”。资源异常而
  没有目录装备行时，保留原有的名称与强化等级回退说明。
- 确认页主文本此前仅显示“购买确认/回收确认”和物品名。客户端确实解析了选项
  `description`，但确认页布局只绘制主文本，详情槽位没有进入该页面的可见区域，这就是
  “确认窗口只有标题”的首个可观察偏离。现在服务端在生成物品详情后，将同一份详情追加到
  主文本：`标题\n等级\n每条基础属性/效果/强化\n价格`；药品使用
  `等级\n每条恢复效果/时效\n价格`。列表选项仍保留原有 `description`，因此列表光标详情和
  确认正文使用同一份数据；每个字段之间使用客户端已有任务/对话正文支持的单字节 LF。

- IDA 证据：`DrawNPCDialogMenu (0x01013D46)` 对正文和选项说明都调用
  `DrawMultiLineTextSer (0x01034C32)`；其实现 `RenderMultiLineText (0x01034AC8)`
  对 `0x0A`/`0x0D` 直接增加行高并从下一字节绘制。`ParseNPCDialogData (0x010380E8)`
  仅按字符串长度复制说明，不把 LF 当作终止符。因此服务端应发送实际 LF 字节，而不是
  两个可见字符 `\\` 和 `n`，也不应使用 CRLF 造成重复换行。

## 验证

已执行：

```text
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 \
  -ffunction-sections -fdata-sections \
  scripts/npc-equipment-confirmation-regression.c \
  obj/server/gifDecode.o obj/server/mystd.o obj/server/mysql-client.o \
  obj/server/md5.o -Wl,--gc-sections \
  -o tmp/npc-equipment-confirmation-regression.exe \
  -lpthread -liconv -lm -lkernel32 -lws2_32
tmp/npc-equipment-confirmation-regression.exe
```

隔离回归断言商品/回收页码均能从第 6 条记录恢复为第 2 页，首次点击不改变角色，确认上下文只可消费一次，
错误 NPC 上下文被拒绝并清除，并且装备描述同时包含等级、基础属性和强化等级，药品描述按效果逐行输出，确认主文本也包含该详情与价格。实际客户
端验收需覆盖武器/防具购买、药品购买、装备回收、取消、确认以及确认前重新打开其他 NPC
的失效路径。
