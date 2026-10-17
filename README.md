# weft-unreal

把 [weft](https://github.com/JiahaoLi01/weft)（HTML、CSS、DOM 与 JavaScript 的游戏 UI 运行时）接进 Unreal Engine 5 的插件。页面在游戏线程上运行，帧由 weft 对应当前 RHI 的后端画进 Slate 的输出：用的是引擎自己的 GPU 设备与命令列表，不经 CPU 拷贝，也不另开设备。

当前状态（2026-10-15）：实验版。Win64、UE 5.8、D3D12 RHI 已在 CaseKargaroo 里跑通（城市标签）。

## 目录

| 位置 | 内容 |
| --- | --- |
| `Weft.uplugin` | 插件描述，运行时模块 `Weft` |
| `Native/` | 把 weft 编成一个 DLL 的 CMake 工程：链接 weft 的静态库，只导出 `include/weft` 的 C ABI（`weft.def` 从头文件生成），所以 weft 的 FreeType、HarfBuzz 与 UE 的互不相见 |
| `Scripts/BuildWeft.ps1` | 用一份 weft 签出编译 DLL，把头文件、导入库与 DLL 放进插件 |
| `Source/ThirdParty/WeftLibrary` | 外部模块：头文件与导入库（脚本生成，不入库）；没有它们时 `WITH_WEFT=0`，插件照常编译、界面不画 |
| `Source/Weft` | 运行时模块：子系统、View、Slate 控件、渲染 |

## 编译 weft

需要 CMake 3.25+、Visual Studio 2022；装了 Vulkan SDK 1.4.363.0 时连 Vulkan 后端一起编。weft 的 D3D12 后端在 weft 的 main 上（2026-10-17 由 `unreal` 分支合入）。

```powershell
pwsh Plugins/Weft/Scripts/BuildWeft.ps1 -WeftSource F:/Projects/weft
```

不给 `-WeftSource` 时找环境变量 `WEFT_SOURCE_DIR`，再找游戏工程旁边的 `weft` 目录。产物：`Binaries/ThirdParty/Weft/Win64/weft.dll`（随包发布，`RuntimeDependencies`）、`Source/ThirdParty/WeftLibrary/{include,lib}`，以及记录 weft 提交的 `WEFT_VERSION`。插件加载 DLL 时在日志里打出它的 weft 提交。weft 更新（比如 rebase 到 weft 的主分支）后重跑脚本。

## 用法

```cpp
// 游戏线程：一个 View，载入一个页面目录（page.json，或 page.js 加 page.css、keyframes.css）
UWeftView* View = UWeftSubsystem::Get()->CreateView();
View->LoadDirectory(FPaths::ProjectContentDir() / TEXT("UI/Hud"));
View->OnMessage.AddUObject(this, &AMyHud::OnPageMessage);        // weft.engine.send / call
GetWorld()->GetGameViewport()->AddViewportWidgetContent(SNew(SWeftViewport).View(View), 10);

// 低频数据：事件与视图模型（JSON）
View->Emit(TEXT("units"), Json, /*bLatest*/ true);                // weft.engine.on('units', ...)
View->SetModel(TEXT("camera"), TEXT("{\"tier\":\"Mid\"}"));       // weft.engine.model('camera')

// 每帧跟着场景走的元素：锚点（页面写 data-anchor="<Key>"），由渲染线程直接移动，不跑脚本、不重排
View->SetAnchors(Anchors);   // FWeftAnchor: Key, Position（逻辑 px）, Scale, Depth, bHidden, bBehind
```

- `SWeftViewport` 每帧按自己的几何调整 View 的大小（逻辑 px 即 Slate 单位除以 View 的内容缩放 `SetContentScale`，设备缩放即 Slate 的缩放乘它：UI 缩放设置用它，文字仍按设备分辨率画）、推进页面、把帧交给渲染线程。每一层的指针分流（`AddView` 的 `EWeftPointerRouting`）：`Element`（默认）是页面在那里有接受指针的元素就给页面；`Cursor` 是那个元素显示可交互光标（`auto`、`default`、`none` 以外）时才给页面——整屏铺着面板、游戏从空白处透出来的页面用它；滚轮在页面有元素处都交给页面，`Cursor` 下默认箭头处游戏也收到。其余交给下面的游戏视口。
- 页面文件夹的 `page.json` 可以写 `"forgivingCss": true`（Tailwind 这类构建工具生成的样式表：画不出的声明丢掉并打诊断，而不是拒绝页面）与 `"webPlatform": true`（console、localStorage、dataset、focus、KeyboardEvent、matchMedia）。
- 字体与效果：`UWeftSubsystem::AddFonts(fonts.json)`（`faces` 的 `family`、`path`、`weight`、`italic`、`face`、`unicodeRange`，与 `fallback`；在第一个页面加载之前）、`AddEffects(effects.json)`（UI 着色器效果：字节码由 weft 的 `cmake/compile_effect.cmake` 编出，`BuildWeft.ps1` 把它连同外壳复制到 `Source/ThirdParty/WeftLibrary/effects/`）。
- 页面的 localStorage：`UWeftView::SetStorageItem`（加载页面之前恢复）、`GetStorage(Revision, Items)`（修订号变了才交出，游戏据此保存）。
- 锚点随这一帧的帧数据一起交给渲染线程，在录制前写入 View：锚定的元素与这一帧的 3D 画面同步，不会晚一帧。
- 页面的诊断打在 `LogWeft`：console 的 log 为 Log、warn 与样式丢弃为 Warning、脚本异常与 console.error 为 Error；`UWeftView::OnDiagnostic` 也收到。帧画不出时渲染器把 weft 给的原因（`weft_renderer_last_failure`）各记一次。
- 页面文件夹里的图片：CSS 写 `background-image: url(../Textures/x.png)`（PNG），像背景图那样铺排。相对路径从页面的基准（`FWeftPage::Base`，`FromDirectory` 设为样式表的路径）算起，View 用 `FFileHelper` 读文件（打包后从 pak 读，所以页面文件夹要随包，如 `DirectoriesToAlwaysStageAsUFS`）。读不到或不是 PNG 时打一条页面诊断，那个背景不画图。
- 引擎的纹理：`UWeftSubsystem::SetTexture(Name, Texture)`，页面写 `background-image: -weft-texture(Name)`。

## 渲染

- `FWeftRenderer`（每个 weft 运行时一个）在第一次画时按当前 RHI 选后端 `IWeftRhiBackend`：
  - D3D12：weft 的 D3D12 后端（`render_backend_d3d12.h`）用 RHI 的设备，把帧录进 RHI 自己的命令列表（在 RDG 的一个 pass 里，先冲掉 RHI 的 barrier）。录完 signal 一个手动 fence：既关闭 RHI 的命令列表，让 RHI 重设它的状态，也告诉 weft 哪次录制在 GPU 上完成了。
  - Vulkan：weft 已有 Vulkan 后端（录进宿主的命令缓冲），接到 UE 的 Vulkan RHI 是下一步。
  - Metal 及主机平台：等 weft 有对应的后端；插件这一层的接口不变。
- weft 的渲染器只认它的创建线程，而 UE 5 的 RHI 在任务线程上执行命令列表的 lambda：插件给渲染器一个自己的线程（`FWeftOwnerThread`），lambda 把工作交给它并等它做完，录制仍落在命令列表里 lambda 的位置。
- Slate：控件画一个自定义元素，在 Slate 的渲染图里加 pass。控件在输出的左上角时 weft 直接画进输出；不在时（比如编辑器里的 PIE）先把那块拷进一张同样大小的纹理，weft 画在上面（`backdrop-filter` 也读得到游戏画面），再拷回去。
- 目标格式：BGRA8、RGBA8，以及 10 位的 `PF_A2B10G10R10`（UE 5 默认的后缓冲）。别的格式（HDR 输出）记一次警告、不画。

## 限制

- 只有 Win64 + D3D12；D3D11、Vulkan RHI 下不画（记警告）。
- 输入只有指针（移动、按键、滚轮、离开）；键盘、IME 与手柄还没有。
- 字体用系统字体（weft 的默认），以及 `UWeftSubsystem::AddFont` 加的字体文件。
- 页面里的图片只有 PNG 背景图（`url()`），没有 `<img>`、JPEG 与 `data:` URL。
- 一个运行时一个渲染器：所有 View 由同一个 weft 渲染器画。
- 没有 UMG 包装（`UWidget`）与蓝图节点以外的编辑器工具。
