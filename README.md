<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/JiahaoLi01/weft/main/docs/images/weft-logo-dark.gif">
    <img src="https://raw.githubusercontent.com/JiahaoLi01/weft/main/docs/images/weft-logo-light.gif" width="320" alt="Weft">
  </picture>
</p>

<h3 align="center">用 Web，写 Unreal 界面。</h3>

<p align="center">
  <strong>Unreal Engine 5</strong> · <strong>Win64</strong> · <strong>D3D12 RHI &amp; RDG</strong> · <strong>Slate</strong>
</p>

---

**weft-unreal** 将 [Weft](https://github.com/JiahaoLi01/weft) 嵌入式 Web UI 运行时集成到 Unreal Engine 5。界面运行在游戏线程，渲染直接录进 UE 自己的 D3D12 RHI 命令列表与 Slate 渲染图。没有独立浏览器进程，没有 CPU 像素拷贝，也不引入额外 GPU 设备。

<p align="center">
  <img src="https://raw.githubusercontent.com/JiahaoLi01/weft/main/docs/images/showcase/dark-fantasy-rpg.png" width="99%" alt="CRPG UI">
</p>

## 为什么是 weft-unreal

- **直入 RDG 与 Slate。** `SWeftViewport` 通过自定义 Slate 绘制元素接入 D3D12 渲染管线，支持 BGRA8、RGBA8 与 10 位 `PF_A2B10G10R10` 后缓冲，毛玻璃与混合效果直接读取游戏画面。
- **3D 与界面，零拷贝互通。** 渲染线程直接更新 3D 世界锚点（`data-anchor`），与当前帧画面严格同步；`SetTexture` 将 `UTextureRenderTarget2D` 等引擎纹理零拷贝共享给 CSS `-weft-texture()`，自定义 HLSL 着色器通过 `-weft-effect()` 在界面上实时运行。
- **原生键鼠、输入法与手柄。** 自动与游戏视口分流指针和键盘焦点，内建 Windows IMM / TSF 中文输入法候选窗跟随，以及基于手柄与方向键的空间导航。
- **开发连 Chrome，发布干干净净。** 编辑器与非 Shipping 配置自动加载带 DevTools 的开发版 DLL，支持 Chrome DevTools 审查、热重载与内置性能面板；Shipping 包默认切换为纯净发布版 DLL，彻底移除调试代码与 `WS2_32.dll`。

## 编译与安装

将本仓库放到游戏工程的 `Plugins/Weft`，准备好一份 [Weft](https://github.com/JiahaoLi01/weft) 源码目录（需 CMake 3.25+ 与 Visual Studio 2022），编译 Dev / Release 双构型 DLL：

```powershell
pwsh Plugins/Weft/Scripts/BuildWeft.ps1 -WeftSource F:/Projects/weft
```

重新生成工程并编译即可。无需手动配置第三方依赖，插件仅导出 `include/weft` 纯 C ABI，不会与 UE 内置的 FreeType 或 HarfBuzz 冲突。

## 快速上手

```cpp
// 创建 View 并加载页面目录（page.json，或 page.js + page.css）
UWeftView* View = UWeftSubsystem::Get()->CreateView();
View->LoadDirectory(FPaths::ProjectContentDir() / TEXT("UI/Hud"));

// 接收页面消息与异步调用（weft.engine.send / call）
View->OnMessage.AddUObject(this, &AMyHud::OnPageMessage);

// 挂载到游戏视口
GetWorld()->GetGameViewport()->AddViewportWidgetContent(
    SNew(SWeftViewport).View(View), 10);

// 向页面推送视图模型与事件
View->SetModel(TEXT("player"), TEXT("{\"hp\":73}"));
View->Emit(TEXT("units"), Json, /*bLatest*/ true);

// 绑定 3D 世界锚点与引擎渲染目标
View->SetAnchors(Anchors);
UWeftSubsystem::Get()->SetTexture(TEXT("minimap"), MinimapRenderTarget);
```

## 文档

| 文档 | 内容 |
| --- | --- |
| [接入与页面开发](docs/getting-started.md) | 构建双 DLL、页面清单、字体与自定义着色器效果、引擎纹理、`localStorage` 与宿主通信 |
| [输入系统](docs/input.md) | 指针路由（`Element` / `Cursor`）、键盘焦点、Windows IMM / TSF 输入法、手柄与空间导航 |
| [开发者工具与性能面板](docs/devtools.md) | Dev / Release 双构型、Shipping 开关、Chrome DevTools、热重载、控制台命令与编辑器面板 |
| [渲染管线与测试](docs/rendering-and-testing.md) | D3D12 RHI / RDG 录制、Slate 绘制、Unreal Insights 采样、自动化测试与当前边界 |

## 目录结构

| 目录 | 说明 |
| --- | --- |
| `Weft.uplugin` | 插件清单（运行时模块 `Weft`、编辑器模块 `WeftEditor`） |
| `Native/` | 将 Weft 静态库编译为纯 C ABI `weft.dll` 的 CMake 工程 |
| `Scripts/BuildWeft.ps1` | 一键构建 Dev / Release 双构型 DLL 并同步头文件、导入库与效果编译脚本 |
| `Source/ThirdParty/WeftLibrary` | 外部库规则，按构建目标自动选择 Dev 或 Release DLL |
| `Source/Weft` | 运行时核心：子系统、View、Slate 控件、输入预处理、D3D12 渲染与调试工具 |
| `Source/WeftEditor` | 编辑器面板（`Window > Log > Weft`）与项目设置（`Plugins > Weft UI`） |
