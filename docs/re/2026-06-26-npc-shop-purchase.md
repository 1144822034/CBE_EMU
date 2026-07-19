# NPC Shop Purchase Phase

Date: 2026-06-26

Status: implemented for shop open, DSH-backed catalog paging, shop-friendly catalog ordering, host DSH lookup compatibility, and buy-result packets; needs end-to-end NPC click validation

## 2026-07-19 Windows 服务资源根修复

### 现象与根因

- Windows 版以 `--mock-service-only` 启动时仍走完整模拟器编译分支，不会执行
  `CBE_SERVER_ONLY` 分支中的资源根初始化。
- 服务启动后先把工作目录切到 `bin/`，此时客户端可写缓存尚未下载
  `item.dsh/equip.dsh`。商城目录第一次加载便永久缓存了英文传送石兜底行，日志为
  `mock_shop_catalog fallback=item.dsh/equip.dsh-not-found item=800`。
- 后续即使客户端已经下载 DSH，服务端缓存仍只有这一行，所以秘宝以外的
  `14/6..13` 全部返回 `total=0 rows=0 iteminfo_len=3`。

### 修复

- `vm_net_mock_service_run_forever()` 在账号和角色数据初始化前统一解析服务资源根；
  默认优先 `../web/fs/JHOnlineData`，并支持 `CBE_RESOURCE_ROOT`。Windows 与独立
  服务编译现在使用同一正式资源源，不再依赖客户端缓存何时生成。
- 客户端只读打开裸 `*.dsh` 时仍优先使用 `bin/JHOnlineData` 缓存；缓存尚未存在时，
  回退到 `web/fs/JHOnlineData`，确保 `mmGame:0x418C` 能用本地 DSH 填充
  `17/1` 行的名称和属性。

### 回归结果

- 启动日志：`resource_root=../web/fs/JHOnlineData source=service-auto`；装备属性目录
  加载 `1485` 行，不再出现 fallback。
- 商城目录：`total=1715 items=230 equips=1485 first=1001`。
- `14/5` 返回 8 条秘宝；`14/6..13` 各分类第一页均返回 10 条，响应长度分别为
  `532/532/530/530/533/532/532/530` 字节。
- 商城返回场景的 NPC one-shot 回归同时通过；服务 stderr 为空。

## 1. Current Block

- Visible symptom: after NPC dialog purchase flow, clicking the dialog `购买` button opens the shop, but the list either stayed on one `传送石` row or became selectable blank rows with no item names.
- Minimum target for this pass:
  - answer the shop-open `1/1/14(actorId)` request with the response objects consumed by `mmShopMstarWqvga.cbm`;
  - expose the local `item.dsh` and `equip.dsh` rows through the mmShop item-page format instead of one hard-coded row;
  - support the confirmed shop purchase result packet so the shop module does not receive an echo/empty response for a state-changing buy request.

