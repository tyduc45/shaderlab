# ShaderLab 实施决策记录

## 2026-08-15 — Vulkan loader 只在运行时由 volk 加载

> 状态：已被同日“官方 ImGui Vulkan backend 与 namespaced Volk 分层”决策局部修订。ShaderLab 自身 Vulkan 调用仍由 Volk 加载；ImGui 官方 backend 独立链接 vcpkg `Vulkan::Vulkan`，因此本地 `vulkan-1.dll` 不再被视为错误。

### 背景

初始 CMake 链接了 `Vulkan::Vulkan`/VMA CMake target。vcpkg 因此把 Debug `vulkan-1.dll` 复制到可执行文件目录，Windows DLL 搜索顺序使它覆盖系统 loader，进程在 `volkInitialize()` 期间以 `0xC0000005` 退出。

### 决策

- 链接 `Vulkan::Headers` 和 `volk::volk`，不链接 Vulkan loader import library。
- VMA 作为 header implementation 使用，include path 由 manifest 安装树解析；运行时函数表由 volk 提供。
- 干净构建后，可执行文件目录不得出现 `vulkan-1.dll`。

### 验证

MSVC AddressSanitizer 用于隔离 loader 阶段；移除本地 loader 后 Vulkan 初始化正常。正常 Debug 干净构建和 4 帧 smoke test 均通过。

## 2026-08-15 — Present semaphore 按 swapchain image 分配

### 背景

最初 `renderFinished` binary semaphore 按 frame-in-flight slot 分配。timeline 等待只证明 graphics submit 完成，不能证明 presentation engine 已停止使用该 binary semaphore。Validation 在第 3/4 帧报告 `VUID-vkQueueSubmit2-semaphore-03868`。

### 决策

- `imageAvailable` 仍按 frame slot 分配。
- command pool/buffer 和 timeline value 仍按 frame slot 管理。
- `presentReady` 按 swapchain image 分配，并用 acquire 返回的 image index 选择。
- swapchain 重建后同步重建这组 semaphore。

### 验证

隐藏窗口连续 acquire/submit/present 4 帧，进程退出码 0，validation VUID 为 0。

## 2026-08-15 — M1 材质只投影 baseColor

### 背景

M1 的验收目标是“加载 DamagedHelmet，能看到带贴图的模型”；完整 PBR 光照库属于后续 shader 作者接口与环境系统。规格明确要求引擎不能假设用户采用 PBR。

### 决策

M1 固定 shader 只读取 glTF `baseColorTexture × baseColorFactor`，再施加一个简单方向光用于观察形体。Normal、metallic-roughness、AO、emissive 图片可由 tinygltf 解码，但不绑定到固定 shader。M2 以后该固定 pipeline 会被自由的用户 fragment shader 取代；PBR 仅作为 GLSL library 提供。

### 验证

DamagedHelmet baseColor 纹理与 UV 在 1280×720 screenshot layer 输出中目视正确，validation VUID 为 0。

## 2026-08-15 — 输入采样与 Vulkan 阻塞解耦

### 背景

轨道相机原先在 `recordFrame()` 中轮询鼠标并立即应用整数坐标差。该位置晚于 frame timeline wait 和无限超时的 `vkAcquireNextImageKHR`，因此鼠标响应延迟会继承 GPU/WSI 帧节奏；每帧单次位置采样也没有显式保留事件轮询期间的全部中间增量。

### 决策

- 保留 Vulkan 正确性所需的 frame-slot timeline wait 和 swapchain acquire。
- 每帧在任何潜在阻塞点之前消费相机输入。
- GLFW cursor callback 只负责累积位移；渲染帧统一消费，避免回调直接修改渲染状态。
- 左键拖动期间捕获光标，并在平台支持时启用 raw mouse motion。
- 使用按 delta time 计算的轻量指数积压滤波；积压逐步归零，以免为平滑永久丢弃输入位移。

### 验证

Debug `/W4 /WX` 构建、CTest 1/1 和 4 帧 Vulkan smoke test 均通过；用户随后确认鼠标手感验收成功并授权推送。

## 2026-08-15 — GPU 候选在 worker 构建，帧边界只交换所有权

### 背景

只有 CPU SPIR-V 异步编译不足以保证热重载流畅和失败安全；`vkCreatePipelineLayout`、`vkCreateGraphicsPipelines` 也可能耗时或被驱动拒绝。与此同时，旧 pipeline 可能仍被 in-flight command buffer 使用，不能在 swap 后立即销毁。

### 决策

