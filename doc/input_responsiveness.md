# 鼠标拖动响应改进

## 状态

- 代码实现：完成
- 自动验证：完成
- 人工手感验收：2026-08-15 用户确认通过
- Git 提交/推送：用户已授权，纳入本次验收收口提交

## 问题判断

原实现没有在拖动时触发模型、材质、descriptor、shader 或 pipeline 重建。卡顿感主要来自三点：

1. 相机输入在 `recordFrame()` 内更新，发生在 timeline 等待和阻塞式 swapchain acquire 之后，输入响应直接受 GPU/WSI 帧节奏影响。
2. 每个渲染帧只读取一次当前鼠标位置，不能显式累积一次事件轮询中收到的全部位移。
3. Win32 普通鼠标移动以整数像素事件为主，原实现直接应用位移，没有时间相关的平滑。

## 本次实现

1. `Renderer::drawFrame()` 一进入就计算 delta time 并更新相机，位置早于 deletion flush、swapchain 检查、timeline wait 和 image acquire。
2. 使用 GLFW cursor-position callback 累积两次渲染帧之间收到的全部鼠标位移，不再每帧调用 `glfwGetMouseButton()`/`glfwGetCursorPos()` 采样拖动。
3. 左键按下时使用 `GLFW_CURSOR_DISABLED` 捕获鼠标；平台支持时开启 `GLFW_RAW_MOUSE_MOTION`，松开后恢复正常光标。
4. 对累计位移使用 `35 s^-1` 指数积压滤波，半衰期约 20ms。滤波按时间计算，目标是削弱整数位移的颗粒感，同时让未应用位移逐步耗尽而不是永久丢失。
5. 相机 delta time 限制为最多 100ms，避免最小化、断点或偶发长帧导致键盘缩放突跳。

Vulkan 所需的帧槽等待和 swapchain acquire 没有删除；本次只解除相机更新对这些阻塞点的顺序依赖。

## 自动验证

2026-08-15：

- `cmake --build --preset debug`：成功，MSVC `/W4 /WX`。
- `ctest --preset debug --output-on-failure`：1/1 通过。
- `SHADERLAB_SMOKE_TEST=1`：Debug 应用隐藏窗口连续呈现 4 帧，退出码 0；没有 error-level validation 消息。
- `git diff --check`：通过，仅有仓库既有的 LF/CRLF 转换提示。

## 人工验收建议

1. 启动 Debug 应用，按住左键连续慢速拖动模型轮廓，观察微小移动是否仍有明显阶梯或停顿。
2. 快速横向、纵向拖动，确认旋转不会丢失明显位移，松开后没有可感知的长时间漂移。
3. 拖动过程中改变窗口尺寸后继续操作，确认光标捕获、释放与相机控制正常。
4. 按下、松开左键后确认系统光标恢复到正常模式。

如果手感仍偏“黏”，优先提高 `smoothingRate`；如果仍偏“颗粒”，优先降低该值。不要先改动 Vulkan 同步正确性。