## 2. IDA Evidence

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| `mmShopMstarWqvga.cbm` | `sub_11F0` / `0x11F0` | shop-open request builder | Builds `WT 1/1/14(actorId)` when the NPC dialog opens the buy branch. |
| `mmShopMstarWqvga.cbm` | `sub_162C` / `0x162C` | shop actor-query loading path | Sets the shop loading flag at `[r9+10344]+16`, then calls `sub_11F0`. This path does not issue the `14/5` and `14/6` page requests before waiting for the actor-query response. |
| `mmShopMstarWqvga.cbm` | `sub_1038` / `0x1038` | shop init | Registers `sub_9DE` as the network parser, zeros the two page-state structs at `r9+10792`, then immediately sends `sub_618(0,5)` and `sub_618(1,6)`. |
| `mmShopMstarWqvga.cbm` | `sub_9DE` / `0x9DE` | shop-open/network parser | Iterates response objects. For kind `14`: subtype `14` reads `result` and `shopinfo`; subtype `4` reads `coolmoney` and `ticket`; subtype `5..13` calls the item-page parser; subtype `3` reads `seq` and `result` for buy completion. For kind `1` subtype `14`, it reads `revivetype` / `ruffianflag` / `type`, calls the shared `actorinfo` parser, clears the shop loading flag, and immediately returns, so this status object must be last when bundled with page objects. |
| `江湖OL.CBE` | `parse_actorinfo_response` / `0x0100FA88` | actor-state payload parser | The `mmShop:0x9DE` `1/1/14` branch reaches this parser. If the response object has no `actorinfo` blob, the stream reader is initialized with a null buffer and crashes at `stream_read_i32_be_tagged_impl(0x01033A68)`. |
| `mmShopMstarWqvga.cbm` | `sub_7BC` / `0x7BC` | shop item page parser | Reads `totalnum`, then raw `iteminfo`: row count, item id, item name, small flags, price, stock/count, another flag, and the shared item extra block. |
| `mmShopMstarWqvga.cbm` | `sub_618` / `0x618` | shop page request builder | Builds `WT 1/14/<subtype>` with an `index` byte. This is the paging path for subtypes `5..13`; subtype `5` is `秘宝道具`, while `6..13` are the `神兵利器` sub-tabs. |
| `mmShopMstarWqvga.cbm` | `sub_2F6C` / `0x2F6C` | W-coin buy request builder | Sends `WT 1/14/3` with `type`, `id`, and `num`. `sub_2EB6` selects a row and `sub_2E88` stores row `+60` as the default purchase count/step; if that byte is zero, the W-coin purchase dialog can divide by zero while formatting `花费%dW币`. |
| `mmGameMstarWqvga.cbm` | `sub_418C` / `0x418C` | shop/backpack item-list parser | The `17/1` branch opens `item.dsh` and `equip.dsh`, reads raw `iteminfo` as `row_count` followed by `itemId + common item-extra`, allocates `324 * row_count`, sorts rows by item id, and fills display data from local DSH rows. If the DSH open/lookup fails, the UI still has selectable rows but names/prices remain blank. |
| `江湖OL.CBE` | `ParseEquipAttributes` / `0x010185C2` | shared item-extra parser | The vtable `+2452` helper first reads two i16-style fields, then one u8 `attr_count`; it only reads attr slots while `slot_index < attr_count`. The six arrays passed by callers are destination capacity, not fields that must always be sent. |
| `mmShopMstarWqvga.cbm` | `sub_7BC` / `0x7BC` | shop page cursor and row parser | `sub_7BC` starts writing rows at the page-state start cursor (`[page+6]`) and advances it by `row_count`. The `14/5` row tail calls `ParseEquipAttributes`, so zero-attribute rows must stop after `i16, i16, u8 attr_count=0`. |
| host file/DF I/O | `src/vmFunc.c` | local DSH lookup compatibility | `mmGame:0x418C` asks for bare `item.dsh/equip.dsh`. The files are present under `JHOnlineData/`, so read-only host file open and DF filename lookup now resolve bare `.dsh` names to `JHOnlineData/<name>` / matching basename. |
| runtime dispatch order | `src/mock-server.c` | NPC dialog buy combo | The NPC dialog buy path can combine the shop object family with scene/dialog follow-up objects such as `2/10` or `27/4`. A broad `27/4` handler must not run before the shop-family handler, or the shop page remains stale. |
| runtime packet trace | `WT 17/2` | shop buy request | Buy confirmation emits a single `1/17/2` object with `shopId`; the response consumed by the active shop parser is not an echo of this object. |
| `mmShopMstarWqvga.cbm` | `0xC2C` string | buy-result field name | The string referenced by the subtype-3 branch is `seq`. |
| `江湖OL.CBE` | `sub_1033544` / `0x01033544` | main item business parser comparison | For acquire subtype `7/8 type=4`, the parser reads field `seq` and writes it into item `+276`; this confirms the field name and typed value used by the item insertion contract. |
| `mmGameMstarWqvga.cbm` | `sub_418C` / `0x418C` | backpack/item sync parser comparison | The already implemented backpack-open response pairs `17/1 iteminfo` with `7/42 booksinfo`. Runtime showed the NPC purchase flow can emit a combined `17/1 + 7/42` request before final buy confirmation, so answer it with the same parser-safe pair. |

