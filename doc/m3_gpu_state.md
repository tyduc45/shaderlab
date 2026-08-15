# M3.2–M3.3 Descriptor 投影与 Inspector

日期：2026-08-15

## GPU 兼容组

固定 set 0 仍由引擎提供空占位布局。用户 fragment shader 的 set 1 不再由
`ForwardPass` 写死，而是根据 `ReflectionResult` 创建：

- `VkDescriptorSetLayout`
- 每代独占的 `VkDescriptorPool` 与每 glTF material 一个 descriptor set
- 可选 `MaterialParams` UBO（binding 0）
- 任意数量的 `sampler2D`（combined image sampler）
- `VkPipelineLayout` 与 Forward pipeline

上述对象全部由一个 `GpuState` 持有。候选状态完整创建和 descriptor 写入成功后才
进入 pending；帧边界一次替换 live 指针。旧状态整体捕获进 `DeletionQueue`，不会出现
新 pipeline 搭配旧 descriptor set 的中间态。

## Material Inspector

- SPIR-V 反射决定控件顺序和真实类型。
- `@param` 补充显示名、color、range、默认值、group 与 tooltip。
- UBO 参数从 `MaterialAsset` 按名称和反射 offset 投影。
- 纹理槽按 sampler 名称持久化，可选择模型 base color、白色 fallback 或当前 glTF 已加载 image。
- UI 编辑前等待 GPU idle，再更新 live UBO/descriptor；这是 M3 的简单正确路径，后续可改为每帧 UBO 与 descriptor 双缓冲消除等待。
- 编译进行中锁定 Inspector，防止 pending snapshot 覆盖同时发生的编辑。

## 当前约束

- set 1 最多一个 UBO，位于 binding 0。
- 纹理使用 combined image sampler；descriptor array、storage image/buffer 暂不支持。
- mat3 因 std140 matrix stride 投影尚未实现，会给出明确错误；float/int/uint/bool、vec2/3/4、mat4 可反射。

## 验证

- 默认虹彩 shader 已改为 `MaterialParams` + `baseColorTexture`，Inspector 自动出现 5 个参数和 1 个纹理槽。
- Debug `/W4 /WX` 与 CTest 通过。
- reload smoke 成功应用动态布局，语法错误保持旧兼容组。
- 100 次顺序动态 descriptor 重建全部应用，退出码 0，DeletionQueue 归零。
- 本地截图：`build/m3-inspector-preview/2.png`（Git 忽略）。
