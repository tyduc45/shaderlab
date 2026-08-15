# M2 验收记录 — 热重载骨架

日期：2026-08-15

## 当前结论

M2 自动验收与可见交互验收均已通过，milestone 正式完成。用户于
2026-08-15 确认可以收口 M2 并开始 M3。

## 已完成能力

- JobSystem、线程安全 ResultQueue、generation counter 与三帧 DeletionQueue。
- shaderc Vulkan 1.4 / SPIR-V 1.6 异步编译与结构化 path/line/column 错误。
- CPU compile result 与 GPU build result 两层 generation 过滤。
- worker 上创建候选 pipeline layout/pipeline；失败不修改 live。
- `liveGpuState_` / `pendingGpuState_` 帧边界原子交换，旧 live 延迟销毁。
- F5 和可见 Compile 按钮；编译状态、live generation、最近 reload 毫秒数。
- Console 显示编译、validation 与运行日志；语法错误保留旧画面。

## 自动验收结果

### 构建与基础回归

- MSVC Debug `/W4 /WX`：通过。
- CTest：1/1 通过。
- dynamic-rendering 场景 + UI Vulkan smoke：退出码 0。
- DamagedHelmet UI 截图：目视通过，见 `doc/m2_editor_ui.md`。
- UI 已迁移到 vcpkg 官方 `imgui_impl_vulkan`；ShaderLab 使用 namespaced Volk 隔离符号，详见 `doc/imgui_vulkan_backend.md`。
- 官方 backend 迁移后重新运行基础 smoke、10 次 reload smoke、100 次 stress 和连续两次 resize，均退出码 0，无非预期 error-level validation。

### 连续快速触发 10 次

`SHADERLAB_RELOAD_SMOKE_TEST=1`：

- generation 1–10 快速排队，只应用 generation 10。
- shaderc worker 预热后，generation 10 端到端耗时 43.0ms。
- generation 11 故意制造语法错误，Console 定位 `reload_failure.frag:2`。
- 失败后 live generation 保持 10，再渲染 4 帧无异常。
- 退出码 0，无非预期 error-level validation 消息。

### 顺序重载 100 次

`SHADERLAB_RELOAD_STRESS_TEST=1` 每一代都等待 CPU compile、GPU candidate build 和帧边界 apply 后再开始下一代：

- 100/100 generations 均按序成为 live，最终 live generation 100。
- 100 个端到端样本：minimum 7.9ms，average 9.8ms，maximum 14.9ms。
- 最后额外推进 4 帧，程序断言 `DeletionQueue::pendingCount() == 0`。
- 退出码 0，无 error-level validation 消息。
- 首次未预热对照为 739.2ms；将 shaderc/glslang 一次性初始化移到两个持久 worker 的构造预热后，第一次用户 generation 降至 9.3ms。
- 首轮长时间观测中，进程工作集在中后段约 269–274MiB 波动，没有随 generation 线性增长。驱动层显存曲线仍保留为人工验收观察项。

## 可复现命令

```powershell
$env:VCPKG_ROOT = 'E:\cpp_review\vcpkg'
cmake --build --preset debug
ctest --preset debug --output-on-failure

$env:SHADERLAB_RELOAD_SMOKE_TEST = '1'
.\build\Debug\shaderlab.exe
Remove-Item Env:SHADERLAB_RELOAD_SMOKE_TEST

$env:SHADERLAB_RELOAD_STRESS_TEST = '1'
.\build\Debug\shaderlab.exe
Remove-Item Env:SHADERLAB_RELOAD_STRESS_TEST
```

## 用户可见交互验收

1. Shader 与 Console 面板可见；此前已验收的轨道相机交互保持可用。
2. 将用户 fragment shader 改为复杂虹彩蚀刻效果后触发 Compile，画面更新且面板显示 reload 时间。
3. generation 11 故意注入语法错误后，画面保持 generation 10 的成功效果，Console 正确显示 `reload_failure.frag:2`。
4. 随后重新加载合法文件，恢复成功。
5. 复核截图保存在忽略目录 `build/m2-audit-error/6.png`；错误回退 smoke 退出码 0。

GPU dedicated memory 长时间人工曲线为可选观察项；100 次压力测试已验证
DeletionQueue 归零且进程工作集不随 generation 线性增长。
