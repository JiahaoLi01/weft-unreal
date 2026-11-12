# weft-unreal

把 [weft](https://github.com/JiahaoLi01/weft)（HTML、CSS、DOM 与 JavaScript 的游戏 UI 运行时）接进 Unreal Engine 5 的插件。页面在游戏线程上运行，帧由 weft 对应当前 RHI 的后端画进 Slate 的输出：用的是引擎自己的 GPU 设备与命令列表，不经 CPU 拷贝，也不另开设备。

当前状态（2026-10-21）：实验版。Win64、UE 5.8、D3D12 RHI 已在 CaseKargaroo 里跑通（城市标签），也跑 weft-page 的宣传页（约 4,000 个元素的长页面、四张场景捕获每帧重画的纹理、六个锚点；3840 × 2040 下帧时的中位数是 240 fps 上限的 4.17 ms）。

## 目录

| 位置 | 内容 |
| --- | --- |
| `Weft.uplugin` | 插件描述，运行时模块 `Weft` |
| `Native/` | 把 weft 编成一个 DLL 的 CMake 工程：链接 weft 的静态库，只导出 `include/weft` 的 C ABI（`weft.def` 从头文件生成），所以 weft 的 FreeType、HarfBuzz 与 UE 的互不相见 |
| `Scripts/BuildWeft.ps1` | 用一份 weft 签出编译两个 DLL（开发版、发布版），把头文件、导入库与 DLL 放进插件 |
| `Source/ThirdParty/WeftLibrary` | 外部模块：头文件与导入库（脚本生成，不入库），按构建配置挑 DLL；没有它们时 `WITH_WEFT=0`，插件照常编译、界面不画 |
| `Source/Weft` | 运行时模块：子系统、View、Slate 控件、渲染；开发版里还有调试工具（`WITH_WEFT_DEVTOOLS`） |
| `Source/WeftEditor` | 编辑器模块：Weft 面板（Window > Log > Weft），项目设置 Plugins > Weft UI |

## 编译 weft

需要 CMake 3.25+、Visual Studio 2022；装了 Vulkan SDK 1.4.363.0 时连 Vulkan 后端一起编。weft 的 D3D12 后端在 weft 的 main 上（2026-10-17 由 `unreal` 分支合入）。

```powershell
pwsh Plugins/Weft/Scripts/BuildWeft.ps1 -WeftSource F:/Projects/weft
```

不给 `-WeftSource` 时找环境变量 `WEFT_SOURCE_DIR`，再找游戏工程旁边的 `weft` 目录。产物：

- `Binaries/ThirdParty/Weft/Win64/Dev/weft.dll`：weft 的开发版（`WEFT_DEVTOOLS=ON`，在 `Intermediate/WeftBuildDev` 里编），带 weft 的开发者工具，要了才开。编辑器和 Shipping 以外的配置用它。
- `Binaries/ThirdParty/Weft/Win64/Release/weft.dll`：weft 的发布版（`WEFT_DEVTOOLS=OFF`，在 `Intermediate/WeftBuild` 里编），没有开发者工具，也没有 socket（不导入 `WS2_32.dll`）。Shipping 用它。
- `Source/ThirdParty/WeftLibrary/{include,lib}`：两版的头文件与导出相同，一个导入库两版共用；以及记录 weft 提交的 `WEFT_VERSION`。

目标只带它用的那一个 DLL（`RuntimeDependencies`）。插件加载 DLL 时在日志里打出路径与 weft 提交。weft 更新（比如 rebase 到 weft 的主分支）后重跑脚本。

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
- 页面文件夹的 `page.json` 可以写 `"forgivingCss": true`（Tailwind 这类构建工具生成的样式表：画不出的声明丢掉并打诊断，而不是拒绝页面）、`"webPlatform": true`（console、localStorage、dataset、matchMedia；焦点与键盘每个页面都有）与 `"spatialNavigation": true`（方向键与手柄在元素间移动焦点，见“手柄”）。
- 键盘、文本框与手柄见下面几节。
- 字体与效果：`UWeftSubsystem::AddFonts(fonts.json)`（`faces` 的 `family`、`path`、`weight`、`italic`、`face`、`unicodeRange`，与 `fallback`；在第一个页面加载之前）、`AddEffects(effects.json)`（UI 着色器效果：字节码由 weft 的 `cmake/compile_effect.cmake` 编出，`BuildWeft.ps1` 把它连同外壳复制到 `Source/ThirdParty/WeftLibrary/effects/`）。
- 页面的 localStorage：`UWeftView::SetStorageItem`（加载页面之前恢复）、`GetStorage(Revision, Items)`（修订号变了才交出，游戏据此保存）。
- 锚点随这一帧的帧数据一起交给渲染线程，在录制前写入 View：锚定的元素与这一帧的 3D 画面同步，不会晚一帧。
- 页面说的话打在 `LogWeftPage`，行首是页面名（样式表所在的文件夹，`UWeftView::GetPageName`），如 `[Settings] ...`：console 的 log 为 Log、warn 与样式丢弃为 Warning、脚本异常与 console.error 为 Error；`UWeftView::OnDiagnostic` 也收到。插件自己的事（加载 DLL、开发者工具、页面载入失败）在 `LogWeft`。帧画不出时渲染器把 weft 给的原因（`weft_renderer_last_failure`）各记一次。console 只在开了 `webPlatform` 的页面上写诊断；没开的页面里 `console.log`、`console.warn` 什么也不做，`console.error` 当作异常（weft 的约定）。
- 页面文件夹里的图片：CSS 写 `background-image: url(../Textures/x.png)`（PNG），像背景图那样铺排。相对路径从页面的基准（`FWeftPage::Base`，`FromDirectory` 设为样式表的路径）算起，View 用 `FFileHelper` 读文件（打包后从 pak 读，所以页面文件夹要随包，如 `DirectoriesToAlwaysStageAsUFS`）。读不到或不是 PNG 时打一条页面诊断，那个背景不画图。
- 引擎的纹理：`UWeftSubsystem::SetTexture(Name, Texture)`，页面写 `background-image: -weft-texture(Name)`。渲染目标（`UTextureRenderTarget2D` 这类引擎能画进去的纹理，比如场景捕获每帧重画的那张）按 weft 的 live 纹理导入：引擎可以在两帧之间重画它，页面画的总是这一帧的内容；别的纹理导入后内容不能再变（要变就换个名字绑一张新的）。

## 开发者工具与调试

weft 的开发者工具（weft 的 `docs/devtools.md`）用 Chrome 自带的 DevTools 前端看游戏里的页面：DOM 与它的改动、在游戏画面上点选元素、命中的规则与计算样式、盒模型、现场改样式、Console（未捕获的异常、样式诊断、求值）。插件在编辑器和开发版里给它开关、打地址，再加一张 View 的表。

**哪些构建有**：编辑器与 Shipping 以外的配置用开发版 DLL，`WITH_WEFT_DEVTOOLS=1`。Shipping 默认用发布版 DLL，`WITH_WEFT_DEVTOOLS=0`：weft 的工具、插件的这些命令、面板与它们的字符串都不在包里，exe 也不导入 `weft_devtools_*`。要在某个 Shipping 包里带上，二选一：

- 设置：项目设置 Plugins > Weft UI > Developer Tools in Shipping，即 `Config/DefaultEngine.ini` 的

  ```ini
  [/Script/WeftEditor.WeftBuildSettings]
  bDevToolsInShipping=True
  ```

- 打包命令（只这一次）：UnrealBuildTool 的 `-ini:Engine:[/Script/WeftEditor.WeftBuildSettings]:bDevToolsInShipping=True`，经 UAT 时写成 `-ubtargs="-ini:Engine:[/Script/WeftEditor.WeftBuildSettings]:bDevToolsInShipping=True"`。

两样在编译游戏时读（`WeftLibrary.Build.cs`）；ini 文件或 UBT 的命令行变了，UBT 会重新生成，不会沿用上一次的选择。UBT 的输出里有一行 `Weft: Shipping with weft's developer tools` 时就是带上了。这样的 Shipping 包没有控制台，用命令行 `-WeftDevTools`、`-WeftDebug`。

**开**（开发版的 weft 默认不监听）：

- 控制台 `weft.DevTools`：本机 127.0.0.1，端口从 9230 起第一个空闲的；`weft.DevTools 9240` 指定端口（被占了就失败）；`weft.DevTools 0.0.0.0:9230` 让别的设备也能连（会被 Windows 防火墙问）；在监听时再输 `weft.DevTools` 重打地址；`weft.DevTools stop` 停。
- 命令行 `-WeftDevTools`、`-WeftDevTools=9240`、`-WeftDevTools=0.0.0.0:9230`；weft 自己的环境变量 `WEFT_DEVTOOLS_LISTEN` 也认。
- 编辑器的 Weft 面板上的 Start / Stop。

开了以后 `LogWeft` 打出监听的地址，以及每个有页面的 View 给 Chrome 的地址，之后载入的页面也各打一行：

```
LogWeft: Display: Weft DevTools: listening on 127.0.0.1:9230. Open a page's address below in Chrome, or chrome://inspect > Configure > 127.0.0.1:9230.
LogWeft: Display: Weft DevTools: [Settings] devtools://devtools/bundled/devtools_app.html?ws=127.0.0.1:9230/devtools/page/3-1
```

把 `devtools://` 地址粘贴进 Chrome 的地址栏；或者在 `chrome://inspect` 的 Configure 里加一次 `127.0.0.1:9230`，之后页面列在 Remote Target 下，点 inspect。`3-1` 是 View 句柄的槽与代。工具的回复在 View 推进时处理，所以页面要在画（`SWeftViewport` 每帧推进它）。改页面不用重新打包：`node ThirdParty/weft/tools/devtools_watch.mjs 127.0.0.1:9230 <页面文件夹>` 存盘即换（weft 的“热重载”）。

**日志**：页面的异常、样式诊断与 console 在 `LogWeftPage`，行首是页面名，Output Log 里按类别筛。

**View 的表**：

- `weft.Views`：先量一秒，再把表打进 `LogWeft`，监听时每个页面的地址附在后面。
- `weft.Debug 1`（命令行 `-WeftDebug`）：同一张表画在游戏画面左上方（引擎的调试文字），`weft.Debug 0` 关。开着时多一层，关了不画也不量。
- 编辑器 Window > Log > Weft（控制台 `weft.Panel`）：同一张表（每 0.5 秒），开关开发者工具、复制每个页面的地址、勾 On screen 即 `weft.Debug`。PIE 时列出游戏里的 View。

| 列 | 意思 |
| --- | --- |
| View | 页面在开发者工具里的 target（`/devtools/page/<槽>-<代>`） |
| Page | 页面名 |
| Size、Scale | 逻辑 px，每个逻辑 px 的设备 px |
| Elements | 文档里的元素数（`document.querySelectorAll('*').length`，最多每秒在页面里跑一次） |
| Game ms | weft 在游戏线程上为这个 View 做的事（推进、输入、交出帧），每帧多少 ms：上一整秒的平均 / 最慢一帧 |
| Draw ms | 在 RHI 线程上录制它的绘制，每帧多少 ms：平均 / 最慢 |
| Animated | 页面的帧自己在变（CSS 动画与过渡、requestAnimationFrame） |
| Errors、Warnings | 页面载入以来的异常与 console.error；样式丢弃与 console.warn |

耗时只在表显示时量（显示后两秒内），不看时不计时；元素数是页面里跑的一段脚本。

## 键盘

- 焦点：`SWeftViewport` 的键盘给它的一层，像浏览器窗口的键盘给它的页面：鼠标最近一次按下的那层，或 `FocusView` 给的那层，都没有时是最上面的可交互层。按在一层的页面上时控件拿到 Slate 的键盘焦点（`SetUserFocus`）；控件有 Slate 的键盘焦点、应用在前台时，那一层的页面有焦点（window 的 focus 与焦点元素的 focus，`:focus`、`:focus-visible`、`document.hasFocus()` 只在这时成立），其他层的页面没有。按在游戏上（没有页面接的地方）时游戏视口拿走焦点，页面收到 blur。`FocusView` 可以在 `BeginPlay` 里调：打包的游戏在窗口第一次显示游戏视口时（第一个关卡的 `BeginPlay` 之后）把焦点给游戏视口，控件在它的第一帧把焦点拿回来。控件不画 Slate 的焦点框，焦点由页面自己画（`:focus-visible`）。
- 按键：`OnKeyDown`、`OnKeyUp` 交给那层的页面（`UWeftView::Key`）：code 是系统键盘布局把 Slate 给的 Windows 虚拟键放在的物理键（`weft_keyboard_code`；Slate 不给扫描码），key 是布局用它打出的字符，修饰键取事件的，加上 Slate 在 Windows 上不报的 Windows 键与 Num Lock；系统的自动重复照 Slate 给的 `IsRepeat`。`OnKeyChar` 的字符是页面的 keypress（`UWeftView::Text`；BMP 以外的字符两个代理项合成一个）。
- 页面用没用：页面阻止了的键（`preventDefault`）和 weft 执行了默认动作的键（移焦点、滚动、点击、编辑文本框）是 Handled，其余 Unhandled，照 Slate 的路由往上交给游戏视口与游戏的输入，同 Chrome 把页面没用的键还给浏览器。可编辑文本框里打字的键按下时就是页面的（玩家在聊天框里打字时角色不动）。
- 游戏自己给键：`UWeftView::Key(bDown, Code, Value, Modifiers, bRepeat, bIme)`、`Text`、`SetFocus`，给不在 `SWeftViewport` 里的 View，或游戏自己判断的键。

## 文本框与输入法

- 页面的 `<input>`（文字类型）与 `<textarea>` 照 Chrome 编辑（weft 做：打字、选择、鼠标、事件）。鼠标事件带修饰键，Shift+点击扩展选区。
- 输入法：`SWeftViewport` 有一个 `ITextInputMethodContext`（`GetTextInputMethodContext`），在键盘那层的页面里可编辑的文本框有焦点（不是密码框，同 Chrome）、控件有 Slate 的键盘焦点时，它是 Slate 输入法系统（Windows 上 IMM 或 TSF）的活动上下文，输入法开着；否则关着（UE 默认关着输入法，只在有活动上下文时打开）。候选窗在文本框光标下面（`GetTextBounds` 给光标在桌面上的矩形，光标移动时告诉输入法布局变了）。
- weft 自己保存文本框的值与选区，所以输入法编辑的文档只有它正在组合的串（以及到下一次交给页面之前它上屏的文字）：组合串的变化在输入法这次编辑完之后交给页面（按键之后、下一个键之前、每帧），作为页面的组合（compositionstart、compositionupdate、input），组合结束时上屏（compositionend）。组合的范围用输入法给的；不给时（微软拼音经 UE 的 TSF 文本存储就从不给）是这次组合开始以来输入法放进来的文字。UE 的 TSF 存储对不经组合直接插入的文字也开始一次组合，而它不会结束，所以这样的文字要到下一次组合结束（或文本框失焦）才上屏。没有组合时文档清空并告诉输入法。页面自己结束组合时（脚本改值、文本框失焦），输入法的这次组合作废（关掉再打开输入法）。
- 输入法吃掉的键 Windows 给的是 `VK_PROCESSKEY`：页面收到 key 为 "Process"、keyCode 229 的 keydown（没有 code：Slate 不给扫描码，Chrome 的 code 是物理键的），是页面的（Handled）；它的 keyup 有真正的键时照常给页面（`isComposing`）。
- `UWeftView::GetTextInput`、`Compose`、`CommitText`：游戏自己读输入法时用（`weft_view_text_input`、`weft_view_composition`、`weft_view_commit_text`）。
- 在宣传页的 Unreal 播放器里用真实的微软拼音试过（2026-10-22）：组合串、候选窗在光标下面、空格上屏、组合中的 Enter 只上屏字母（页面不当作发送）、逗号键的全角“，”。

## 手柄

- 页面看得到手柄：每个 View 的页面有 `navigator.getGamepads()` 与 `gamepadconnected`、`gamepaddisconnected`，照 Chrome：某个手柄按下按钮或推过一半摇杆之前一个也不显示，Gamepad 是快照（weft 做）。`FWeftGamepadInput`（子系统把它注册为 Slate 的第一个输入预处理器，游戏与控件之前）收 Slate 给的手柄事件，按 Gamepad API 的标准布局给每个 View（`UWeftView::Gamepad`）：一个手柄从它的第一次输入起占一个槽（0–3；页面在手势之前本来也看不到），id 是 Chrome 给 XInput 手柄的 "Xbox 360 Controller (XInput STANDARD GAMEPAD)"；按钮的变化随到随给，摇杆与扳机的值在这一帧的输入结束时（`FinishedInputThisFrame` 里预处理器的 `Tick`）一起给（摇杆的 x、y 是分开来的），摇杆 y 向下、不加死区，扳机过 30/255 算按下，同 Chrome；平台的输入设备映射说手柄断开时页面收到 `gamepaddisconnected`。
- 空间导航：`page.json` 的 `"spatialNavigation": true`（`FWeftPage::bSpatialNavigation`）开着时，方向键照 Chrome 的 `--enable-spatial-navigation` 把焦点移到那个方向上最近的元素，指针跟着走，Escape 让它失焦（weft 做）；有键盘焦点的那层页面把十字键与左摇杆（推过一半）当方向键，A 是 Enter，B 是 Escape，按住的方向 0.5 s 后每 0.1 s 重复（weft 自己重复，Slate 对手柄按钮的重复不给页面）。
- 页面用了的不是游戏的：页面用了一个按钮的按下（方向键移了焦点、Enter 点击、Escape 失焦，或监听器阻止了），预处理器吞掉它（连同 Slate 对它的重复）直到它抬起；页面用了左摇杆的一个方向时，从下一帧起吞掉左摇杆的值与 Slate 为它的方向发的键，直到摇杆回到一半以内（这一帧的值在输入结束时才交给页面，所以推过去的那一帧游戏还会收到）。
- Slate 的导航（页面没用的方向键、Tab、十字键）不把键盘焦点从控件带走（`OnNavigation` 停住），同浏览器的键不离开页面。
- 游戏自己读手柄时：`UWeftView::Gamepad(FWeftGamepadState)`。

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

自动化测试（`Source/Weft/Private/Tests`，`Weft.Keyboard.*`、`Weft.TextInput.*`、`Weft.Gamepad.*`）在一个只放了这个插件的宿主工程里跑，编辑器不画（`-nullrhi`），所以测的是页面这一侧：`UWeftView` 的键（Slate 的键事件到页面的焦点元素、修饰键、重复、文字、阻止的键与丢掉的文字、手柄的键不算）与窗口焦点，`SWeftViewport` 的键盘焦点（Slate 的焦点进出、哪一层拿键盘、按下给焦点在页面的 mousedown 之前、`FocusView`、页面没用的键 Unhandled、代理项合成），以及输入法的上下文：照 IMM 与 TSF 调用它的顺序（组合、上屏、取消、页面结束的组合；TSF 照微软拼音经 UE 的存储时日志里的实际调用：不给范围、SetText 退回 InsertTextAtSelection、标点是一次编辑里开始又结束的组合）、候选窗的位置、"Process" 键与打字的键；手柄：给 `UWeftView` 的状态（手势之前不显示、断开、空间导航的页面上十字键与左摇杆移焦点、A 点击、B 失焦、没有焦点时没有键、没开空间导航的页面没有键），`FWeftGamepadInput` 收 Slate 的事件（页面用了的按钮连同重复与抬起被吞掉、页面不用的按钮与别的键不吞、摇杆的值在输入结束时一起给、页面有左摇杆后吞掉它的值与方向键而不吞右摇杆、扳机过 30/255 按下、第二个手柄占下一个槽、断开）。输入法的开关（Slate 输入法系统的活动上下文）要控件在窗口里，自动化测试里不测。开发版里还有 `Weft.DevTools.*`：插件跑的是 weft 的开发版（`weft_get_build_info`）；`StartDevTools` 监听、日志里打出页面的地址，weft 的 `/json/list` 列出的 target 就是插件给的那个，再开一次、写错地址、端口超出都被拒，停了以后端口没人听；表里的页面名、大小、元素数、异常与样式丢弃的计数，以及它们在 `LogWeftPage` 里带着页面名。宿主工程：一个 `.uproject`（`"Plugins": [{"Name": "Weft", "Enabled": true}]`），`Plugins/Weft` 指向本仓库的签出（目录联接），`Config/DefaultEngine.ini` 给一张启动地图（`[/Script/EngineSettings.GameMapsSettings]` 的 `EditorStartupMap=/Engine/Maps/Entry.Entry`：没有地图的空工程在 `-nullrhi` 下启动时崩溃），再加一个空的 `Content` 目录。UE 5.8.3 上五个测试都通过（weft `input` 分支 651940f）。

```powershell
E:/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Build.bat UnrealEditor Win64 Development -Project=<宿主>/WeftTests.uproject -WaitMutex
E:/UnrealEngine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe <宿主>/WeftTests.uproject -ExecCmds="Automation RunTests Weft;Quit" -nullrhi -unattended -nopause -testexit="Automation Test Queue Empty" -log
```

## 限制

- 只有 Win64 + D3D12；D3D11、Vulkan RHI 下不画（记警告）。
- 输入有指针（移动、按键、滚轮、离开）、键盘与输入法、手柄（Slate 读到的，id 一律是 XInput 的；没有震动与 Home 键）。输入法吃掉的键没有 code。Slate 不给扫描码，所以 Num Lock 关着时小键盘的键的 code 是它这时的键（End，Chrome 是 Numpad1），小键盘的 Enter 是 Enter；右 Alt 一律是 Alt。
- 字体用系统字体（weft 的默认），以及 `UWeftSubsystem::AddFont` 加的字体文件。
- 页面里的图片只有 PNG 背景图（`url()`），没有 `<img>`、JPEG 与 `data:` URL。
- 一个运行时一个渲染器：所有 View 由同一个 weft 渲染器画。
- 开发者工具：一个进程一个端口（所有 View 共用）；只监听 127.0.0.1 或所有网卡；控制台里展开对象（`Runtime.callFunctionOn`）、断点与性能面板 weft 还没有（weft 的 `docs/devtools.md`“尚缺”）。热重载按 `page.json` 或已加载页面的脚本名认页面，游戏自己拼起来的页面（比如样式表前面接了一份主题）推过去时只有文件夹里的那部分。
- 没有 UMG 包装（`UWidget`）与蓝图节点以外的编辑器工具。
