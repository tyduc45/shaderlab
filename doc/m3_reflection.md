# M3.1 反射与材质真相层

日期：2026-08-15

## 已完成

- shaderc 成功后立即用 SPIRV-Reflect 解析 descriptor binding。
- 记录 set、binding、descriptor 类型、数组数量、UBO padded size，以及成员名称、类型、offset 和 size。
- 对排序后的完整布局计算稳定 `layoutHash`，供后续 pipeline-only 快速路径使用。
- 解析源码 `@param` 的显示名、UI 类型、默认值、range、group 与 tooltip。
- `MaterialAsset` 以参数名和纹理名为唯一真相；反射中暂时消失的值不删除，类型改变时才重置。
- `CompileResult` 原子携带 SPIR-V、ReflectionResult 与 ParamMetadata，失败不产生可应用状态。

## 验证

- 单测编译带 `MaterialParams` UBO 和 `sampler2D` 的 fragment shader。
- 验证 UBO binding、成员顺序/offset、纹理枚举、元数据和 baseColor 模型贴图默认映射。
- MSVC Debug `/W4 /WX` 构建通过。
- CTest 1/1 通过。

## 下一步

扩展 `GpuState`，让反射得到的 set 1 layout、descriptor pool/set、UBO 和 pipeline
作为一个兼容组构建并在帧边界原子替换。
