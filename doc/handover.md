# ShaderLab 交接记录

## 当前任务

M1.4：实现 VMA Buffer/Image、固定 shader 与 ForwardPass。M1.1-M1.3 已完成。

## 不可破坏的不变式

1. `MaterialAsset` 是参数与贴图的唯一真相；GPU 状态只可从真相层重新投影。
2. `MaterialInstance::live` 必须始终可用；任何编译或资源创建失败都不能破坏 live 状态。
3. 热重载时 PSO、pipeline layout 与 descriptor 必须作为兼容的一组原子切换。
4. 所有运行期 Vulkan 销毁必须进入 DeletionQueue（Device 最终清理除外）。
5. 编译失败不得覆盖上一次成功的 include 依赖图。
6. 不实现 deferred、clustered/tiled lights、render graph、自定义 VS、face-level 材质、跨平台抽象、shader graph、OIT 或骨骼动画。

## 恢复工作步骤

1. 阅读 `/doc/implementation_plan.md`、`/doc/progress.md`、本文件。
2. 检查 `git status --short --branch` 和最近提交，绝不覆盖未知的用户改动。
3. 从 `progress.md` 第一条非 DONE 项继续。
4. 完成功能点后运行对应验证，更新 `/doc`，提交并立即推送当前分支。

## 当前环境

- 工作目录：`E:/cpp_review/shaderlab`
- 远端：`https://github.com/tyduc45/shaderlab.git`
- 目标：Windows 10/11 x64、MSVC 2022、CMake 3.25+、Vulkan 1.4。
- 本机 vcpkg：`E:/cpp_review/vcpkg`（仓库外，仅用于构建验证）。

## 最近验证

- `cmake --preset windows-msvc`：成功。
- `cmake --build --preset debug`：成功，`/W4 /WX`。
- `ctest --preset debug`：1/1 通过。
- `SHADERLAB_SMOKE_TEST=1` 运行 Debug 应用：退出码 0，已验证本机 Vulkan 1.4 device、surface、timeline semaphore 与 VMA 初始化/清理。
- 当前 smoke test 会隐藏窗口连续提交/呈现 4 帧；使用 dynamic rendering 与 synchronization2，任何 error-level validation 回调都会使进程失败。

## 已完成的 Vulkan 约束

- instance API 固定为 Vulkan 1.4，不满足即给出明确错误。
- 选卡时检查 swapchain、present queue 和 Vulkan 1.2/1.3/1.4 所需特性。
- Debug validation 消息进入 `core::Log`，可在后续 ConsolePanel 直接接收。
- VMA 使用动态 Vulkan 函数表；allocator 生命周期受 `rhi::Device` 管理。

## M1.3 同步模型

- 每个 in-flight frame 独占 command pool、command buffer、image-available 与 render-finished binary semaphore。
- `Device::frameTimeline()` 是跨帧 timeline；重用 frame slot 前等待该 slot 最近一次 signal value。
- acquire/present 仍使用 binary semaphore，这是 WSI 契约；GPU 帧节流使用 timeline semaphore。
- swapchain image 通过 synchronization2 在 `UNDEFINED → COLOR_ATTACHMENT_OPTIMAL → PRESENT_SRC_KHR` 间转换。
- resize/out-of-date 当前使用 `device.waitIdle()` 后整体重建；这是 M1 最简单且正确的编辑器路径，后续无需改动渲染 ABI。
