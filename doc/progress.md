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
| 2026-08-15 | M1.3 | DOING | Swapchain、dynamic rendering 与 synchronization2 帧循环 | 正在实现 |
| 2026-08-15 | M2 | TODO | 热重载骨架 | 未开始 |
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

## 验证命令

```powershell
$env:VCPKG_ROOT = 'E:\cpp_review\vcpkg'
cmake --preset windows-msvc
cmake --build --preset debug
ctest --preset debug
```

Vulkan 初始化 smoke test：

```powershell
$env:SHADERLAB_SMOKE_TEST = '1'
.\build\Debug\shaderlab.exe
Remove-Item Env:SHADERLAB_SMOKE_TEST
```