## 3. Request / Response Contract

### Shop Open Request

- WT object: `1/1/14`
- Required detector fields:
  - `actorId` as u32

### Shop Open Response

- WT objects:
  - `1/14/14 { result=1, shopinfo="Codex Shop" }`
  - `1/14/4 { coolmoney=<active-role wcoin>, ticket=0 }`
  - `1/14/5 { totalnum=<secret item count>, iteminfo=<秘宝道具 page 0 rows> }`
  - `1/14/6 { totalnum=<weapon item count>, iteminfo=<神兵利器/武器 page 0 rows> }`
  - `1/1/14 { revivetype=0, ruffianflag=0, type=0, actorinfo=<current-role actorinfo blob> }`
- Runtime negative after entering the shop and returning showed `WT 1/1/14(actorId)` followed by only the `builtin-shop-actor-query14` response, with no subsequent `1/14/5` or `1/14/6` page requests. IDA confirms `mmShop:0x162C` only sets the loading flag and calls `sub_11F0`, so the actor-query response now includes the first page inline.
- The final `1/1/14` object is not an echo of the request. It is an actor-state status object for `mmShop:0x9DE`; placing it before the `14/5` page would stop the parser before item rows are consumed.
- Negative runtime: sending only `revivetype/ruffianflag/type` in the final `1/1/14` object crashed with `pc=01033A68 lr=0100FAC5`, proving the `actorinfo` blob is mandatory for this branch.
- The standalone `1/14/<subtype>(index)` page path remains supported for `mmShop:0x1038/sub_618` and later paging.
- Catalog page size: `10` rows per page response.

### Shop Catalog Source

- `item.dsh`: 230 rows, parsed from `ID`, `名称`, `形象`, `类别`, `价值`, and `堆叠数`.
- `equip.dsh`: 1485 rows, parsed from `ID`, `名称`, `价值`, and `类别`.
- Mall secret items (`类别=14`) also carry a dedicated `酷宝` column. Current
  runtime rows such as `800 传送石`, `801 复活石`, and `806 背包扩容` show that
  `价值` is not the premium-shop W-coin price for this page; the mock now uses
  `酷宝` as the displayed/billed shop price for `14/5`, while equipment pages
  continue to use `equip.dsh` `价值`.
- `equip.dsh` has no `酷宝` / `人民币` / `ticket` column. For the
  `神兵利器` pages, `mmShopMstarWqvga.cbm:sub_7BC(0x7BC)` reads the row `price`
  directly from the server `iteminfo` stream and does not perform any local
  `equip.dsh`-based price conversion. That means `1001 木制宽剑 = 675` currently
  comes from the mock's choice to map equipment-page price to `equip.dsh` `价值`;
  there is not yet evidence for a second built-in premium-price source.
- Purchase success for `14/3` `type=2` does not spend a single unified balance.
  `mmShopMstarWqvga.cbm:sub_9DE(0x9DE)` first applies `ticket`, then deducts any
  remainder from `coolmoney`. The current mock still reports `ticket=0`, so all
  successful mall purchases draw from the role-bound `wcoin` balance exposed as
  `coolmoney`.
- `806 背包扩容` is a client-local price exception. `sub_7BC` ignores the raw
  row price for that item and calls `sub_74E(0x74E)`, which maps the local
  backpack-capacity field to `20 / 40 / 60`. The mock now mirrors that helper
  when building the row price and when calculating the `14/3` bill, so client
  display and server deduction stay aligned.
- Current max catalog capacity: `2048` rows; the local resources load the full `item.dsh` plus the first bounded slice of `equip.dsh`.
- Runtime DSH visibility matters twice:
  - mock-server loads `JHOnlineData/item.dsh` and `JHOnlineData/equip.dsh` to build `14/5` rows with server-provided names;
  - `mmGame:0x418C` independently opens bare `item.dsh/equip.dsh` while rendering the `17/1` list, so host I/O must resolve those bare names to the same local resources.
