# M1 验收记录 — 能看到东西

日期：2026-08-15

## 结论

M1 通过。ShaderLab 能在 Windows x64 上初始化 Vulkan 1.4，使用 VMA、timeline semaphore、synchronization2 与 dynamic rendering 加载并绘制 Khronos DamagedHelmet glTF。截图中模型轮廓完整、baseColor 贴图与 UV 正常、深度遮挡正确；轨道相机按 bounds 自动 framing，并支持左键环绕与 `W/S` dolly。

## 验收对象

- 来源：`https://github.com/KhronosGroup/glTF-Sample-Assets.git`
- sparse-checkout：`Models/DamagedHelmet/glTF`
- 本地路径：`build/glTF-Sample-Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf`
- 加载统计：14556 vertices、46356 indices、1 submesh。
- 第三方模型与贴图未提交到 ShaderLab 仓库。

## 自动验证

```powershell
$env:VCPKG_ROOT = 'E:\cpp_review\vcpkg'
cmake --preset windows-msvc -DSHADERLAB_ASAN=OFF
cmake --build --preset debug
$env:SHADERLAB_SMOKE_TEST = '1'
.\build\Debug\shaderlab.exe '<DamagedHelmet.gltf path>'
ctest --preset debug
```

结果：

- MSVC Debug `/W4 /WX` 构建成功。
- 隐藏窗口连续 4 帧 acquire/submit/present，退出码 0。
- Khronos validation VUID：0。
- CTest：1/1 通过。

## 视觉验证

使用 Vulkan SDK `VK_LAYER_LUNARG_screenshot` 捕获第 2 帧：

- 原始：`build/screenshots/2.ppm`（1280×720）。
- 目视：头盔居中完整显示，白/灰/黄/黑表面纹理清晰，UV 无明显错位，前后遮挡稳定，背景正确清除。
- PNG 验收副本：`build/screenshots/2.png`。
- PNG SHA-256：`2457A51B87BEE752F83A2CF9F4BC3AA51F8767E6644F2D6C60111EFE6045BD66`。

截图与本地测试资产位于 Git 忽略的 `build/`，不作为产品资产提交。

## 环境消息说明

本机 Vulkan loader 会报告 Epic Online Services 残留 layer JSON 缺失、OBS layer 重复/旧 API。这些是系统 implicit layer 的 general 消息，不是 ShaderLab validation VUID。Smoke test 只把 validation 类型的 error severity 视为失败，仍完整记录其他 loader 消息。

