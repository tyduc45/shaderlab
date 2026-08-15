# 官方 ImGui Vulkan backend 集成记录

日期：2026-08-15

## 结论

ShaderLab 保留 Volk 处理自身 Vulkan 函数加载，但 ImGui 使用 vcpkg 已提供的官方 `imgui_impl_vulkan`，独立通过 `Vulkan::Vulkan` 调用系统 loader。项目不再维护自制 ImGui Vulkan renderer。

长期原则：

> 依赖库存在官方 Vulkan backend 时，默认使用官方 backend。若加载方式冲突，优先调整编译、命名空间和链接边界；除非官方 backend 明确缺少必要能力，否则不自行重写渲染后端。

## 根因留痕

原配置同时存在：

- vcpkg `imgui[vulkan-binding]`：`imgui_impl_vulkan.cpp` 以直接 Vulkan prototypes 编译，并链接 `Vulkan::Vulkan`。
- ShaderLab：`VK_NO_PROTOTYPES` + 全局 namespace 的 `volk::volk` 静态库。

二进制检查显示，ImGui backend 对 Vulkan 入口生成直接函数调用，例如 `call vkCreateSampler`；全局 Volk 则定义同名的函数指针数据符号 `PFN_vkCreateSampler vkCreateSampler`。Windows x64 COFF 不在 C 外部符号名中区分函数与数据，链接时可能把直接函数调用绑定到 Volk 数据地址，导致初始化访问冲突。

当前 vcpkg backend 未以 `IMGUI_IMPL_VULKAN_NO_PROTOTYPES` 编译，因此对它调用 `ImGui_ImplVulkan_LoadFunctions()` 只会走编译期空分支，不能补救已经生成的直接调用。

此前用项目内 renderer 绕过了冲突，但重复实现了官方已有的字体上传、descriptor、pipeline、draw buffer 和 draw command，且会增加 M3 texture thumbnail 等后续功能的维护成本。

## 当前边界

```text
ShaderLab Vulkan 代码
    → namespaced Volk
    → LoadLibrary(vulkan-1.dll) / Vulkan loader

ImGui core + GLFW backend + 官方 Vulkan backend
    → Vulkan::Vulkan
    → vulkan-1.dll / Vulkan loader
```

两条调用路径共享应用创建的 `VkInstance`、`VkPhysicalDevice`、`VkDevice`、`VkQueue` 与 command buffer，但不共享同名 Vulkan 外部符号。

ShaderLab 仍负责主交换链 image barrier、`vkCmdBeginRendering`/`vkCmdEndRendering` 和 present transition；官方 backend 负责 ImGui pipeline、font texture、descriptor、上传 buffer、scissor 和 indexed draw。

## 实现变更

- `CMakeLists.txt`
  - 使用 `volk::volk_headers` 替代全局符号静态库 `volk::volk`。
  - 为 ShaderLab target 定义 `VOLK_NAMESPACE`。
  - 新增 `src/rhi/Volk.cpp`，以 C++ header implementation 生成 namespaced Volk 符号。
  - 保留 vcpkg `imgui[vulkan-binding]` 与其传递依赖 `Vulkan::Vulkan`。
  - 不再构建项目自制 ImGui GLSL shader。
- `editor::EditorUi`
  - 使用 `ImGui_ImplVulkan_Init/NewFrame/RenderDrawData/Shutdown`。
  - 以 Vulkan 1.4 dynamic rendering 初始化官方 backend。
  - 删除自制 font atlas、descriptor、pipeline、VMA vertex/index buffer 和 draw loop。
- `Renderer`
  - 保留 ForwardPass → UI LOAD pass → present barrier 顺序。
  - swapchain resize 时继续在 device idle 后重建 EditorUi/backend。

## 验证

- MSVC Debug `/W4 /WX`：通过。
- MSVC Release 构建与基础 smoke：通过，退出码 0。
- CTest：1/1 通过。
- `SHADERLAB_SMOKE_TEST=1`：退出码 0。
- `SHADERLAB_RELOAD_SMOKE_TEST=1`：退出码 0；最后 generation 生效，失败 shader 不替换 live。
- `SHADERLAB_RELOAD_STRESS_TEST=1`：100/100 generation，退出码 0，DeletionQueue 归零。
- 两次 Win32 `MoveWindow` 触发 swapchain/backend 重建：日志出现三次官方 backend 初始化（首次 + 两次重建），最终退出码 0，无 error-level validation。
- PE imports 包含 `vulkan-1.dll` 及官方 backend 使用的 `vkGetDeviceProcAddr`/`vkCreateSampler`。
- `Volk.obj` 中对应符号为 `volk::vkGetDeviceProcAddr`/`volk::vkCreateSampler`，确认不再与 ImGui 的全局函数符号重名。
- Vulkan screenshot layer 第 2 帧目视通过：Shader/Console 面板、字体、透明混合和场景合成正常。
- 本地截图：`build/ui-screenshots-official/2.png`（Git 忽略）。
- PNG SHA-256：`37C7681027F9A9897FCDEBA09F80F711B2043717FEFD26813E67749264598AD6`。

本机 loader 仍报告 Epic Online Services 残留 JSON、OBS layer 重复和旧 API 警告；这些是既有系统 implicit layer 消息，不是 ShaderLab validation 错误。