- The in-memory shop rows retain source/category metadata. Do not classify mall
  pages by ID ranges alone: `800..999` contains several `item.dsh` categories,
  while the client mall tabs use the labels and category fields below.
- Shop row names are capped at 12 bytes to match `sub_7BC`'s 13-byte destination including the NUL terminator.
- Fallback, only if both DSH files are unavailable: one `Teleport Stone` row.

### Shop Page Request

- WT object: `1/14/<subtype>`
- Supported fields:
  - `index` as u8 or u32
- Response:
  - subtype `5`: `秘宝道具`, filtered from `item.dsh` category `14` and capped
    to one visible page for now.
  - subtype `6`: `神兵利器/武器`, filtered from `equip.dsh` weapon categories
    `7/8/9`.
  - subtype `7`: `衣服`, `equip.dsh` category `1`.
  - subtype `8`: `裤子`, `equip.dsh` category `4`.
  - subtype `9`: `帽子`, `equip.dsh` category `0`.
  - subtype `10`: `鞋子`, `equip.dsh` category `5`.
  - subtype `11`: `束腰`, `equip.dsh` category `3`.
  - subtype `12`: `披风`, `equip.dsh` category `2`.
  - subtype `13`: `饰品`, `equip.dsh` category `6`.
  - Static evidence: `mmShopMstarWqvga.cbm` contains the label sequence
    `秘宝道具 / 神兵利器 / 武器 / 衣服 / 裤子 / 帽子 / 鞋子 / 束腰 / 披风 / 饰品`.
    `sub_1BD6` calls `sub_17E8(1, selected + 6)` for the eight `神兵利器`
    sub-tabs, so `14/6` must not be used for `秘宝道具`.

### Shop Info Refresh Request

- WT objects:
  - `1/14/14`
  - `1/14/4`
- IDA evidence:
  - `mmShop:0x1038` calls `sub_6D6()` and `sub_6BC()` before page requests;
  - those build the same kind-14 status/money requests that `sub_9DE` parses.
- Response:
  - `1/14/14 { result=1, shopinfo="Codex Shop" }`
  - `1/14/4 { coolmoney=<active-role wcoin>, ticket=0 }`

### NPC Dialog Buy + Shop Combo Request

- Core WT shape: contains the shop-open family and may include `14/14 + 14/4 + 14/5 + 14/6` markers from the dialog path.
- Optional companion objects seen or supported on the NPC dialog path:
  - `2/10` actor-other follow-up;
  - `27/11` / `27/4` fb-target dialog follow-up;
  - `7/42` book info.
- Previous handling was too narrow (`requestLen == 61`) and was placed after the broad `27/4` handler. If the NPC dialog buy packet carried `27/4` or had a different length, it could be answered without the shop item page, leaving the UI at the previous single `800` row.
- Response now returns:
  - requested safe scene/dialog follow-up objects (`2/10`, `27/11`, `27/4`, `7/42`) when present;
  - `1/14/14 { result=1, shopinfo="Codex Shop" }`
  - `1/14/4 { coolmoney=<active-role wcoin>, ticket=0 }`
  - `1/14/5 { totalnum=<secret item count>, iteminfo=<秘宝道具 page index rows> }`
  - `1/14/6 { totalnum=<weapon item count>, iteminfo=<神兵利器/武器 page index rows> }`
- If `14/5/14/6` are batched in the same request, do not drop them. `mmShop:0x9DE` uses one local response-object counter and clears loading after the full four-object family is parsed.

### Purchase Request

- W-coin mall page:
  - WT object: `1/14/3`
  - fields: `type`, `id`, `num`
- NPC/list compatibility path:
  - WT object: `1/17/2`
  - required field: `shopId` as u32
  - optional field: count / `num`

### Purchase Response

- WT object: `1/14/3`
- Fields:
  - `seq`
  - `result`

- The W-coin `14/3` request is handled by `builtin-shop-buy14`; the balance now
  comes from the active role's persisted `wcoin` field and new roles start at
  `0`. The normal path checks that balance, deducts `price * num`, adds the
  item to the active role backpack, saves the role DB, then returns
  `seq/result`.
