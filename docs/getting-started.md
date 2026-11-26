# 接入与页面开发

本文介绍如何在 Unreal Engine 5 项目中构建 `weft-unreal`、加载页面，以及使用字体、自定义着色器效果、图片、引擎纹理和持久化存储。

## 1. 构建 Weft 动态库

构建环境需要 **CMake 3.25+** 与 **Visual Studio 2022**（安装 Vulkan SDK 1.4.363.0 时会一并编出 Vulkan 后端）。在项目根目录运行：

```powershell
pwsh Plugins/Weft/Scripts/BuildWeft.ps1 -WeftSource F:/Projects/weft
```

如果不指定 `-WeftSource`，脚本会依次查找环境变量 `WEFT_SOURCE_DIR` 以及游戏工程同级目录下的 `weft` 文件夹。

脚本会在 `Intermediate/WeftBuildDev` 与 `Intermediate/WeftBuild` 中分别构建两套共享同一套 C ABI 导出表的 DLL：

| 产物路径 | 说明 |
| --- | --- |
| `Binaries/ThirdParty/Weft/Win64/Dev/weft.dll` | 开发版（`WEFT_DEVTOOLS=ON`），用于编辑器与除 Shipping 外的所有配置 |
| `Binaries/ThirdParty/Weft/Win64/Release/weft.dll` | 发布版（`WEFT_DEVTOOLS=OFF`），无调试代码与 `WS2_32.dll` 依赖，用于 Shipping |
| `Source/ThirdParty/WeftLibrary/{include,lib,WEFT_VERSION}` | 公开头文件、共享导入库与当前构建对应的 Weft 提交记录 |
| `Source/ThirdParty/WeftLibrary/effects/` | 离线效果编译脚本（`compile_effect.cmake`、`check_effect.cmake`、`scene.hlsl`） |

`WeftLibrary.Build.cs` 会根据构建目标仅将对应的那一份 DLL 加入 `RuntimeDependencies`。若尚未运行构建脚本，模块会以 `WITH_WEFT=0` 静默通过编译（此时 View 不输出画面），不会阻塞工程编译。

更新 Weft 源码后重新执行脚本即可。

## 2. 创建 View 与加载页面

```cpp
UWeftView* View = UWeftSubsystem::Get()->CreateView();
View->LoadDirectory(FPaths::ProjectContentDir() / TEXT("UI/Hud"));
View->OnMessage.AddUObject(this, &AMyHud::OnPageMessage);

TSharedRef<SWeftViewport> Viewport = SNew(SWeftViewport).View(View);
GetWorld()->GetGameViewport()->AddViewportWidgetContent(Viewport, 10);
```

页面目录支持两种组织方式：

- **清单模式（`page.json`）**：指定脚本执行顺序、样式表、关键帧、根节点选择器及特性开关：

  ```json
  {
    "scripts": ["vendor.js", "page.js"],
    "stylesheet": "page.css",
    "keyframes": "keyframes.css",
    "root": "#root",
    "forgivingCss": true,
    "webPlatform": true,
    "spatialNavigation": false
  }
  ```

- **默认模式**：目录中没有 `page.json` 时，自动读取 `page.js`、`page.css` 与可选的 `keyframes.css`。

各特性开关含义如下：

| 开关 | 说明 |
| --- | --- |
| `forgivingCss` | 按浏览器规则处理 `@media`、`@supports`、层叠层、CSS 嵌套及 Tailwind 等构建产物；场景无法绘制的声明会记录警告并忽略，而非拒绝整页 |
| `webPlatform` | 启用 `console` 诊断、`localStorage` / `sessionStorage`、`dataset`、`matchMedia` 等浏览器平台接口 |
| `spatialNavigation` | 启用方向键与手柄空间导航 |
| `crpgBridge` | 启用兼容 `window.ue.sendEvent` / `__ue_dispatch` / `__vm_update` 的桥接层 |

### 缩放与分辨率适配

`SWeftViewport` 每帧依据自身几何尺寸自动调整 View 大小。可以通过 `View->SetContentScale(Scale)` 实现游戏内的 UI 缩放设置：

- 逻辑尺寸 = Slate 逻辑尺寸 ÷ `ContentScale`
- 设备缩放 = Slate DPI 缩放 × `ContentScale`

文字与矢量图形仍按物理分辨率光栅化，缩放后不会模糊。

## 3. 宿主通信与锚点

### 事件、视图模型与异步调用

