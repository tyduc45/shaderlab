# ShaderLab 交接记录

## 当前任务

初始化项目，并开始 M1（能看到东西）。

## 不可破坏的不变式

1. `MaterialAsset` 是参数与贴图的唯一真相；GPU 状态只可从真相层重新投影。
2. `MaterialInstance::live` 必须始终可用；任何编译或资源创建失败都不能破坏 live 状态。
3. 热重载时 PSO、pipeline layout 与 descriptor 必须作为兼容的一组原子切换。
4. 所有运行期 Vulkan 销毁必须进入 DeletionQueue（Device 最终清理除外）。
5. 编译失败不得覆盖上一次成功的 include 依赖图。
6. 不实现 deferred、clustered/tiled lights、render graph、自定义 VS、face-level 材质、跨平台抽象、shader graph、OIT 或骨骼动画。

## 恢复工作步骤

1. 阅读 `/doc/implementation_plan.md`、`/doc/progress.md`、本文件。
2. 检查 `git status --short --branch` 和最近提交，绝不覆盖未知的用户改动。
3. 从 `progress.md` 第一条非 DONE 项继续。
4. 完成功能点后运行对应验证，更新 `/doc`，提交并立即推送当前分支。

## 当前环境

- 工作目录：`E:/cpp_review/shaderlab`
- 远端：`https://github.com/tyduc45/shaderlab.git`
- 目标：Windows 10/11 x64、MSVC 2022、CMake 3.25+、Vulkan 1.4。

