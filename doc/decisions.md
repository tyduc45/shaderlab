# ShaderLab 实施决策记录

## 2026-08-15 — Vulkan loader 只在运行时由 volk 加载

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

## 2026-08-15 — ImGui Vulkan 绘制使用项目内 volk-compatible backend

### 背景

vcpkg 的 ImGui static library 将官方 Vulkan backend 以直接 Vulkan prototype 编译；ShaderLab 则定义 `VK_NO_PROTOTYPES` 并由 volk 暴露同名函数指针。实际链接后官方 backend 在 `ImGui_ImplVulkan_Init` 发生访问冲突，显式调用其 load-functions API 也不能改变库本身已编译的调用 ABI。

### 决策

- 不改变 ShaderLab 的 volk-only loader 约束，也不引入本地 `vulkan-1.dll`。
- 继续使用 vcpkg ImGui core/docking 与 GLFW backend。
- 项目内实现只满足当前编辑器需求的 Vulkan renderer：font atlas、每帧 VMA buffer、descriptor、alpha blend pipeline、scissor 和 indexed draw。
- UI 与场景一样只使用 synchronization2 和 dynamic rendering；不引入 render pass/framebuffer。
- UI 纹理 registry 留到 M3 Inspector 需要缩略图时扩展，M2 不提前实现泛化层。

### 验证

基础 UI smoke 和 reload+UI smoke 均退出码 0；DamagedHelmet 截图确认两个面板、字体、混合和场景合成正确。

## 2026-08-15 — shaderc 一次性初始化在持久 worker 上预热

### 背景

未预热时第一次 fragment compile 端到端为 739.2ms；之后同一线程上的编译稳定在约 8–15ms。根因是每个任务重新构造 `shaderc::Compiler`，并把 glslang 的一次性初始化成本暴露给第一次用户 Compile。

### 决策

- `ShaderCompiler::compileFragment()` 使用 worker thread-local `shaderc::Compiler`。
- `ShaderReloadController` 构造时向每个持久 worker 提交一次不产生 generation/result 的最小 shader 预热，并等待完成。
- 预热只移动一次性成本到编辑器启动，不自动生成或应用用户 shader，仍保持“手动 Compile 为默认”的产品语义。

### 验证

预热后第一次用户 generation 为 9.3ms；100 次顺序 reload 的 min/avg/max 为 7.9/9.8/14.9ms；10 次快速触发最终代为 43.0ms，均低于 200ms。
