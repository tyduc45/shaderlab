# ShaderLab 交接记录

## 当前任务

M2.4 ImGui 编辑器外壳、Compile 状态和 Console 已实现并完成自动/截图验证。下一项是 M2.5：100 次 reload 压力测试、人工 Compile/错误 Console 验收和 M2 milestone 收口。M1、M2.1-M2.4 已完成。

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
4. M2.4 收口后从 M2.5 继续；每个已验证功能点更新 `/doc`、提交并推送。

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
- M1.4 smoke test 已实际提交固定 ForwardPass 的 indexed cube，退出码 0，validation VUID 0。
- M1.5 已用 Khronos DamagedHelmet 验证 glTF geometry：14556 vertices、46356 indices、1 submesh，4 帧退出码 0，validation VUID 0。
- M1.6 已完成 baseColor texture/factor、白色 fallback、descriptor 和 transfer upload；目视验收通过，详见 `doc/m1_acceptance.md`。
- M1.7 已将相机输入提前到 Vulkan 阻塞点之前，使用 GLFW 回调累积位移、拖动捕获/raw mouse 和约 20ms 半衰期平滑；构建、CTest、Vulkan smoke 与用户手感验收均通过，详见 `doc/input_responsiveness.md`。
- M2.3b reload smoke 连续触发 10 代只应用 generation 10；generation 11 语法错误保持 live generation 10，详见 `doc/m2_gpu_state.md`。
- M2.4 基础 UI 与 reload+UI smoke 均退出码 0；DamagedHelmet UI 截图目视通过，详见 `doc/m2_editor_ui.md`。

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

## M2 接入点

- `Device::deletionQueue()` 已由 Renderer 绝对帧号驱动，默认延迟 3 帧（`FRAMES_IN_FLIGHT + 1`）；热重载旧状态应通过 `push(frame, destroy)` 进入该队列。
- `core::JobSystem` 捕获任务异常并继续工作；`ResultQueue<T>` 用于将 shader compile 结果带回主线程，主线程不得阻塞等待编译。
- `shader::ShaderCompiler` 只产出 CPU SPIR-V 与结构化 diagnostics，不修改任何 live GPU 状态；`CompileResult::generation` 必须在主线程消费时与当前 generation 比较。
- `shader::GenerationCounter` 已单测连续 10 次触发只接受第 10 代；M2.3 将它嵌入 material/runtime compile controller。
- `ShaderReloadController::requestFile/requestSource` 是 CPU 编译结果的唯一主线程消费入口；当前成功 SPIR-V 再交给 Renderer 的单 GPU worker 构建候选。
- Controller 成员声明为 `ResultQueue` 在前、`JobSystem` 在后，C++ 逆序析构使 JobSystem 先 join 全部 worker，再销毁结果队列；如重排成员必须重新审计生命周期。
- `ForwardPass` 持有 `liveGpuState_`/`pendingGpuState_`；帧边界先安全排队旧 live 的延迟销毁，再原子 swap。不要把任何可能失败的 Vulkan 创建调用移入 swap 区间。
- `GpuState` 当前拥有 pipeline layout + pipeline；M3 必须把随反射变化的 descriptor layout/pool/set/UBO 扩入同一原子状态，不可拆开切换。
- 用户 fragment 位于 `assets/shaders/user/default.frag`，F5 触发异步编译；材质 descriptor 已从 set 0 迁到固定 set 1，set 0 当前为空布局占位。
- `editor::EditorUi` 使用 ImGui core + GLFW backend，但 Vulkan draw 由项目内 volk-compatible renderer 完成；不要重新接入 vcpkg 预编译 `ImGui_ImplVulkan_*`，其 prototype ABI 与当前 volk 配置冲突。
- UI 在 ForwardPass 后以 LOAD dynamic rendering 合成；`ForwardPass::transitionToPresent()` 必须保持在 UI record 之后。
- `core::Log` 已能把 validation 与编译错误送往未来 ConsolePanel，也能在 smoke test 中通过 stderr 留证。
- 固定 shader 位于 `assets/shaders/engine/`，只服务于 M1；M2 的用户 fragment shader 编译链不得复用构建期 glslc 状态。

## DamagedHelmet 本地验收资产

资产未提交。当前机器路径：

`E:/cpp_review/shaderlab/build/glTF-Sample-Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf`

恢复方式：clone `https://github.com/KhronosGroup/glTF-Sample-Assets.git`，仅 sparse-checkout `Models/DamagedHelmet/glTF` 到忽略的 `build/` 下。
