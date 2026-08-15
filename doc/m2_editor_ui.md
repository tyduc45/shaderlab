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
- UI 的 font atlas、descriptor、pipeline、上传 buffer 和 draw command 由 vcpkg 官方 `imgui_impl_vulkan` 管理。
- swapchain 重建时在 device idle 后同步重建官方 Vulkan backend；项目只保留主交换链同步和 dynamic-rendering scope。

## Vulkan backend 决策

最初因 vcpkg backend 的全局 Vulkan 函数符号与全局 Volk 函数指针数据符号重名，官方 backend 初始化发生访问冲突。项目一度以精简自制 renderer 绕过问题。

复核源码和二进制后确认，应当隔离加载符号而不是重写官方 renderer。当前方案为：

- ShaderLab 自身使用 `VOLK_NAMESPACE` 下的 Volk。
- ImGui 使用 vcpkg 官方 `imgui_impl_vulkan` 和 `Vulkan::Vulkan`，Volk 不参与 ImGui 函数加载。
- UI pipeline 继续使用 dynamic rendering，不创建 render pass/framebuffer。
- M3 Inspector 可直接使用官方 backend 的 texture registration API。

根因、符号证据和长期原则见 `doc/imgui_vulkan_backend.md`。

## 自动验证

2026-08-15：

- Debug `/W4 /WX` 构建成功。
- CTest 1/1 通过。
- 基础 UI Vulkan smoke：4 帧退出码 0。
- reload+UI smoke：generation 1–10 只应用 10；generation 11 语法失败保持 live=10；退出码 0。
- 两条 smoke 均无非预期 error-level validation 消息。
- 后续官方 backend 迁移再次通过基础 smoke、10 次 reload、100 次 stress、CTest 1/1 和连续两次 resize 重建。

## 视觉验证

使用 `VK_LAYER_LUNARG_screenshot` 捕获 DamagedHelmet 第 2 帧：

- `Shader` 面板位于左上，可见 Compile 按钮和 generation 状态。
- `Console` 位于底部，可见启动、GPU、模型与 UI 日志。
- 字体清晰，透明混合正确，场景仍完整可见。
- 本地 PNG：`build/ui-screenshots-layout/2.png`（Git 忽略）。
- SHA-256：`21336F110F3A57E6D218974994B33ABC47583CAE6678A83B6AFE22C485CAA8A4`。

迁移到官方 backend 后重新捕获第 2 帧：

- 本地 PNG：`build/ui-screenshots-official/2.png`（Git 忽略）。
- 字体、透明混合、场景合成和 Console 目视通过。
- SHA-256：`37C7681027F9A9897FCDEBA09F80F711B2043717FEFD26813E67749264598AD6`。

## 下一步

M2.5 执行 100 次重载压力验证、可见 Compile/错误 Console 人工验收，并形成 M2 milestone 收口记录。
