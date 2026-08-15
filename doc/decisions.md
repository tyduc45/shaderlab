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

