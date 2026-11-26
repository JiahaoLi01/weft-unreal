# 渲染管线与测试

## 1. 渲染架构

`FWeftRenderer`（每个 Weft 运行时实例对应一个）在首次绘制时根据当前 UE RHI 创建后端实现（`IWeftRhiBackend`）：

- **D3D12 RHI**：直接复用引擎的 `ID3D12Device`，在 RDG Pass 内先刷新 RHI 资源屏障，再通过 Weft D3D12 后端将绘制命令直接录进 RHI 当前命令列表。录制结束后发出一次手动 Fence 信号，既让 RHI 正确重置内部缓存状态，也为 Weft 提供精确的 GPU 完成回收点。
- **独立录制线程**：Weft 渲染器要求所有录制操作在创建线程执行，而 UE5 RHI 任务运行在动态任务线程上。插件维护专用的 `FWeftOwnerThread` 录制线程，由 RHI Lambda 同步派发录制工作，保证命令依然精确落在当前命令列表位置。

### Slate 绘制与离屏拷贝

`SWeftViewport` 通过自定义 Slate 绘制元素向 Slate 渲染图注入 Pass：

- 当控件位于渲染目标左上角时，Weft 直接绘制到目标缓冲。
- 当控件存在偏移（例如编辑器内嵌 PIE 视口）时，先将对应区域拷贝至同尺寸临时纹理，供 Weft 绘制与 `backdrop-filter` 采样，绘制完成后再拷回输出目标。

支持的渲染目标格式包括 `BGRA8`、`RGBA8` 以及 UE5 默认的 10 位 `PF_A2B10G10R10`。其他格式（如 HDR 输出）会记录一次警告并跳过绘制。

## 2. Unreal Insights 性能标签

启用 `-trace=cpu` 后，可在 Unreal Insights 中直接定位各阶段 CPU 耗时：

| 线程 | 标签 | 说明 |
| --- | --- | --- |
| GameThread | `Weft_Advance` | 脚本执行、增量样式层叠、布局计算与场景构建 |
| GameThread | `Weft_Pointer` / `Weft_HitTest` | 指针派发与命中测试 |
| GameThread | `Weft_MakePacket` | 获取不可变帧快照与打包锚点 |
| `WeftRender` | `Weft_Record` | 渲染命令准备与 GPU 命令列表录制 |

## 3. 自动化测试

自动化测试位于 `Source/Weft/Private/Tests/`，覆盖以下测试集：

- `Weft.Keyboard.*`：键盘事件映射、修饰键、自动重复、字符与代理项合并、焦点切换与输入路由。
- `Weft.TextInput.*`：IMM / TSF 输入法生命周期、候选窗定位、`VK_PROCESSKEY`、组合上屏与取消。
- `Weft.Gamepad.*`：手柄连接手势、空间导航移动与重复、按键与左摇杆拦截、扳机阈值与多手柄槽位。
- `Weft.DevTools.*`（开发版）：构建标志检查、DevTools 启停与 `/json/list`、`/json/version` 接口校验、View 监控表与 `LogWeftPage` 日志计数。

测试可在无渲染环境（`-nullrhi`）下运行，只需准备一个启用了 `Weft` 插件的轻量宿主 `.uproject`（通过目录联接指向本仓库并在 `Config/DefaultEngine.ini` 设置 `EditorStartupMap=/Engine/Maps/Entry.Entry`）：

```powershell
# 编译测试宿主
E:/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Build.bat UnrealEditor Win64 Development -Project=<Host>/WeftTests.uproject -WaitMutex

# 运行全部 Weft 自动化测试
E:/UnrealEngine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe <Host>/WeftTests.uproject `
  -ExecCmds="Automation RunTests Weft;Quit" -nullrhi -unattended -nopause -testexit="Automation Test Queue Empty" -log
```

## 4. 当前边界与限制

- **平台与图形后端**：当前支持 Windows x64 + D3D12 RHI；在 D3D11 或 Vulkan RHI 下会记录警告且不绘制（UE Vulkan RHI 与 Metal 适配待接入）。
- **键码限制**：因 Slate 不提供原始扫描码，Num Lock 关闭时小键盘键的 `code` 按其当前逻辑键上报（如 `End`），小键盘回车上报为 `Enter`，右 Alt 统一映射为 `Alt`，输入法拦截的按键不携带物理 `code`。
- **手柄限制**：经由 Slate 读取，设备 ID 统一按 XInput 映射，暂不支持震动与 Home 键。
- **渲染器共享**：单个 Weft 运行时下所有 View 共用一个 Weft 渲染器实例。
- **DevTools**：每个进程共用一个监听端口，支持 `127.0.0.1` 或 `0.0.0.0`；热重载通过 `page.json` 或脚本名匹配页面目录。
- **UMG 封装**：当前提供 Slate 控件（`SWeftViewport`）与 C++ API，暂未提供 `UWidget` 蓝图包装。