- `ShaderReloadController` 继续负责文件 I/O、shaderc 和第一层 generation 过滤。
- 当前 generation 的 SPIR-V 交给独立单 worker 创建完整 `GpuState` 候选；GPU 结果回主线程时再次检查 generation。
- `liveGpuState_` 永远有效，成功候选才进入 `pendingGpuState_`；主线程帧边界执行一次不抛异常的 `unique_ptr` swap。
- 交换前先把旧 live 的销毁闭包成功写入 `DeletionQueue`，交换后 disarm 旧 RAII 对象；这样队列分配失败也发生在 live 状态改变之前。
- M2 descriptor ABI 固定为预留 set 0 和材质 set 1。M3 反射变化时，descriptor layout/pool/set 将扩展进同一 `GpuState` 原子组。

### 验证

实际 Vulkan smoke 连续触发 10 代只应用第 10 代；语法错误的第 11 代没有创建 pending、没有替换 live。继续渲染和延迟销毁期间 validation 无非预期错误。

## 2026-08-15 — 官方 ImGui Vulkan backend 与 namespaced Volk 分层

### 背景

vcpkg 的 ImGui static library 已包含官方 `imgui_impl_vulkan.cpp`，并以直接 Vulkan prototype 编译；ShaderLab 则定义 `VK_NO_PROTOTYPES`，原先链接的全局 namespace Volk 暴露同名函数指针变量。二进制检查确认官方 backend 生成 `call vkCreateSampler`，而 Volk 提供同名数据符号 `PFN_vkCreateSampler vkCreateSampler`，两者在 Windows x64 COFF 链接时可能错误绑定，解释了此前 `ImGui_ImplVulkan_Init` 的访问冲突。

这不是官方 backend 缺少 Volk 适配，也不是必须重写 renderer。此前为绕开链接冲突实现项目内 UI renderer，扩大了维护范围；正确边界是保留官方 renderer，只隔离 ShaderLab 自身的函数加载符号。

### 决策

- vcpkg `imgui[vulkan-binding]` 继续提供未经修改的官方 Vulkan backend，并通过 `Vulkan::Vulkan` 调用 loader。
- ShaderLab 将 Volk 以 `VOLK_NAMESPACE` + header implementation 编译到 `volk` C++ namespace，不再链接全局符号版本 `volk::volk`。
- ShaderLab 自身保留 `VK_NO_PROTOTYPES` 和 Volk；ImGui 不定义 `IMGUI_IMPL_VULKAN_USE_VOLK`，也不调用 `ImGui_ImplVulkan_LoadFunctions()`。
- 删除项目内 UI font atlas、descriptor、pipeline、VMA draw buffer 和 UI shader；交还官方 backend 管理。
- ShaderLab 只保留主交换链 barrier 与 dynamic-rendering begin/end，这是官方 backend 要求调用方提供的渲染上下文，不属于自制 backend。
- vcpkg `Vulkan::Vulkan` 会把 `vulkan-1.dll` 部署到 Debug 输出目录；它只服务官方 ImGui 调用路径，不改变 ShaderLab 自身使用 Volk 的约束。

### 后续原则

依赖库存在官方 Vulkan backend 时，默认使用官方 backend。若加载方式冲突，优先调整编译、命名空间和链接边界；除非官方 backend 明确缺少必要能力，否则不自行重写渲染后端。

### 验证

Debug `/W4 /WX` 构建、CTest 1/1、基础 UI smoke、10 次 reload smoke 与 100 次 stress 均通过。二进制同时显示 namespaced Volk 数据符号和 `vulkan-1.dll` ImGui 函数导入；连续两次窗口 resize 后官方 backend 随交换链重建并正常退出。Vulkan screenshot layer 第 2 帧确认字体、透明混合、场景合成与 Console 正常，详见 `doc/imgui_vulkan_backend.md`。

## 2026-08-15 — shaderc 一次性初始化在持久 worker 上预热

### 背景

未预热时第一次 fragment compile 端到端为 739.2ms；之后同一线程上的编译稳定在约 8–15ms。根因是每个任务重新构造 `shaderc::Compiler`，并把 glslang 的一次性初始化成本暴露给第一次用户 Compile。

### 决策

- `ShaderCompiler::compileFragment()` 使用 worker thread-local `shaderc::Compiler`。
- `ShaderReloadController` 构造时向每个持久 worker 提交一次不产生 generation/result 的最小 shader 预热，并等待完成。
- 预热只移动一次性成本到编辑器启动，不自动生成或应用用户 shader，仍保持“手动 Compile 为默认”的产品语义。

### 验证

预热后第一次用户 generation 为 9.3ms；100 次顺序 reload 的 min/avg/max 为 7.9/9.8/14.9ms；10 次快速触发最终代为 43.0ms，均低于 200ms。
