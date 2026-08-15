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
| 2026-08-15 | M2.1 | DOING | DeletionQueue 与 JobSystem | 正在实现 |
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
