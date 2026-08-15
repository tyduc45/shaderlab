# M3 验收记录 — 反射参数系统

日期：2026-08-15

## 当前结论

M3 实现与自动验收通过，等待用户完成 Material Inspector 可见交互验收后正式收口。

## 自动验收

- MSVC Debug `/W4 /WX` 构建通过。
- CTest 覆盖 descriptor/UBO 反射、`@param`、名称持久化、删除后恢复和类型变化重置。
- Vulkan reload smoke 从固定 M2 layout 切换到 `MaterialParams + sampler2D` 动态 layout，退出码 0。
- Material smoke 将 `engravingStrength` 从默认值改为 0.125，再次重编译后值保持 0.125。
- 100 次顺序重载全部应用；generation 2–100 共 99 次命中 layout-unchanged 快速路径，DeletionQueue 归零。
- 语法错误继续保持上一代完整 pipeline + descriptor 兼容组。
- Inspector 截图 `build/m3-inspector-preview/2.png` 显示 5 个反射参数、4 个分组和 baseColor 纹理槽。

## 待用户可见验收

1. 运行 `build/Debug/shaderlab.exe`，按 F5；确认右侧 Material Inspector 出现 Coating、Engraving、Lighting 和 Textures。
2. 拖动 Iridescence、Engraving Strength 或 Rim Strength，确认画面立即变化。
3. 修改 shader 的颜色计算但不改变参数声明，再按 F5；确认调好的值不变。
4. 可选：临时新增一个 `float` UBO 成员及 `@param` 后编译，确认出现滑块；删除再恢复时旧值自动回来。
5. 可选：把该成员类型改成 `vec3`，确认使用新默认值且 Console 显示类型变化重置提示。

用户确认后，将 M3 状态改为 DONE，提交并推送 milestone 收口记录，再进入 M4。
