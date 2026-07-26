# 磁盘模式下重命名导致缩略图变灰设计缺陷修复 —— Modification_Plan-94.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在磁盘导航模式下，用户对图形文件重命名后，原有的缩略图会瞬间消失并退化为纯灰色占位符。本方案旨在彻底分析并解决这一缩略图缓存 Key 在重命名发生变更时未执行同步迁移导致的设计缺陷，实现缩略图与卡片大小的高性能无缝继承。

## 2. 问题定位
在 `src/ui/ContentPanel.cpp` 中，`FerrexVirtualDbModel::setData` 处理重命名（`EditRole`）逻辑：
```cpp
auto& mutableRec = weakThis->m_allRecords[row];
mutableRec.path = nativeNewPath; // 1. 内存中将路径更新为了新路径
mutableRec.filename = newName;
weakThis->m_metaCache.remove(oldPath); // 2. 仅清除了元数据缓存，但遗漏了缩略图和宽高比缓存
```
由于 `FerrexVirtualDbModel` 使用绝对物理路径作为 `m_iconCache`（缩略图缓存）与 `m_aspectRatios`（宽高比缓存）的唯一检索 Key。
当重命名执行后，旧路径 `oldPath` 改变为新路径 `nativeNewPath`，导致后续主线程重绘 `data(index, Qt::DecorationRole)` 时由于无法命中最优缓存，且没有重新被加入到后台异步线程提取队列中，从而发生永久性变灰。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 磁盘打开某个文件夹对文件重命名，缩略图变灰（我的理解） | 重命名成功后对缩略图及宽高比缓存执行同步 Key 迁移，确保新路径继续命中优质缓存 | ✅ 一致 |

## 4. 详细解决方案

在 `FerrexVirtualDbModel::setData` 异步重命名成功、通过 `invokeMethod` 回到主线程的回调逻辑中，**无损地将旧 Key 对应的缓存指针与宽高比元数据迁移到新 Key 的映射下**。

具体在 `QMetaObject::invokeMethod` 的主线程回调体内：
```cpp
// 1. 无损迁移缩略图缓存 (m_iconCache)
QIcon* oldIconPtr = weakThis->m_iconCache.take(oldPath); // 将旧路径对应的 QIcon 弹出（QCache 会移交所有权）
if (oldIconPtr) {
    weakThis->m_iconCache.insert(nativeNewPath, oldIconPtr); // 重新插入到新路径下
}

// 2. 无损迁移宽高比缓存 (m_aspectRatios)
QString oldNativeKey = QDir::toNativeSeparators(oldPath);
QString newNativeKey = QDir::toNativeSeparators(nativeNewPath);
if (weakThis->m_aspectRatios.contains(oldNativeKey)) {
    double oldRatio = weakThis->m_aspectRatios.take(oldNativeKey);
    weakThis->m_aspectRatios[newNativeKey] = oldRatio;
}
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`
  - 函数：`FerrexVirtualDbModel::setData`

**明确禁止越界修改的范围：**
- [ ] 物理磁盘 USN 监控感知底座——不修改
- [ ] MFT 解析器及后台多媒体特征物理提取管线——不修改

## 6. 实现准则与预警【核心】
1. **安全迁移所有权**：在从 `QCache` 弹出（`take`）指针时，注意必须确保在成功弹出后立刻重新 `insert` 到新 Key 中，以防指针由于脱离生命周期发生析构或内存泄漏。
2. **QDir::toNativeSeparators 一致性**：宽高比缓存使用的是经过 Windows 规范化的 Native 路径作为 Key，所以在迁移 `m_aspectRatios` 时必须对 `oldPath` 和 `nativeNewPath` 进行 `QDir::toNativeSeparators` 转换以确保绝对对齐。
3. **性能零开销**：此方案全部在主线程内存中通过 $\mathcal{O}(1)$ 级的哈希映射平滑重命名，完全避免了重新向后台投递提取任务、二次触发 I/O 读盘和 SVG 解析的巨额开销，做到真正的开箱即用。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 异步加载防闪烁规范 | 在异步数据扫描前，禁止调用 `m_model->clear()`，应平滑维护 | ✅ 符合。本方案为原地增量无损更新，没有也不需要调用 `clear()` 重置 |

## 8. 待确认事项（可选）
（无）