- Special case confirmed in `mmShopMstarWqvga.cbm:sub_9DE`: local purchase
  `type=2` + `id=806` (`背包扩容`) does not route through the normal backpack
  add callback. The client handles that success by increasing its local
  capacity field directly, so the mock must expand persisted backpack capacity
  immediately, return `14/3 { seq=0, result=1 }`, and avoid arming the
  one-shot role-backpack sync that is only valid for ordinary purchased items.
- On successful buy, clear the one-shot shop `17/1` pending flag. That flag is
  only for the shop list sync after open/page requests; leaving it set after a
  completed purchase hijacks the next backpack `17/1`/open request and makes the
  backpack appear stale until relogin.
- The `17/1 + 7/42` combo must also be gated by that same pending flag and by a
  nonempty `17/1` payload. Runtime after W-coin purchase showed that an
  unconditional shop-list combo handler answered the normal backpack-open combo
  with purchasable shop rows, so the visible backpack looked empty/wrong until
  relogin rebuilt the role item grid.
- Successful purchase also arms a one-shot role-backpack priority for the next
  backpack-shaped request. This protects the return/open-backpack path even if
  an intermediate shop page or return response re-arms the shop list pending
  flag before the user opens the backpack UI.
- Opening mmShop invalidates the one-login backpack grid seed guard. Runtime
  after returning from the shop shows a fresh mmGame scene init followed by
  `5/10 + 7/7`; if `30/21` is suppressed because the role was seeded earlier in
  the login, the newly created client item manager stays empty until relogin.
  Shop-open and successful-buy responses therefore clear the grid-seeded role id
  so the next group/type1 response can resend the active role backpack grid.
- Runtime negative: `14/13` page followed by local purchase crashed with
  divide-by-zero before a buy packet was sent. The row byte after `stock`
  must be `1` because the client copies row `+60` into its purchase count/step
  field before rendering the `花费%dW币` dialog.

### NPC Shop 17/1 List Combo Request

- Runtime failure:
  - `[error][network] unhandled wt=17/1 len=25 objects=1 first=1/17/1:11,1/7/42:0`
- Narrow detector:
  - request length `25`;
  - first object `1/17/1` with payload length `11`;
  - second object `1/7/42` with empty payload.
- Response:
  - answer the `mmGame:0x418C` visible list parser, not the backpack default list:
    - `1/17/1 { iteminfo=<shop item id list> }`
    - `1/7/42 { booknum=0, booksinfo=<empty> }`
- `iteminfo` rows:
  - one `u8 row_count`, currently capped at `10` for parser safety;
  - each row is `u32 itemId` plus the shared common item-extra block:
    `i16 stack/runtime`, `i16 reserved`, `u8 attr_count`;
  - with `attr_count=0`, no attr slots follow;
  - low material/task-drop ids are omitted;
  - the visible `17/1` shop-sync list can still prefer equipment ids for the
    NPC equipment-shop compatibility path, but `14/5..13` page responses are
    now filtered by the mall tab mapping above.
- Shop context fallback:
  - after a shop open/page/interaction response, set a one-shot shop-list pending flag;
  - if the next request is empty `1/17/1`, return the same shop `17/1` item list instead of the backpack default;
  - if the next request is empty `1/7/42`, return shop `17/1` item list plus empty `7/42` books.

## 4. Implementation

- Replaced the old `1/1/14(actorId)` echo with `vm_net_mock_build_shop_actor_query14_response()`:
  - returns the `14/14 + 14/4 + 14/5 + 14/6 + 1/1/14` shop-open object family;
  - appends the `1/1/14` actor-state status with current-role `actorinfo` last, because `mmShop:0x9DE` clears loading and returns from that branch;
  - keeps standalone catalog rows available through the client-driven `1/14/<subtype>(index)` requests emitted by `mmShop:0x1038/sub_618`;
  - logs `mock_shop_open14 ... pages=inline actor_state=last evidence=runtime:no-page-followup-after-1/1/14 mmShop:0x162C/0x11F0/0x9DE/0x7BC`.
