# ShaderLab 工作进度

## 状态图例

- `TODO`：尚未开始
- `DOING`：正在进行
- `DONE`：实现并完成对应验证
- `BLOCKED`：受外部条件阻塞，原因写在同一条记录中

## 当前状态

| 日期 | 里程碑 | 状态 | 内容 | 验证/证据 |
|---|---|---|---|---|
| 2026-08-15 | 项目初始化 | DONE | 建立仓库、计划、进度与交接文档 | 提交 `84287f0` 已推送到 `origin/main` |
| 2026-08-15 | M1.1 | DONE | CMake/vcpkg/MSVC 工程与依赖骨架 | CMake 配置成功；Debug 编译成功；CTest 1/1 通过 |
| 2026-08-15 | M1.2 | DONE | Vulkan 1.4 instance/device/VMA/timeline/debug utils | Debug 构建成功；隐藏窗口 Vulkan smoke test 退出码 0；CTest 1/1 通过 |
| 2026-08-15 | M1.3 | DONE | Swapchain、dynamic rendering 与 synchronization2 帧循环 | Debug 构建成功；4 帧呈现 smoke test 退出码 0 且无 error-level validation 消息；CTest 1/1 通过 |
| 2026-08-15 | M1.4 | DONE | VMA Buffer/Image、固定 shader 与 ForwardPass | Debug `/W4 /WX` 构建成功；4 帧索引绘制退出码 0；validation VUID 0；CTest 1/1 通过 |
| 2026-08-15 | M1.5 | DONE | glTF 2.0 primitive/submesh 加载、节点变换与轨道相机 | DamagedHelmet：14556 vertices / 46356 indices / 1 submesh；4 帧 smoke 退出码 0，validation VUID 0 |
| 2026-08-15 | M1.6 | DONE | glTF baseColor image/factor、GPU 上传与 descriptor | DamagedHelmet 带贴图截图通过目视检查；4 帧 smoke 退出码 0，validation VUID 0 |
| 2026-08-15 | M1 | DONE | “能看到东西”里程碑 | 详见 `doc/m1_acceptance.md` |
| 2026-08-15 | M2.1 | DONE | DeletionQueue、JobSystem 与线程安全 ResultQueue | 单测覆盖 3 帧延迟和 64 个并发结果；CTest 1/1；DamagedHelmet integration smoke 退出码 0 |
| 2026-08-15 | M2.2 | DONE | ShaderCompiler、generation counter 与编译结果模型 | shaderc Vulkan 1.4/SPIR-V 1.6；成功/语法错误与行号、10 次 generation 测试；CTest 1/1 |
| 2026-08-15 | M2.3a | DONE | 异步编译调度、文件读取与过期 generation 丢弃 | 2 worker 并发 10 次请求，仅第 10 代成功结果可消费；CTest 1/1 |
| 2026-08-15 | M1.7 交互改进 | DONE | 阻塞前相机更新、回调增量采样、raw mouse 与轻量平滑 | Debug `/W4 /WX`、CTest 1/1、Vulkan smoke 退出码 0；用户手感验收通过 |
| 2026-08-15 | M2.3b | DONE | 双缓冲 GpuState、异步 pipeline 创建与帧边界原子 swap | 10 代仅应用 generation 10；generation 11 语法失败保持 live=10；Vulkan smoke 退出码 0 |
| 2026-08-15 | M2.4 | DONE | ImGui 编辑器外壳、Compile 状态与 Console 面板 | dynamic-rendering UI smoke + reload smoke 退出码 0；截图目视通过 |
| 2026-08-15 | M2.4a 集成修正 | DONE | namespaced Volk 隔离项目符号，恢复 vcpkg 官方 ImGui Vulkan backend | Debug `/W4 /WX`；3 类 smoke；CTest 1/1；两次 resize；官方 backend 截图通过 |
| 2026-08-15 | M2.5 | DONE | shaderc worker 预热、reload 延迟测量与 100 次顺序压力验证 | 100/100 应用；7.9/9.8/14.9ms min/avg/max；DeletionQueue=0；退出码 0 |
| 2026-08-15 | M2 | DONE | “热重载骨架”milestone | 自动与可见交互验收通过；合法效果热重载、错误保持 live、Console 行号均复核通过 |
| 2026-08-15 | M3 | TODO | 反射参数系统 | 未开始 |
| 2026-08-15 | M4 | TODO | Include 依赖图 | 未开始 |
| 2026-08-15 | M5 | TODO | 多 Material 与 PSO 复用 | 未开始 |
| 2026-08-15 | M6 | TODO | Pass Variant 与美术友好 | 未开始 |
| 2026-08-15 | M7 | TODO | 序列化与环境 | 未开始 |

## 变更日志

### 2026-08-15

