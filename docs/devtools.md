# 开发者工具与性能面板

`weft-unreal` 将 Weft 的 Chrome DevTools 协议、编辑器面板、屏幕性能浮层和日志分级整合进 UE5 工作流，同时通过 Dev / Release 双构型保证正式发布包零调试残留。

## 1. Dev / Release 双构型与 Shipping 开关

| 构建配置 | 默认加载的 DLL | `WITH_WEFT_DEVTOOLS` | 行为 |
| --- | --- | --- | --- |
| Editor / DebugGame / Development / Test | `Dev/weft.dll` | `1` | 内含 DevTools（默认休眠，启动后才监听端口）与调试面板 |
| Shipping | `Release/weft.dll` | `0` | 完全移除调试模块、控制台命令、字符串与 `WS2_32.dll` 依赖 |

如需在某个 Shipping 测试包中临时保留 DevTools，可使用以下两种方式之一：

1. **项目设置**：打开 **Project Settings → Plugins → Weft UI**，勾选 **Developer Tools in Shipping**（写入 `Config/DefaultEngine.ini`）：

   ```ini
   [/Script/WeftEditor.WeftBuildSettings]
   bDevToolsInShipping=True
   ```

2. **单次命令行打包覆盖**（不改动配置文件）：

   ```powershell
   # 直接传给 UBT
   -ini:Engine:[/Script/WeftEditor.WeftBuildSettings]:bDevToolsInShipping=True

   # 通过 RunUAT 传递
   -ubtargs="-ini:Engine:[/Script/WeftEditor.WeftBuildSettings]:bDevToolsInShipping=True"
   ```

打包日志中出现 `Weft: Shipping with weft's developer tools` 即表示生效。由于 Shipping 包不带交互控制台，启动游戏时可通过命令行参数 `-WeftDevTools` 和 `-WeftDebug` 启用。

## 2. 启动与连接 Chrome DevTools

开发版 DLL 默认不监听任何网络端口，按需通过以下任意途径开启：

- **控制台命令**：
  - `weft.DevTools`：监听 `127.0.0.1`，从 `9230` 起自动选取首个空闲端口（若已在监听，则重新打印各页面地址）
  - `weft.DevTools 9240`：监听指定端口
  - `weft.DevTools 0.0.0.0:9230`：监听所有网卡，供局域网其他设备远程连接
  - `weft.DevTools stop`：停止监听
- **命令行参数**：`-WeftDevTools`、`-WeftDevTools=9240`、`-WeftDevTools=0.0.0.0:9230`
- **环境变量**：`WEFT_DEVTOOLS_LISTEN=9230` 或 `0.0.0.0:9230`
- **编辑器面板**：在 **Window → Log → Weft** 中点击 **Start / Stop**

启动后，`LogWeft` 会输出监听地址及每个已加载页面的调试链接：

```text
LogWeft: Display: Weft DevTools: listening on 127.0.0.1:9230. Open a page's address below in Chrome, or chrome://inspect > Configure > 127.0.0.1:9230.
LogWeft: Display: Weft DevTools: [Settings] devtools://devtools/bundled/devtools_app.html?ws=127.0.0.1:9230/devtools/page/3-1
```

将 `devtools://...` 粘贴至 Chrome 地址栏，或在 `chrome://inspect` 的 `Configure` 中添加 `127.0.0.1:9230` 后点击 `inspect`，即可实时检查 DOM、盒模型、命中的 CSS 规则、计算样式、Console 日志，并直接在游戏画面上拾取元素。

### 热重载

搭配 Weft 主仓库的监视脚本，保存本地 CSS / JS 文件后直接热更新运行中的游戏界面，无需重新编译或打包：

```sh
node path/to/weft/tools/devtools_watch.mjs 127.0.0.1:9230 Content/UI/Hud
```

> DevTools 请求在 View 推进时处理，目标页面需处于正常渲染状态（挂载在 `SWeftViewport` 中）。

## 3. 日志分类

插件将系统事件与页面输出拆分为两个独立的日志分类，便于在 Output Log 中过滤：

- **`LogWeft`**：插件自身事件（加载的 DLL 路径与提交哈希、DevTools 启停与页面链接、页面加载失败、渲染错误）。
- **`LogWeftPage`**：来自页面内部的日志与诊断，每行自动携带页面目录名（`UWeftView::GetPageName()`），例如 `[Settings] ...`：
  - `console.log` / `info` / `debug` → `Log`
  - `console.warn` 与场景/样式降级诊断 → `Warning`
  - 未捕获脚本异常与 `console.error` → `Error`

所有诊断同时通过 `UWeftView::OnDiagnostic` 委托广播。未启用 `webPlatform` 的页面中，`console.log` / `console.warn` 为空操作，`console.error` 视作异常。

## 4. View 性能监控表

插件提供三种查看所有活跃 View 状态与耗时的入口：

- **编辑器面板**：**Window → Log → Weft**（或控制台输入 `weft.Panel`），每 0.5 秒自动刷新，提供 DevTools 开关、一键复制 `devtools://` 地址与屏幕浮层开关。
- **屏幕浮层**：控制台执行 `weft.Debug 1` / `0`，或启动时附加 `-WeftDebug`，在画面左上方叠加显示实时表格。
- **日志快照**：控制台执行 `weft.Views`，采样 1 秒后将对齐表格与 DevTools 链接输出到 `LogWeft`。

| 列 | 含义 |
| --- | --- |
| `View` | View 在 DevTools 中的目标 ID（`<slot>-<generation>`） |
| `Page` | 页面名称（样式表所在目录名） |
| `Size` / `Scale` | 逻辑尺寸（CSS px）与设备像素比 |
| `Elements` | 文档 DOM 节点数（最多每秒统计一次） |
| `Game ms` | 游戏线程耗时（推进、输入、帧快照移交），过去 1 秒每帧的平均 / 最慢值 |
| `Draw ms` | 渲染录制线程耗时，过去 1 秒每帧的平均 / 最慢值 |
| `Animated` | 页面是否存在持续动画（CSS 动画/过渡或 `requestAnimationFrame`） |
| `Errors` / `Warnings` | 页面加载以来的异常/错误数与样式/警告数 |

性能采样仅在面板打开、屏幕浮层开启或调用 `weft.Views` 后的 2 秒窗口内激活，平时零额外计时开销。