- Added a lazy DSH catalog loader for `item.dsh` and `equip.dsh`.
- Added host-side `.dsh` lookup compatibility in `src/vmFunc.c`:
  - ordinary read-only file open for bare `item.dsh/equip.dsh` tries `JHOnlineData/<name>`;
  - `vm_file_exists` uses the same fallback;
  - DF resource-name lookup performs a `.dsh`-only basename fallback after exact matching fails.
- Added DSH-backed category metadata so mall pages can be filtered by client tab
  labels instead of raw item id ranges.
- Capped row names to the mmShop display buffer size to avoid later long names
  overwriting the fixed 64-byte row slots parsed by `sub_7BC`.
- DSH file lookup now also tries paths relative to the running `main.exe`
  directory and its parent so launching from a different working directory does
  not silently fall back to one Teleport Stone.
- Runtime console evidence:
  - success: `[info][network] mock_shop_catalog total=... items=... equips=... first=... source=item.dsh/equip.dsh`
  - fallback: `[warn][network] mock_shop_catalog fallback=item.dsh/equip.dsh-not-found item=800`
- Added `vm_net_mock_build_shop_iteminfo_page_blob()` and
  `vm_net_mock_append_shop_catalog_page_object()` for the mmShop item-page rows.
- Confirmed the shared item-extra encoding for both visible item-list paths:
  - `17/1` and `14/5` both call `JianghuOL:0x010185C2`;
  - the wire layout is `i16, i16, u8 attr_count`, followed by exactly
    `attr_count` slots of `u8, u8, u8, i16`;
  - for the current zero-attribute shop rows, the correct tail is just the
    two i16 fields plus `attr_count=0`.
- Added `vm_net_mock_is_shop_page14_request()` and
  `vm_net_mock_build_shop_page14_response()`:
  - responds to `1/14/5(index)` with the bounded `秘宝道具` page;
  - responds to `1/14/6..13(index)` with bounded `神兵利器` subcategory pages
    based on `equip.dsh` category/slot metadata;
  - dispatch source: `builtin-shop-page14`;
  - logs `mock_shop_page14 ... evidence=mmShop:0x618/0x7BC`.
- Added `vm_net_mock_is_shop_info14_request()` and
  `vm_net_mock_build_shop_info14_response()`:
  - answers standalone `1/14/14` and `1/14/4` init refresh requests;
  - dispatch source: `builtin-shop-info14`;
  - logs `mock_shop_info14 ... evidence=mmShop:0x6D6/0x6BC/0x9DE`.
- Updated `vm_net_mock_build_scene_interaction_followup_response()`:
  - detector now matches the shop object family instead of a fixed packet length;
  - it preserves requested scene/dialog follow-up objects before appending the full mmShop `14/14 + 14/4 + 14/5 + 14/6` family;
  - dispatch is before the broad `27/4` follow-up handler;
  - logs `mock_shop_scene_interaction_combo ... page5/page6 evidence=runtime:npc-buy-shop-family mmShop:0x1038/0x618/0x9DE`.
- Added `vm_net_mock_is_backpack_items_books_combo_request()` and
  `vm_net_mock_build_shop_items_books_combo_response()`:
  - matches the two-object `17/1 + 7/42` combo by object structure instead of
    hard-coding total packet length;
  - dispatch source: `builtin-shop-items-books-combo`;
  - returns the `mmGame:0x418C` `17/1` visible list instead of the normal backpack's single `传送石`;
  - logs `mock_shop_items_books_combo ... ids=... evidence=mmGame:0x418C runtime=npc-buy-wt17/1-len25`.
- The shop combo handler is only dispatched while `g_netMockShop17ListPending`
  is set and the request's `17/1` object has a nonempty payload. Otherwise the
  same request shape is a normal backpack-open combo and must be answered by
  `builtin-backpack-items-books-combo`.
- After a successful buy, `builtin-shop-buy-backpack-sync-*` takes precedence
  over the shop-list handlers for exactly one backpack-shaped request and
  returns the active role backpack (`17/1`, `17/1+7/42`, or `7/42` shape).
