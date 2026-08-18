# 小回春散使用后的常驻进度条

日期：2026-08-18  
状态：implemented，待真实客户端复验

## 触发与首个偏离

背包内使用小回春散后，服务端日志记录了实际的普通物品请求和回复：

```text
mock_item_use item=301 seq=16 count=1 mode=0 ... hp=100/100 ...
response=7/1-use-ok+7/7-type2+7/11-info
```

这里的物品 `301` 是 `item.dsh` 中立即恢复生命的普通堆叠药品。旧回复先下发
`WT 1/7/1 { result=1,type,id }`，随后才下发背包行和数量更新。

`江湖OL.CBE:HandleItemOperationResponse(0x01033544)` 的 subtype `1` 分支在
存在待使用行时，收到 `result=1` 会调用项目管理器更新行，然后固定调用
`ui_show_message_box("使用成功", 0, 0, 10)`。该 UI 不是场景计时提示；没有自动
关闭分支，所以会留下阻塞输入的常驻条。故首个错误状态是服务端把普通回复中的
`7/1` 成功确认误当成安全提示通道，而不是后续 `7/11` 的数量同步。

第一次修改仅移除了 `7/1`；最新真实客户端日志确认它已生效：

```text
mock_item_use item=301 ... response=7/7-type2+7/11-info-nonmodal-use
```

但进度条仍然常驻。这排除了“`7/1` 成功文字框是唯一来源”的假设；也说明
`7/7 type=2` 不是普通药品的正确路径。

## 客户端契约与本轮修复

- `mmGameMstarWqvga.cbm:sub_D04(0x00000D04)` 解析 `7/7`，但普通药品的刷新不应继续伪装成
  `7/7 type=2`。`0x000011CE` 只把 `7/7 subtype=7` 送进 `sub_D04`，不会把 `17/1`
  当成同一回调里的背包刷新。
- `江湖OL.CBE:HandleItemOperationResponse(0x01033544)` 的 `7/4` 分支不弹消息框，
  并清除该次业务等待状态；普通药品的可见背包状态应由同一响应中的全量 `7/11`
  原地更新。`30/21` 是登录/网格初始化通道，不能从已打开的背包界面触发。
  它对 `result=1` 的 `7/1`
  成功弹窗是另一条不同契约，不能混用。

因此本轮只对非储备型（`consumeMode != 2`）的立即 HP/MP 药品使用这个已验证的背包
完成通道，并追加只显示一次的成功提示：

```text
1/7/4  { result=1 }          // 静默完成，关闭操作等待
1/7/11 { info=<row_count,seq,new_count> } // 同 ID 的所有物理堆叠行
1/7/37 { msg="使用成功。", result=1 } // 只显示提示，不插入物品
```

`7/1`、`7/7 type=2` 不再出现在该分支；`7/11` 同步客户端合并后的同 ID 物理行，
避免把服务端未合并的数据库序号逐行写入已经被 CBE 合并的记录（这会把 `20+16`
错误写成 `9+12`）。服务端会先按 `TimerControl_ProcessItem(0x01032EB8)` 的堆叠
上限、拆分和序号交换规则重建可见行，再扣减和生成 7/11。`7/37 result=1` 负责
显示一次成功提示。储备瓶、扩容卡、小喇叭及各专用道具协议维持
原状。

## 已排除的方案

- `7/1` 成功确认：`HandleItemOperationResponse` 无条件调用
  `ui_show_message_box("使用成功",...,10)`；该界面不是此路径的安全完成通道。
- `7/7 type=2`：真实客户端已复现仍有常驻进度条。

## 回归

`scripts/normal-recovery-item-modal-regression.c` 从隔离资源加载实际 `item.dsh`，
断言 301（小回春散）和 321（小回气散）走 `7/4 + 7/11(all-rows)` 的静默背包完成分支，
而 802 储备瓶及 806 背包扩容继续保留各自既有协议。实现后执行：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 `
  -ffunction-sections -fdata-sections -w `
  scripts/normal-recovery-item-modal-regression.c `
  obj/client/gifDecode.o obj/client/cbeParser.o obj/client/mystd.o `
  obj/client/fontEngine.o obj/client/vmMalloc.o obj/client/fileIoEngine.o `
  obj/client/lcd.o obj/client/automation_png.o obj/client/md5.o `
  obj/server/mysql-client.o -Wl,--gc-sections `
  -o tmp/normal-recovery-item-modal-regression.exe `
  -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32 `
  Lib/unicorn-2.1.4/unicorn-import.lib -LLib/sdl2-2.0.10/lib `
  -lSDL2main -lSDL2
.\tmp\normal-recovery-item-modal-regression.exe
```

待验证：使用 301 后生命恢复、数量减少（含最后一份移除）、界面无需点击即可继续操作；
再覆盖法力药、储备瓶与背包扩容。
