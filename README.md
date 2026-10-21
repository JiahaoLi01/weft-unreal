# weft-unreal

把 [weft](https://github.com/JiahaoLi01/weft)（HTML、CSS、DOM 与 JavaScript 的游戏 UI 运行时）接进 Unreal Engine 5 的插件。页面在游戏线程上运行，帧由 weft 对应当前 RHI 的后端画进 Slate 的输出：用的是引擎自己的 GPU 设备与命令列表，不经 CPU 拷贝，也不另开设备。

当前状态（2026-10-21）：实验版。Win64、UE 5.8、D3D12 RHI 已在 CaseKargaroo 里跑通（城市标签），也跑 weft-page 的宣传页（约 4,000 个元素的长页面、四张场景捕获每帧重画的纹理、六个锚点；3840 × 2040 下帧时的中位数是 240 fps 上限的 4.17 ms）。

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

// 页面每帧自己推进的东西（施法条、冷却）：只在开始时给它开始的时刻与时长。时刻在页面的时钟上（秒；页面里
// performance.now() 与 requestAnimationFrame 的时间是它的毫秒）
View->Emit(TEXT("cast"), FString::Printf(TEXT("{\"start\":%.4f,\"duration\":2.4}"), View->GetPageTime()));

// 每帧跟着场景走的元素：锚点（页面写 data-anchor="<Key>"），由渲染线程直接移动，不跑脚本、不重排
View->SetAnchors(Anchors);   // FWeftAnchor: Key, Position（逻辑 px）, Scale, Depth, bHidden, bBehind

// 键盘：按在页面上时控件拿到 Slate 的键盘焦点，按键给页面的焦点元素（Tab、Enter、空格、方向键照 Chrome），
// 页面没用的键照常交给游戏；一开始就要页面拿键盘时
Viewport->FocusView(View);
```

- `SWeftViewport` 每帧按自己的几何调整 View 的大小（逻辑 px 即 Slate 单位除以 View 的内容缩放 `SetContentScale`，设备缩放即 Slate 的缩放乘它：UI 缩放设置用它，文字仍按设备分辨率画）、推进页面、把帧交给渲染线程。每一层的指针分流（`AddView` 的 `EWeftPointerRouting`）：`Element`（默认）是页面在那里有接受指针的元素就给页面；`Cursor` 是那个元素显示可交互光标（`auto`、`default`、`none` 以外）时才给页面——整屏铺着面板、游戏从空白处透出来的页面用它；滚轮在页面有元素处都交给页面，`Cursor` 下默认箭头处游戏也收到。其余交给下面的游戏视口。指针的移动每帧交给页面一次，用的是最后的位置（同浏览器把 pointermove 对齐到动画帧）；按键、滚轮、离开之前先把攒着的移动交出去。原因是 Slate 会把系统攒下的每一下移动都交过来，一帧慢了攒得多；而每一下都要页面做命中判断、跑处理函数，越慢攒得越多。指针在不在页面上（`IsPointerOverPage`）按每帧第一下移动的位置算。指针停着、按键不变时 Slate 仍会重复发移动（控件在它底下移动时），页面最多每 0.1 秒收一次：每一次都是一次命中测试，悬停状态仍跟得上从静止指针底下滚过的元素。
- 页面文件夹的 `page.json` 可以写 `"forgivingCss": true`（Tailwind 这类构建工具生成的样式表：画不出的声明丢掉并打诊断，而不是拒绝页面）与 `"webPlatform": true`（console、localStorage、dataset、matchMedia；焦点与键盘每个页面都有）。
- 键盘见下一节。
- 字体与效果：`UWeftSubsystem::AddFonts(fonts.json)`（`faces` 的 `family`、`path`、`weight`、`italic`、`face`、`unicodeRange`，与 `fallback`；在第一个页面加载之前）、`AddEffects(effects.json)`（UI 着色器效果：字节码由 weft 的 `cmake/compile_effect.cmake` 编出，`BuildWeft.ps1` 把它连同外壳复制到 `Source/ThirdParty/WeftLibrary/effects/`）。
- 页面的 localStorage：`UWeftView::SetStorageItem`（加载页面之前恢复）、`GetStorage(Revision, Items)`（修订号变了才交出，游戏据此保存）。
- 锚点随这一帧的帧数据一起交给渲染线程，在录制前写入 View：锚定的元素与这一帧的 3D 画面同步，不会晚一帧。
- 页面的诊断打在 `LogWeft`：console 的 log 为 Log、warn 与样式丢弃为 Warning、脚本异常与 console.error 为 Error；`UWeftView::OnDiagnostic` 也收到。帧画不出时渲染器把 weft 给的原因（`weft_renderer_last_failure`）各记一次。
- 页面文件夹里的图片：CSS 写 `background-image: url(../Textures/x.png)`（PNG），像背景图那样铺排。相对路径从页面的基准（`FWeftPage::Base`，`FromDirectory` 设为样式表的路径）算起，View 用 `FFileHelper` 读文件（打包后从 pak 读，所以页面文件夹要随包，如 `DirectoriesToAlwaysStageAsUFS`）。读不到或不是 PNG 时打一条页面诊断，那个背景不画图。
- 引擎的纹理：`UWeftSubsystem::SetTexture(Name, Texture)`，页面写 `background-image: -weft-texture(Name)`。渲染目标（`UTextureRenderTarget2D` 这类引擎能画进去的纹理，比如场景捕获每帧重画的那张）按 weft 的 live 纹理导入：引擎可以在两帧之间重画它，页面画的总是这一帧的内容；别的纹理导入后内容不能再变（要变就换个名字绑一张新的）。

## 键盘

- 焦点：`SWeftViewport` 的键盘给它的一层，像浏览器窗口的键盘给它的页面：鼠标最近一次按下的那层，或 `FocusView` 给的那层，都没有时是最上面的可交互层。按在一层的页面上时控件拿到 Slate 的键盘焦点（`SetUserFocus`）；控件有 Slate 的键盘焦点、应用在前台时，那一层的页面有焦点（window 的 focus 与焦点元素的 focus，`:focus`、`:focus-visible`、`document.hasFocus()` 只在这时成立），其他层的页面没有。按在游戏上（没有页面接的地方）时游戏视口拿走焦点，页面收到 blur。`FocusView` 可以在 `BeginPlay` 里调：打包的游戏在窗口第一次显示游戏视口时（第一个关卡的 `BeginPlay` 之后）把焦点给游戏视口，控件在它的第一帧把焦点拿回来。控件不画 Slate 的焦点框，焦点由页面自己画（`:focus-visible`）。
- 按键：`OnKeyDown`、`OnKeyUp` 交给那层的页面（`UWeftView::Key`）：code 是系统键盘布局把 Slate 给的 Windows 虚拟键放在的物理键（`weft_keyboard_code`；Slate 不给扫描码），key 是布局用它打出的字符，修饰键取事件的，加上 Slate 在 Windows 上不报的 Windows 键与 Num Lock；系统的自动重复照 Slate 给的 `IsRepeat`。`OnKeyChar` 的字符是页面的 keypress（`UWeftView::Text`；BMP 以外的字符两个代理项合成一个）。
- 页面用没用：页面阻止了的键（`preventDefault`）和 weft 执行了默认动作的键（移焦点、滚动、点击）是 Handled，其余 Unhandled，照 Slate 的路由往上交给游戏视口与游戏的输入，同 Chrome 把页面没用的键还给浏览器。
- 游戏自己给键：`UWeftView::Key(bDown, Code, Value, Modifiers, bRepeat)`、`Text`、`SetFocus`，给不在 `SWeftViewport` 里的 View，或游戏自己判断的键。

## 渲染

- `FWeftRenderer`（每个 weft 运行时一个）在第一次画时按当前 RHI 选后端 `IWeftRhiBackend`：
  - D3D12：weft 的 D3D12 后端（`render_backend_d3d12.h`）用 RHI 的设备，把帧录进 RHI 自己的命令列表（在 RDG 的一个 pass 里，先冲掉 RHI 的 barrier）。录完 signal 一个手动 fence：既关闭 RHI 的命令列表，让 RHI 重设它的状态，也告诉 weft 哪次录制在 GPU 上完成了。
  - Vulkan：weft 已有 Vulkan 后端（录进宿主的命令缓冲），接到 UE 的 Vulkan RHI 是下一步。
  - Metal 及主机平台：等 weft 有对应的后端；插件这一层的接口不变。
- weft 的渲染器只认它的创建线程，而 UE 5 的 RHI 在任务线程上执行命令列表的 lambda：插件给渲染器一个自己的线程（`FWeftOwnerThread`），lambda 把工作交给它并等它做完，录制仍落在命令列表里 lambda 的位置。
- Slate：控件画一个自定义元素，在 Slate 的渲染图里加 pass。控件在输出的左上角时 weft 直接画进输出；不在时（比如编辑器里的 PIE）先把那块拷进一张同样大小的纹理，weft 画在上面（`backdrop-filter` 也读得到游戏画面），再拷回去。
- 目标格式：BGRA8、RGBA8，以及 10 位的 `PF_A2B10G10R10`（UE 5 默认的后缓冲）。别的格式（HDR 输出）记一次警告、不画。
- Unreal Insights（`-trace=cpu`）里，weft 的工作记在游戏线程的 `Weft_Advance`（页面的脚本、样式、布局与场景）、`Weft_Pointer`、`Weft_HitTest`、`Weft_MakePacket`，与 `WeftRender` 线程的 `Weft_Record`（录制）下。

## 测试

自动化测试（`Source/Weft/Private/Tests`，`Weft.Keyboard.*`）在一个只放了这个插件的宿主工程里跑，编辑器不画（`-nullrhi`），所以测的是页面这一侧：`UWeftView` 的键（Slate 的键事件到页面的焦点元素、修饰键、重复、文字、阻止的键与丢掉的文字、手柄的键不算）与窗口焦点，`SWeftViewport` 的键盘焦点（Slate 的焦点进出、哪一层拿键盘、按下给焦点在页面的 mousedown 之前、`FocusView`、页面没用的键 Unhandled、代理项合成）。宿主工程：一个 `.uproject`（`"Plugins": [{"Name": "Weft", "Enabled": true}]`），`Plugins/Weft` 指向本仓库的签出（目录联接），`Config/DefaultEngine.ini` 给一张启动地图（`[/Script/EngineSettings.GameMapsSettings]` 的 `EditorStartupMap=/Engine/Maps/Entry.Entry`：没有地图的空工程在 `-nullrhi` 下启动时崩溃），再加一个空的 `Content` 目录。UE 5.8.3 上两个测试都通过（weft `input` 分支 5b017a9）。

```powershell
E:/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Build.bat UnrealEditor Win64 Development -Project=<宿主>/WeftTests.uproject -WaitMutex
E:/UnrealEngine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe <宿主>/WeftTests.uproject -ExecCmds="Automation RunTests Weft;Quit" -nullrhi -unattended -nopause -testexit="Automation Test Queue Empty" -log
```

## 限制

- 只有 Win64 + D3D12；D3D11、Vulkan RHI 下不画（记警告）。
- 输入有指针（移动、按键、滚轮、离开）与键盘；IME 与手柄还没有。Slate 不给扫描码，所以 Num Lock 关着时小键盘的键的 code 是它这时的键（End，Chrome 是 Numpad1），小键盘的 Enter 是 Enter；右 Alt 一律是 Alt。
- 字体用系统字体（weft 的默认），以及 `UWeftSubsystem::AddFont` 加的字体文件。
- 页面里的图片只有 PNG 背景图（`url()`），没有 `<img>`、JPEG 与 `data:` URL。
- 一个运行时一个渲染器：所有 View 由同一个 weft 渲染器画。
- 没有 UMG 包装（`UWidget`）与蓝图节点以外的编辑器工具。
