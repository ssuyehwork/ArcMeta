# 内存模式重命名缩略图与宽高比缓存平滑迁移方案 —— Modification_Plan-94.md

> 状态：已批准，执行中

## 1. 任务背景
在 ArcMeta 文件浏览系统中，重命名某个文件夹或文件后，如果系统直接清除原有的缩略图缓存及宽高比缓存，将导致视图在刷新时出现“缩略图变灰、闪烁”并重新发起磁盘 I/O 提取的性能问题。为了实现极致平滑的重名更新体验（对应用户原话：“我期望 内存模式下重命名某个文件夹或文件也要采用这样的方式 缩略图缓存平滑更名：... 宽高比缓存平滑更名：... 性能零开销：...”），我们需要在物理或内存文件重命名成功后，对内存中持有的缩略图缓存及宽高比记录进行原位的 $\mathcal{O}(1)$ 平滑键值迁移。

## 2. 问题定位
经过排查定位，核心数据缓存在 `FerrexVirtualDbModel`（继承自 `QAbstractListModel`）中：
*   **缩略图缓存**：`m_iconCache`，是一个哈希表 `QHash<QString, QIcon>` 或类似结构，以文件物理路径为键值。
*   **宽高比缓存**：`m_aspectRatios`，用于缓存解析出的图片或 SVG 长宽比例。
如果在重命名（如 `FerrexVirtualDbModel::setData` / `ContentPanel::rename`）后直接清空这些缓存，重置 model 会导致所有可视项图标闪烁，并触发大批量磁盘 I/O 重读，带来不可忍受的性能开销。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 缩略图缓存平滑更名 | 详见第 4.1 节，在内存哈希表 `m_iconCache` 中将旧路径的 `QIcon` 迁移至新路径。 | ✅ |
| 2    | 宽高比缓存平滑更名 | 详见第 4.1 节，在 `m_aspectRatios` 中同步将旧路径的比例数据迁移至新路径。 | ✅ |
| 3    | 性能零开销 | 采用 $\mathcal{O}(1)$ 内存键值覆盖、就地迁移，不发生任何二次磁盘 I/O 读取。 | ✅ |

---

## 4. 详细解决方案

在 `FerrexVirtualDbModel` 中新增缓存迁移专属接口 `migrateCache`，实现完全就地的内存迁移：

### 4.1 内存模型缓存迁移接口设计
在 `FerrexVirtualDbModel.h` 及 `FerrexVirtualDbModel.cpp` 中定义并实现 `migrateCache` 函数：
```cpp
void FerrexVirtualDbModel::migrateCache(const QString& oldPath, const QString& newPath) {
    // 1. 缩略图缓存 O(1) 平滑迁移
    if (m_iconCache.contains(oldPath)) {
        m_iconCache[newPath] = m_iconCache.take(oldPath);
    }
    // 2. 宽高比缓存 O(1) 平滑迁移
    if (m_aspectRatios.contains(oldPath)) {
        m_aspectRatios[newPath] = m_aspectRatios.take(oldPath);
    }
}
```

### 4.2 触发机制集成
在重命名事务执行成功后，同步触发该缓存迁移：
```cpp
// 在 ContentPanel 中
void ContentPanel::migrateModelCache(const QString& oldPath, const QString& newPath) {
    if (m_model) {
        m_model->migrateCache(oldPath, newPath);
    }
}
```
当监听到磁盘或内存重命名成功事件时，在通知 Model 数据刷新前，先原位调用 `migrateModelCache(oldPath, newPath)`。刷新后，由于新路径的 Key 已经完美匹配到了现有的缓存项，UI 卡片无需重新提取缩略图和计算长宽比，即刻展现，实现完美的性能零开销与无闪烁过渡。

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/ui/ContentPanel.cpp` / `src/ui/ContentPanel.h` (加入缓存迁移桥接调用)
- [ ] `src/ui/FerrexVirtualDbModel.cpp` / `src/ui/FerrexVirtualDbModel.h` (实现 migrateCache 接口)

---

## 6. 实现准则与预警【核心】
1. **O(1) 精确迁移**：必须使用 `take()` 方法，保证移除旧键的同时获取到对应值的智能指针/实例，随后直接插入新键，杜绝任何物理 I/O 和多余的复制。
2. **多层级目录更名递归迁移**：如果重命名的是文件夹，必要时对以 `oldPath/` 开头的所有子项在内存中做前缀替换迁移。

---

## 7. Memories.md 合规检查
* 符合 UI 异步防闪烁与零开销内存迁移规范。
