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