- Shop entry and buy completion reset `g_netMockBackpackGridSeededRoleId`, so
  the following map re-entry `builtin-group-type1` can include `30/21` again.
  The trace line to expect is
  `mock_backpack_grid role=<id> kind=30 subtype=21 gridnum=<rows>`.
- Historical blank-row fix for the `17/1` visible list:
  - previous first rows were `800..809` from `item.dsh`;
  - current first rows are equipment ids such as `1001`, so the parser takes the `equip.dsh` display-fill branch used by this equipment NPC.
- Do not apply that `17/1` equipment-first workaround to `14/5`: `14/5` is the
  `秘宝道具` page, while `14/6..13` are the `神兵利器` equipment sub-tabs.
- Negative runtime after the previous fixed-slot row-extra build produced
  selectable blank rows with `木制宽剑` only in the fifth visible slot. The
  root cause is row-tail misalignment: the client sorted zero/garbage ids ahead
  of the first valid equipment id. The current implementation sends
  `attr_count=0` and no empty slots, so each following itemId starts at the
  parser's expected cursor.
- Added read-only autotest probes for the blank-row symptom:
  - `shop_parser rows/item/name/done` follows `mmShop:0x7BC` by current module base and prints `row_count`, `item_id`, copied `name_hex`, and final buy/sell first rows;
  - `backpack_parser dsh_lookup` follows `mmGame:0x418C` at runtime `0x05184498/0x051844DA` and prints `item/equip` lookup result per row;
  - `backpack_parser commit` now includes first-row `name_hex`, `price`, and `flag278`.
- Added one-shot shop-context handlers:
  - `builtin-shop-items17` for empty `17/1`;
  - `builtin-shop-items-books` for empty `7/42`;
  - both run before the normal backpack handlers.
- Moved all shop-specific dispatch entries to the front of
  `vm_net_mock_build_response()` after update chunks and before rule/scene/battle
  fallbacks:
  - `builtin-scene-interaction-followup`;
  - `builtin-shop-actor-query14`;
  - `builtin-shop-info14`;
  - `builtin-shop-page14`;
  - `builtin-shop-buy17`;
  - `builtin-shop-items-books-combo`;
  - one-shot `builtin-shop-items-books` / `builtin-shop-items17`.
- Added normal `[info][network]` preview logs for the current shop page and
  `17/1` list. The first page should report ids beginning with
  `1001,1002,1003,1004,...` for this equipment NPC.
- Added `VM_NET_MOCK_SHOP_PURCHASE_ITEM_SEQ`.
- Added `vm_net_mock_is_shop_buy17_request()`:
  - matches only a single `1/17/2` object;
  - requires `shopId`;
  - records `num` when present for trace context.
- Added `vm_net_mock_build_shop_buy17_response()`:
  - answers runtime request `1/17/2(shopId)` with parser-side result `1/14/3 { seq=2, result=1 }`;
  - logs `mock_shop_buy17 ... resp=14/3 evidence=mmShop:0x9DE(seq/result)`.
- Dispatch source: `builtin-shop-buy17`.

## 5. Negative Evidence / Guardrails

- Do not echo the shop-open request back. `1/1/14(actorId)` is request-side; the mmShop open parser expects kind-14 response objects.
- Do not echo the purchase request back. The request carries client-side fields (`shopId`, item id, count); the parser expects kind `14` subtype `3` and will read `seq/result` from that response.
- Do not route `14/*` through the main CBE business dispatcher. The main dispatcher has no kind-14 case; these objects are for `mmShopMstarWqvga.cbm`.
- Do not use raw DSH ordering for `14/5..13`; use the explicit mall tab filters.
  Raw first rows are material/task items and do not match the mall category UI.
- Do not pack the whole catalog into one `iteminfo` object. WT packet and object lengths are 16-bit, and `mmShop` already has a page request path; keep the catalog paged.
- Do not map `秘宝道具` to `14/6`. Static string evidence shows `14/6..13`
  belong to `神兵利器` sub-tabs. Putting `传送石/复活石` in `14/6` makes them
  appear under `神兵利器`, which is a category mapping bug rather than a cursor
  bug.
