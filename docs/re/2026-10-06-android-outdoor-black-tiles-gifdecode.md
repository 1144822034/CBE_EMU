# Android 户外黑地 / 登录框透底：资源契约

日期: 2026-10-06

## 对比 `1b07f8a`

提交版：`GetFile` **包优先**；`JHOnlineData` 解析曾只匹配裸 `.dsh`。

## 现象

1. 户外地砖黑、聊天黄条：曾把磁盘 GIF 抢在包前，或解包时丢掉 type。
2. 登录框白底但背后标题菜单透出：`UI8.gif` 等包未命中后，`fopen("UI8.gif")` 找不到 `JHOnlineData/UI8.gif`。

## 正确契约

1. `GetFileID` / `GetFile`：**包优先**。
2. 包未命中：裸名 `.gif/.map/.sce/.actor/.dsh/.cbm` → `JHOnlineData/<name>`（Android 另可回退 dist-hot）。
3. 磁盘文件：`LE u32 长度 + type + payload`，只剥长度，**保留 type**。
4. `gifDecode`：256KB 读入，缓冲放堆上（Android 线程栈）。

## 验证

覆盖安装后：标题登录框应遮住菜单；户外应有地砖。
