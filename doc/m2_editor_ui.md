# M2.4 编辑器外壳验收记录

## 状态

- 实现：完成
- 自动验证：完成
- 视觉验证：完成
- 提交/推送：纳入本功能点收口提交

## 实现范围

- 新增 ImGui docking 编辑器外壳，提供 `Shader` 与 `Console` 两个面板。
- `Shader` 面板提供 `Compile (F5)` 按钮、当前请求 generation、live generation 和编译中状态。
- `Console` 每帧读取线程安全 `core::Log` 快照，按 Info/Warning/Error/Validation 着色并在用户位于尾部时自动跟随。
- GLFW backend 链式安装回调；UI 捕获的左键按下不会再启动轨道相机，拖动中的释放事件仍始终送达相机。
- 场景 ForwardPass 结束后保持 swapchain image 为 `COLOR_ATTACHMENT_OPTIMAL`；UI 使用第二个 dynamic-rendering pass 以 `LOAD` 合成，最终再执行 present barrier。
- UI vertex/index buffer 按 frame-in-flight slot 独立持有，重用前沿用 Renderer 的 timeline wait，避免 CPU 覆盖 GPU 正在读取的数据。
- UI font atlas、descriptor、pipeline 和全部 Vulkan 对象具备 debug name；swapchain 重建时在 device idle 后同步重建 UI GPU 状态。

## volk 兼容决策

vcpkg 的预编译 ImGui Vulkan backend 使用直接 Vulkan prototype，而 ShaderLab 固定采用 `VK_NO_PROTOTYPES + volk`。两套符号在链接后导致 backend 初始化访问冲突。

最终保留 ImGui 核心与 GLFW backend，使用项目内的精简 Vulkan renderer：

- 所有 Vulkan 调用继续通过 volk。
- UI pipeline 使用 dynamic rendering，不创建 render pass/framebuffer。
- UI shader 由构建期 glslc 生成 SPIR-V。
- 当前只绑定 ImGui font atlas；M3 Inspector 如需展示纹理缩略图时再扩展 texture registry。

## 自动验证

2026-08-15：

- Debug `/W4 /WX` 构建成功。
- CTest 1/1 通过。
- 基础 UI Vulkan smoke：4 帧退出码 0。
- reload+UI smoke：generation 1–10 只应用 10；generation 11 语法失败保持 live=10；退出码 0。
- 两条 smoke 均无非预期 error-level validation 消息。

## 视觉验证

使用 `VK_LAYER_LUNARG_screenshot` 捕获 DamagedHelmet 第 2 帧：

- `Shader` 面板位于左上，可见 Compile 按钮和 generation 状态。
- `Console` 位于底部，可见启动、GPU、模型与 UI 日志。
- 字体清晰，透明混合正确，场景仍完整可见。
- 本地 PNG：`build/ui-screenshots-layout/2.png`（Git 忽略）。
- SHA-256：`21336F110F3A57E6D218974994B33ABC47583CAE6678A83B6AFE22C485CAA8A4`。

## 下一步

M2.5 执行 100 次重载压力验证、可见 Compile/错误 Console 人工验收，并形成 M2 milestone 收口记录。
