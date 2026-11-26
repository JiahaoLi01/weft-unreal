# 输入系统

`weft-unreal` 对接了 Unreal Engine 的 Slate 输入链路，涵盖指针路由、键盘焦点、Windows 原生输入法（IMM / TSF）与游戏手柄空间导航。

## 1. 指针路由

在 `SWeftViewport` 中添加多个页面层时，每一层可独立指定 `EWeftPointerRouting` 路由策略：

| 模式 | 行为 | 适用场景 |
| --- | --- | --- |
| `Element`（默认） | 指针落在除页面根节点外的任意元素上即由页面接管 | 常规面板、背包、弹窗、HUD 控件 |
| `Cursor` | 仅当命中元素的 CSS `cursor` 为可交互值（非 `auto`、`default`、`none`）时由页面接管 | 全屏覆盖层中夹杂非交互视觉装饰，空白区域需穿透给游戏世界 |

补充规则：

- 滚轮事件在指针位于页面元素上方时都会交给页面；在 `Cursor` 模式下，若命中元素为默认光标，游戏视口也会收到滚轮事件。
- 指针未命中页面时，事件透传给下层控件或游戏视口。
- `IsPointerOverPage` 按当前帧首次移动的位置判定。

### 移动事件合并

Slate 会在高回报率鼠标或低帧率下于单帧内派发多次移动事件。为避免重复触发命中测试与脚本回调：

- `SWeftViewport` 每帧最多向页面提交一次最终位置的 `pointermove`（与浏览器对齐动画帧的行为一致）。
- 在按键、滚轮或离开事件到达前，会先刷新缓存中的最后一次移动位置，确保事件顺序准确。
- 鼠标静止但底层控件移动导致 Slate 重复发送移动事件时，页面最多每 0.1 秒同步一次命中与悬停状态。

## 2. 键盘与焦点

`SWeftViewport` 按单层焦点模型分发键盘事件，行为接近浏览器窗口：

- **焦点获取**：点击某一层的页面元素时，控件获取 Slate 用户焦点（`SetUserFocus`），对应页面获得焦点（触发 `window` / 元素 `focus`，匹配 `:focus`、`:focus-visible`，`document.hasFocus()` 返回 `true`）。
- **焦点释放**：点击穿透到游戏世界时，焦点交还给游戏视口，页面收到 `blur`。
- **主动聚焦**：需要在游戏启动或打开菜单时立刻让页面接收键盘，可调用 `Viewport->FocusView(View)`。即便在 `BeginPlay` 中调用（早于视口首次夺取焦点），控件也会在首帧重新将焦点切回该 View。
- **焦点视觉**：控件不绘制 Slate 默认的虚线焦点框，由页面通过 `:focus-visible` 自主控制样式。

### 按键消费规则

- 若页面调用了 `preventDefault()`，或 Weft 执行了默认交互（如 `Tab` 切焦点、`Enter` / `Space` 激活按钮、方向键滚动、文本框编辑），Slate 收到 `FReply::Handled()`。
- 页面未消费的按键返回 `FReply::Unhandled()`，继续交给游戏输入系统。在聊天框输入文字时，移动键被文本框消费，角色不会误动。
- `OnKeyChar` 输入的非 BMP 字符（如 Emoji）会自动合并高低代理项后再传入页面。

对于未挂载在 `SWeftViewport` 中的 View（例如渲染在 3D 表面上的界面），可直接调用 `UWeftView::Key(...)`、`Text(...)` 和 `SetFocus(...)` 自行转发输入。

## 3. 文本框与 Windows 输入法

页面中的 `<input>`（文本类）与 `<textarea>` 内置完整的光标、选区、剪贴板与 `Shift + 点击` 扩选支持。

`SWeftViewport` 实现了 `ITextInputMethodContext`，自动接入 Slate 的 Windows IMM / TSF 输入法管理：

- **自动开关**：仅当焦点位于页面可编辑的非密码文本框、且控件持有 Slate 键盘焦点时激活输入法；失焦或切入密码框时自动关闭输入法，避免在常规游玩时误触中文输入。
- **候选窗定位**：通过 `GetTextBounds` 实时汇报光标的桌面坐标，候选窗准确贴合在输入框光标下方。
- **组合与上屏**：输入法组合过程完整派发 `compositionstart`、`compositionupdate`、`input` 与 `compositionend`；组合期间按下 `Enter` 只确认字母上屏，不会误触页面的提交逻辑。若脚本修改文本框值或主动失焦，插件会自动重置当前输入法组合。
- **进程键处理**：输入法拦截的按键（`VK_PROCESSKEY`）会以 `key = "Process"`、`keyCode = 229` 传入页面并标记为 `Handled`。

> UE 的 TSF 文本存储实现存在一项引擎特性：部分不经组合直接插入的文字仍会触发一次未显式结束的组合，因此该类字符可能延迟到下一次组合结束或输入框失焦时完成最终上屏；常规拼音组合与空格上屏不受影响。

如果游戏自行接管输入法，可通过 `UWeftView::GetTextInput()`、`Compose()`、`CommitText()` 与引擎交互。

## 4. 手柄与空间导航

### Gamepad API

`UWeftSubsystem` 在初始化时注册 `FWeftGamepadInput` 作为 Slate 优先级最高的输入预处理器，为所有页面提供标准 `navigator.getGamepads()`、`gamepadconnected` 与 `gamepaddisconnected`：

- 遵循浏览器手势规则：在玩家首次按下任意按键或推动摇杆过半之前，手柄对页面不可见。
- 最多支持 4 个槽位（0–3），按首次输入顺序分配，映射遵循标准 XInput 布局（摇杆 Y 轴向下、不额外叠加死区、扳机超过 `30/255` 判定按下）。
- 按钮状态即时派发，模拟摇杆与扳机值在当前帧输入结束时（`FinishedInputThisFrame`）合并提交。

### 空间导航

在 `page.json` 中启用 `"spatialNavigation": true`（或设置 `FWeftPage::bSpatialNavigation = true`）后：

- 方向键、手柄十字键与左摇杆（推过一半）将焦点移向对应方向最近的可聚焦元素，并同步更新悬停状态。
- 手柄 **A** 等效于 `Enter`（激活/点击），**B** 等效于 `Escape`（失焦）。
- 长按方向超过 0.5 秒后，Weft 按每 0.1 秒自动重复移动（插件会过滤 Slate 自带的手柄按钮重复事件）。

### 防止游戏误触

- 若页面消费了某个手柄按钮的按下事件，预处理器会持续拦截该按钮直至抬起，避免同一按键同时触发游戏动作。
- 若页面消费了左摇杆方向，从下一帧起拦截左摇杆数值及 Slate 生成的对应方向导航键，直至摇杆回中（右摇杆不受影响）。
- `SWeftViewport::OnNavigation` 会阻断 Slate 自身的方向键/Tab 焦点逃逸，防止焦点意外跳出页面。

如需手动喂入自定义手柄状态，可调用 `UWeftView::Gamepad(FWeftGamepadState)`。