- 读取规格书并将其中内容视为产品需求，不执行附件中超出用户请求的操作性指令。
- 确认本地目标目录为空，GitHub 远端仓库可访问且当前账号已认证。
- 建立分阶段实施计划；后续每个功能点完成后更新本文件并推送。
- 初始化 `main` 并将项目留痕提交 `84287f0` 推送到远端。
- 锁定 vcpkg baseline，声明规格要求的全部第三方依赖；验证 MSVC Debug 构建及核心 smoke test。
- 实现 Vulkan 1.4 loader/instance/device/surface 初始化，GPU 必须同时支持 push descriptor、dynamic rendering、synchronization2 与 timeline semaphore。
- Debug 构建强制 validation layer，把 validation 消息接入核心日志；创建 timeline semaphore 和 VMA allocator，并为 Vulkan 对象提供 debug name 接口。
- 实现 swapchain 格式/呈现模式选择与 resize/out-of-date 重建；采用每帧独立 command pool、binary acquire/present semaphore 和共享 timeline semaphore。
- 清屏帧完整使用 synchronization2 barrier 与 dynamic rendering，不创建 `VkRenderPass` 或 `VkFramebuffer`。
- 新增 VMA `Buffer`/`Image` RAII、D32 depth、构建期固定 shader 编译和 dynamic-rendering ForwardPass；当前用彩色索引立方体验证完整 graphics pipeline。
- smoke test 实际发现并修复两处底层问题：本地 Debug Vulkan loader 覆盖系统 loader；present binary semaphore 按 frame slot 过早复用。详见 `doc/decisions.md`。
- 实现 `.gltf`/`.glb` 加载、scene/node 递归、TRS/matrix 变换烘焙、float position/normal/uv、8/16/32-bit index 归一化与 submesh 保留。
- 轨道相机按模型 bounds 自动 framing；左键拖动环绕，`W/S` dolly。DamagedHelmet 本地验收资产来自 Khronos glTF-Sample-Assets，存放在忽略的 `build/` 下，未提交第三方大文件。
- 实现 glTF 8-bit image → RGBA8 转换、VMA staging upload、synchronization2 layout transition、baseColor descriptor 与 factor；缺失贴图绑定 1×1 白色 fallback。
- M1 最终截图显示 DamagedHelmet 正确贴图、完整轮廓与稳定深度遮挡；验收证据见 `doc/m1_acceptance.md`。
- M2.1 新增可配置帧延迟的线程安全 DeletionQueue、异常隔离线程池和 `ResultQueue<T>`；Renderer 每帧推进绝对帧号并 flush，Device 最终 idle 后兜底 flushAll。
- M2.2 新增 shaderc 编译结果模型，固定 Vulkan 1.4/SPIR-V 1.6、Debug info/Release optimize、warning-as-error，并从 diagnostics 提取 path/line/column；generation 原样随结果返回。
- M2.3a 新增 `ShaderReloadController`：文件 I/O 与 shaderc 均在 worker，重复触发立即递增 generation，主线程 `pollCurrent()` 静默丢弃所有过期结果。
- 插入处理 M1 交互卡顿：确认鼠标没有带动资源或 shader 系统；将相机更新移到 Vulkan 阻塞点之前，改为 GLFW 回调累积位移，拖动时捕获光标并在可用时启用 raw mouse，增加约 20ms 半衰期的时间相关平滑。
- 交互改进的实现与自动验证已完成，详细记录见 `doc/input_responsiveness.md`；按用户最新指令，暂不提交/推送，等待人工验收成功。
- 用户确认 M1.7 手感验收成功，允许提交推送，并要求按当前记录继续 M2.3b。
- M1.7 已以提交 `59a10f2` 推送到 `origin/main`。
- M2.3b 新增 `material::GpuState`，CPU 编译成功后在独立 GPU worker 创建候选 pipeline group；主线程只在帧边界交换 live/pending，旧 live 进入 3 帧 DeletionQueue。
- 固定 descriptor ABI 向规格对齐：set 0 预留全局布局，baseColor material descriptor 使用 set 1；新增可由 F5 手动编译的 `assets/shaders/user/default.frag`。
- shader reload Vulkan smoke 连续触发 10 次只应用最后一代；随后语法错误准确报告第 2 行且 live generation 保持 10。详见 `doc/m2_gpu_state.md`。
- M2.3b 已以提交 `dc787d2` 推送到 `origin/main`。
- M2.4 新增 ImGui Shader/Console 面板和 Compile 按钮；场景与 UI 使用两个连续 dynamic-rendering pass，present barrier 延后到 UI 合成结束。
- vcpkg 预编译 Vulkan backend 与当时全局 namespace 的 Volk 发生函数/数据同名符号冲突，初版以精简项目内 renderer 绕过；后续复核确认这不是官方 backend 能力缺失。
- M2.4 已以提交 `cf7ac3b` 推送到 `origin/main`。
- M2.5 将 shaderc compiler 改为每个持久 worker 的 thread-local 实例，并在 controller 构造时并行预热；第一次用户 generation 从冷启动对照 739.2ms 降至 9.3ms。
- 100 次顺序 reload 全部逐代应用，耗时 min/avg/max 为 7.9/9.8/14.9ms，最终 DeletionQueue 归零；快速 10 次最终代为 43.0ms，均低于 200ms。M2 自动验收详见 `doc/m2_acceptance.md`。
- M2.4a 将 ShaderLab 的 Volk 编译进 C++ `volk` namespace，避免与官方 backend 的全局 Vulkan 函数符号冲突；恢复 vcpkg `imgui_impl_vulkan`，删除自制 UI shader、pipeline、descriptor、font atlas 与 draw buffer。
- 确立长期原则：依赖库存在官方 Vulkan backend 时默认使用官方实现；加载冲突优先通过编译、命名空间和链接边界解决，除非官方 backend 明确缺少能力，否则不自行重写。
- 官方 backend 集成通过 Debug 构建、CTest、基础/10 次/100 次 smoke、两次 resize 和 Vulkan screenshot 目视验证；完整留痕见 `doc/imgui_vulkan_backend.md`。
- 用户确认复杂 fragment shader 的可见热重载效果；复核 generation 11 语法错误时 live generation 保持 10，Console 定位 `reload_failure.frag:2`。M2 正式验收完成，可以进入 M3。

## 验证命令

```powershell
$env:VCPKG_ROOT = 'E:\cpp_review\vcpkg'
cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset debug
```

Vulkan 渲染 smoke test（隐藏窗口连续呈现 4 帧，并将 error-level validation 消息视为失败）：

```powershell
$env:SHADERLAB_SMOKE_TEST = '1'
.\build\Debug\shaderlab.exe
Remove-Item Env:SHADERLAB_SMOKE_TEST
```