```cpp
// 推送事件（bLatest = true 时覆盖队列中尚未送达的同名事件）
View->Emit(TEXT("units"), Json, /*bLatest*/ true);

// 合并更新视图模型（页面每帧最多收到一次批处理变更）
View->SetModel(TEXT("camera"), TEXT("{\"tier\":\"Mid\"}"));

// 响应页面的 weft.engine.call(name, payload)
void AMyHud::OnPageMessage(const FString& Name, const FString& Json, uint64 CallId)
{
    if (CallId != 0 && Name == TEXT("inventory.load"))
    {
        View->CompleteCall(CallId, true, TEXT("{\"items\":[]}"));
    }
}
```

对于施法条、冷却遮罩等每帧连续变化的动效，建议只在触发时向页面传入起始时间与总时长，让页面自行推进。起始时间直接取 `View->GetPageTime()`（单位秒，与页面内 `performance.now()` 和 `requestAnimationFrame` 同源）。

### 3D 世界锚点

在 HTML 元素上标记 `data-anchor="<Key>"`，C++ 侧每帧提交投影后的屏幕坐标：

```cpp
View->SetAnchors(Anchors); // Key, Position（逻辑 px）, Scale, Depth, bHidden, bBehind
```

锚点数据随当前帧快照直接传递到渲染线程，在录制绘制命令前写入 View，既不触发 JavaScript 或重新布局，也与当前帧 3D 画面严格同步。

## 4. 字体与自定义着色器效果

### 字体清单

默认情况下，Weft 自动使用 Windows 系统字体与中文/多语言回退链。需要加载项目自带字体时，在第一个页面加载前调用：

```cpp
UWeftSubsystem::Get()->AddFonts(FPaths::ProjectContentDir() / TEXT("UI/fonts.json"));
```

`fonts.json` 支持按字重、斜体、字体集索引与 `unicodeRange` 组合字体族，并可追加全局回退字体：

```json
{
  "faces": [
    { "family": "GameUI", "path": "NotoSansSC-Regular.otf", "weight": 400 },
    { "family": "GameUI", "path": "Inter-Bold.ttf", "weight": 700, "unicodeRange": "U+0020-007F" }
  ],
  "fallback": ["GameUI"]
}
```

也可以通过 `AddFont(...)` 与 `AddFallbackFamily(...)` 逐条注册。

### 自定义 UI 着色器效果（`-weft-effect`）

将 HLSL 像素效果用插件自带的脚本离线编译为 DXBC / SPIR-V 字节码（无需保留完整 Weft 源码树）：

```powershell
cmake -DSOURCE=path/to/foil.hlsl -DOUTPUT=Content/UI/Effects/foil -P Plugins/Weft/Source/ThirdParty/WeftLibrary/effects/compile_effect.cmake
```

在运行时通过清单注册：

```cpp
UWeftSubsystem::Get()->AddEffects(FPaths::ProjectContentDir() / TEXT("UI/Effects/effects.json"));
```

注册后即可在 CSS 中通过 `background-image: -weft-effect(foil)` 使用，并用自定义属性传递着色器参数（详见 Weft 主仓库 `docs/effects.md`）。

## 5. 图片资源与引擎纹理

### 页面图片（`url()`）

CSS 中的 `background-image: url(../Textures/icon.png)` 会基于样式表路径解析，由插件通过 `FFileHelper` 读取。打包发布时，将页面目录加入项目的 `DirectoriesToAlwaysStageAsUFS`，即可直接从 `.pak` 读取。文件缺失或格式无效时会输出一条页面诊断，不影响其余元素绘制。

### 引擎纹理（`-weft-texture`）

将引擎纹理按名称暴露给所有页面：

```cpp
UWeftSubsystem::Get()->SetTexture(TEXT("minimap"), MinimapRenderTarget);
```

页面中写 `background-image: -weft-texture(minimap)` 即可按标准 CSS 背景图规则铺排、圆角裁切与混合：

- `UTextureRenderTarget2D` 等动态渲染目标会按 live 纹理导入，每帧重画后页面自动显示最新内容。
- 静态纹理导入后内容假定不变；如需替换内容，绑定新纹理或更换名称即可。
- 传入 `nullptr` 解绑该名称。

## 6. 持久化存储（`localStorage`）

开启 `webPlatform: true` 的页面支持标准 `localStorage`，数据在同一个 View 的多次页面加载之间保留。宿主可在加载页面前恢复存档，并在修订号变化时写盘：

```cpp
// 加载页面前恢复数据
View->SetStorageItem(TEXT("theme"), TEXT("dark"));

// 定期或在关卡切换时检查是否发生变化
TMap<FString, FString> Items;
if (View->GetStorage(SavedRevision, Items))
{
    SaveUserUiSettings(Items);
}
```
