# M2.3b 双缓冲 GPU 状态验收记录

## 状态

- 实现：完成
- 自动验证：完成
- 提交/推送：纳入本功能点收口提交

## 实现范围

### 候选状态构建

- 新增 `material::GpuState`，当前 M2 阶段原子持有一组兼容的 `VkPipelineLayout + VkPipeline + generation`。
- CPU shaderc 编译仍由 `ShaderReloadController` 调度；只有当前 generation 的成功 SPIR-V 会进入 GPU 构建。
- GPU pipeline layout、shader modules 和 graphics pipeline 在独立单 worker 上创建。任何 Vulkan 创建失败都会回滚候选对象并返回错误，不修改 `live`。
- set 0 保留为固定全局布局，glTF baseColor 材质迁移到规格约定的 set 1；默认用户 fragment shader 位于 `assets/shaders/user/default.frag`。

### 帧边界切换

- `ForwardPass` 始终持有可用的 `liveGpuState_`，成功候选先进入 `pendingGpuState_`。
- 主线程在 command recording 和 frame-slot wait 之前执行一次 `std::swap(liveGpuState_, pendingGpuState_)`；交换之间没有 Vulkan 调用或其他可失败操作。
- 旧 live 的销毁回调在交换前成功进入 `DeletionQueue`，交换后旧对象立即 disarm，避免 RAII 析构重复销毁。
- 旧 pipeline/pipeline layout 延迟 3 帧释放，覆盖 `FRAMES_IN_FLIGHT + 1`。

### Generation 与失败保持

- F5 手动触发 `assets/shaders/user/default.frag` 的异步重编译。
- CPU 编译结果和 GPU 构建结果都在主线程消费时重新检查当前 generation。
- 过期 GPU 候选从未提交到 command buffer，可立即由 RAII 销毁。
- shaderc 语法错误和 pipeline 创建错误进入 `core::Log`；失败不设置 pending，也不会替换 live。

## 自动验证

2026-08-15：

- `cmake --build --preset debug`：成功，MSVC `/W4 /WX`。
- `ctest --preset debug --output-on-failure`：1/1 通过。
- 基础 Vulkan smoke：4 帧退出码 0，M1 固定启动状态和 set 1 材质绑定无回归。
- `SHADERLAB_RELOAD_SMOKE_TEST=1`：
  - 连续快速请求 generation 1–10；只创建/应用最终 generation 10。
  - 再提交 generation 11 语法错误；日志定位 `reload_failure.frag:2`。
  - 语法错误后 `lastAppliedShaderGeneration` 仍为 10，继续渲染 4 帧。
  - 退出码 0，没有非预期 error-level validation 消息。

本机 Vulkan loader 仍会输出 EOS overlay JSON 缺失和 OBS layer API 1.2 警告；这些是外部隐式 layer 提示，不是 validation 错误，也不影响测试退出码。

## 下一步

M2.4 接入 ImGui 编辑器外壳：提供可见 Compile 按钮、编译 spinner/状态和 Console 日志面板。完成 UI 后再执行 M2 的 100 次重载与人工画面验收。
