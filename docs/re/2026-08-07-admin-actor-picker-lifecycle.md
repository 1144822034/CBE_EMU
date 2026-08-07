# 后台动态 NPC Actor 选择器无响应

## 触发条件

进入“游戏内容管理”的动态 NPC 编辑页，点击“搜索与预览”型 Actor 资源按钮；特别是在
同一后台标签页内通过左侧目录切换过内容区域之后，按钮可能可见但不打开选择弹窗。

## 链路与证据

`vm_mock_admin_render_actor_select` 生成隐藏的 `select.actor-resource-select` 和可见的
`[data-actor-picker-open]` 按钮。`setupPartialNavigation` 通过
`detail.innerHTML = nextDetail.innerHTML` 替换该编辑区。旧实现仅在 `setupActorPicker` 当次
遍历到的按钮上注册 `click` 监听器，并在页面初始化时立即创建全部 Actor 预览卡片。

这让点击能力依赖某一轮局部重绘之后是否恰好再次完成逐元素绑定，违反了动态内容的事件
归属约定；按钮本身不含可独立工作的行为，因此绑定丢失时表现为点击无反应。

独立测试后台响应确认实际页面同时具备触发按钮、隐藏选择值、Actor 选项来源和模态框，且
下发的 `admin.js` 可由 `node --check` 解析。故首个偏离位于前端元素监听器的生命周期，而
非 Actor 资源枚举、路由或服务端资源校验。

## 修复

Actor 选择器改为一个页面级状态对象：

- 只注册一次 document 级点击/提交/键盘委托，局部替换后的新按钮由同一处理器识别；
- 每次内容区重绘时刷新当前表单、选项来源与模态框引用；
- Actor 预览卡片在首次打开弹窗时才创建，选择后通过原生 `change` 事件同步隐藏字段与标签；
- 模态框自身的关闭、遮罩和搜索监听器按当前 DOM 实例绑定一次。

该改动不改变提交字段 `actor_resource`、资源列表或服务端的 Actor 支持规则。

随后在真实页面上发现初始化回调仍调用了已经删除的 `setupChestTabs()`。当前宝箱页面已
改为左右布局，不再输出该函数所需的 `data-chest-editor-root`、`data-chest-editor` 或
`data-chest-select` 标记；该调用没有对应实现。浏览器在此处抛出 `ReferenceError` 后停止
执行同一回调，因而 `setupActorPicker()` 完全未运行。这是 Actor 按钮无响应的直接原因。
移除无效调用，并加入“所有 `setup*()` 调用均有定义”的静态核对。

## 回归边界

构建后应验证：初次进入 NPC 编辑页、目录局部切换后再次进入、搜索、选择预览资源、提交
缺少 Actor 时的提示以及 ESC/遮罩/关闭按钮均走同一个选择器状态机。脚本检查除语法外，
还必须核对初始化函数的定义/调用集合，避免未定义符号中断后续初始化。
