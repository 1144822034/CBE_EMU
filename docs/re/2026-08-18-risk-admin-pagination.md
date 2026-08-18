# 后台管理-风险角色管理列表翻页

日期：2026-08-18  
状态：implemented，已通过本地回归

## 触发

风险角色管理原本一次渲染 `LIMIT 100` 条“三秒内连续进入战斗”审计记录，并且
页面上不提供任何翻页入口。审计表 `account_role_rapid_battle_entry_audit` 只收
间隔 ≤ 3000 ms 的事件，但记录会随时间持续累积；超过 100 条后，更早的证据在
页面完全不可达，操作员无法翻看历史记录，也无法对第 101 条之后的账号执行封禁。

## 修复

页面沿用商品管理的翻页契约（`?tab=risk&page=N`），把固定的 100 条窗口改为
真正的分页：

- `VM_MOCK_ADMIN_RISK_AUDIT_PAGE_SIZE` 由 100 调整为 50，作为每页行数与单次
  渲染预算。
- 查询拆成两步：先用
  `SELECT COUNT(*) ... WHERE interval_ms<=3000` 得到总数，再按
  `ORDER BY audit_id DESC LIMIT offset,50` 取当前页。总数与页数一致由
  `vm_mock_admin_count_row`（由账号目录的 `COUNT(*)` 行回调改名复用）解析。
- `page` 参数用 `vm_net_mock_parse_u32_strict` 严格解析；非法输入回退第 1 页，
  超出范围钳制到最后一页。
- 表格下方输出 `第 X / Y 页 · 共 N 条` 与“上一页 / 下一页”链接；单页或空表
  不输出翻页条，空表保持原“暂无”提示。
- 封禁表单携带当前页（隐藏字段 `page`），操作后重定向回原页而不是回到第 1 页，
  便于连续处置同一页的多个账号。`vm_mock_admin_redirect_risk` 签名相应增加
  `page` 参数，三处调用点同步更新。
- 排序契约不变：始终按 `audit_id DESC`，翻页不会重复或漏掉行。

## 回归

`scripts/risk-admin-pagination-regression.c` 直接调用真实渲染器并连接本地开发
数据库（只读 SELECT；schema 准备与页面自身一致，是幂等的
`CREATE TABLE IF NOT EXISTS`），断言：

1. `page` 参数缺失或为垃圾时回退第 1 页；
2. 超出范围（如 4294967295）钳制到最后一页且不输出“下一页”；
3. 翻页条为 `第 X / Y 页 · 共 N 条`，且 Y 等于 ceil(N/50)；
4. 每页行数不超过 50，第 2 页与第 1 页不重复任何一行（审计时间戳集合不相交）；
5. 有可封禁账号时，封禁表单携带当前页号；
6. 响应不被大小限制替换（strlen < `VM_MOCK_ADMIN_RESPONSE_MAX`）；
7. 空表时任何 page 参数都渲染同一“暂无”页且不出现翻页条。

编译运行（与既有回归一致的命令）：

```powershell
make -j2
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 `
  -ffunction-sections -fdata-sections -w `
  scripts/risk-admin-pagination-regression.c `
  obj/client/gifDecode.o obj/client/cbeParser.o obj/client/mystd.o `
  obj/client/fontEngine.o obj/client/vmMalloc.o obj/client/fileIoEngine.o `
  obj/client/lcd.o obj/client/automation_png.o obj/client/md5.o `
  obj/server/mysql-client.o -Wl,--gc-sections `
  -o tmp/risk-admin-pagination-regression.exe `
  -lpthread -liconv -lm -lmingw32 -lkernel32 -lws2_32 `
  Lib/unicorn-2.1.4/unicorn-import.lib -LLib/sdl2-2.0.10/lib `
  -lSDL2main -lSDL2
.\tmp\risk-admin-pagination-regression.exe
```

当前开发库审计表为空，回归走“空表”断言分支；一旦有真实记录，同一脚本自动
切换为完整翻页断言（第 2 页、钳制、跨页去重）。
