# ShaderLab 实施计划

## 目标与约束

- 需求来源：`C:/Users/Binar/Downloads/shaderlab_spec.md`。
- 用户指令优先：所有工作留痕统一放在 `/doc`；每完成一个规划功能点或 milestone，提交并推送到 `https://github.com/tyduc45/shaderlab.git`。
- 实现顺序严格遵循 M1 → M7；上一里程碑未验收前，不进入下一里程碑。
- 技术优先级：热重载正确性 > 参数系统健壮性 > 渲染效果丰富度。
- 明确不实现规格书“非目标”中的功能。

## 里程碑

### M1 — 能看到东西

1. 工程骨架：CMake 3.25+、C++20、vcpkg manifest、Windows/MSVC/Vulkan 1.4 约束。
2. Vulkan 基础：instance、debug utils、physical/logical device、volk、VMA、timeline semaphore。
3. 窗口与呈现：GLFW、swapchain、dynamic rendering、synchronization2。
4. 资源与场景：VMA buffer/image、glTF 2.0 模型加载、固定 shader、ForwardPass。
5. 交互：轨道相机；DamagedHelmet 验收路径与运行说明。

### M2 — 热重载骨架

JobSystem、ShaderCompiler、generation counter、手动编译、双缓冲 GpuState、DeletionQueue、失败保持 live 状态、Console 错误。

### M3 — 反射参数系统

SPIRV-Reflect、UniformLayout、真相/派生分层、`project()`、descriptor 重建、快速路径、Inspector。

### M4 — Include 依赖图

TrackingIncluder、IncludeGraph、FileWatcher、50ms 路径 debounce、Dirty 与批量编译。

### M5 — 多 Material 与 PSO 复用

PipelineCache、磁盘持久化、submesh 拾取、多 material 管理。

### M6 — Pass Variant 与美术友好

Shadow/Outline variants、`@param`、分组 UI、贴图槽。

### M7 — 序列化与环境

MaterialAsset/SceneAsset JSON、项目保存加载、IBL 环境。

## 提交策略

- 每个独立且已验证的功能点使用一次小提交并立即推送。
- 每个 milestone 完成后追加验收记录，再做 milestone 收口提交并推送。
- 不把构建产物、vcpkg 下载缓存、IDE 用户状态或外部资产提交到仓库。