- Do not send fixed empty attr slots when `attr_count=0`. IDA shows
  `ParseEquipAttributes(0x010185C2)` reads only as many slots as the count byte
  declares; unconditional empty slots shift the next row's itemId and create
  blank shop rows.
- Do not drop `14/5` or `14/6` when they are already present in the same batched
  shop-family request. The no-bundling rule applies only to the initial
  `1/1/14(actorId)` open response; batched `14/14+14/4+14/5+14/6` must be
  answered as a complete family for `mmShop:0x9DE`.
- If the UI still shows only `传送石`, first check whether the process was
  restarted and whether the success/fallback catalog log above was printed.
  A fallback log means the issue is host resource lookup, not mmShop parsing.
- If the UI shows selectable blank rows, first check `mmGame:0x418C` DSH lookup:
  row count and IDs parsed, but local DSH display fill did not run or did not find
  the requested item/equip row.
- Also check `mmShop:0x7BC` first. If `shop_parser name_hex` is nonzero but the
  UI is blank, the problem is in shop rendering/active page selection; if
  `name_hex` is all zero while `item_id` is valid, the server-side `14/5`
  string stream format is wrong for this parser.
- Do not answer the NPC dialog buy combo through the broad `27/4` handler before
  checking for the shop object family. That leaves the `14/5` shop item page
  stale and can keep the visible list at the previous single `800` row.
- Do not move shop handlers behind rule/scene/battle/default backpack fallbacks.
  The NPC buy path overlaps with generic scene follow-up and backpack request
  shapes, so the specific shop contracts must stay first.
- Do not broaden all `17/1` requests. The shop-list packet has a distinct two-object shape and a nonempty `17/1` payload; plain empty `17/1` remains the backpack default list except for the one-shot shop-context flag.
- Do not answer the NPC shop `17/1 + 7/42` combo with the normal backpack-open response. That response intentionally contains only the default `传送石` stack, which exactly reproduces the one-row shop symptom.
- Do not send the full item+equip catalog through one `17/1` raw field. Runtime after the first `17/1` implementation showed a packet-parse error on shop entry; keep this path page-sized because `mmGame:0x418C` copies `iteminfo` into a 1024-byte stream buffer.
- Do not use `25/6` as an initial backpack or purchase shortcut. Prior backpack work showed `25/6` is shop/acquire-style and not a capacity setup path.
- Do not write the main item manager directly. `mmShop:0x9DE` already reaches the normal item insert flow after parsing subtype `14/3` `seq + result`.

## 6. Validation

- `make`: passed after the `ParseEquipAttributes` attr-count fix.
- `git diff --check`: no whitespace errors; only existing LF/CRLF warnings.
- Smoke run from `bin`: `.\main.exe --autotest --shot-ms=5000 --max-ms=12000` exited through `autotest_exit` with no assert. This path does not reach NPC shop.
- Latest dispatch pass: shop handlers now run before rule/scene/battle/default
  fallbacks, and `17/1+7/42` detection is structure-based. Server logs now print
  the emitted id preview for both `14/5` and `17/1` shop lists.
- Latest blank-row pass: `ParseEquipAttributes(0x010185C2)` shows the row tail is count-based, not fixed-slot. The server now writes `i16, i16, u8 attr_count=0` for both `17/1` and `14/5`, which prevents stale empty attr bytes from being parsed as later item ids.
- Added parser probes after the latest blank-row report; the next useful
  validation artifact is a manual or scripted NPC-shop entry with
  `mock_shop_items_books_combo ... iteminfo_len=173` and
  `backpack_parser ... item0=1001 name_hex=...` in `bin/autotest/state.txt`.
- End-to-end NPC click validation is still pending because the current default server path does not force NPC seeding; next probe should start near `00蓬莱仙岛_04.sce` 药师 at `(166,280)` and capture the exact packets from dialog purchase to shop buy.
