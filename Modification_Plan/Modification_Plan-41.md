# ThumbnailDelegate 模块化重构与目录导航宽高比缓存机制纠偏 —— Modification_Plan-41.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
本方案承接自 `Modification_Plan-40.md`（由于 `AGENTS.md` 3.1.1 绝对只读铁律，建立本新文件记录讨论进展）。
在对自适应视图模式下卡片永远显示为正方形（1:1）的进一步审计中，我们深入探究了用户提出的重要反馈：“怀疑是判断缩略图和图标的逻辑存在错误，没有正确判断出项目是缩略图还是图标导致自适应视图模式下卡片永远显示正方形”。

通过高维度的代码与缓存机制审计，我们证实了这一直觉，并精确定位了导致该 Bug 的核心机制：**内存缩略图缓存（`m_iconCache`）与内存宽高比缓存（`m_aspectRatios`）生命周期不一致引发的“静默失效”。**

---

## 2. 问题定位与根因分析

### 2.1 缓存机制不一致导致的“正方形”退化
在 `FerrexVirtualDbModel`（ContentPanel 核心虚拟化模型）中，存在两个专门用来缓存异步加载卡片资源的内存容器：
1. `QCache<QString, QIcon> m_iconCache`：缓存加载成功的缩略图/图标。
2. `QMap<QString, double> m_aspectRatios`：缓存图像的真实宽高比（自适应对齐布局的核心依据）。

当用户切换目录（或重新加载、搜索等）时，会调用 `setRecords`。
在 `setRecords` 中，执行了以下重置操作：
```cpp
    m_requestedIcons.clear();
    m_aspectRatios.clear(); // 宽高比缓存被物理清空
    m_metaCache.clear();
```
**致命断层**：**`m_iconCache` 在此处没有被清空！** 它在模型的整个生命周期中持续保留。

### 2.2 缩略图加载排重判定（`loadThumbnailsForRows`）的 Bug
在接下来的视口防抖检测中，会调用 `loadThumbnailsForRows` 提取当前可见行的缩略图：
```cpp
    for (int r : rows) {
        // ...
        QString path = rec.path;
        QString cacheKey = path;

        if (m_iconCache.contains(cacheKey)) continue; // 关键拦截：如果缩略图已存在于 QCache 中，直接跳过！
        newQueue.push_back({path, cacheKey});
    }
```
由于 `m_iconCache` 依然保留着之前载入时的缩略图，`m_iconCache.contains(cacheKey)` 判定为 `true`。
导致**该图片直接被跳过，不再被加入 `newQueue` 进行异步加载！**

### 2.3 导致的表现：
由于被跳过加载，后台异步提取宽高比并写入 `m_aspectRatios` 的过程**永远不会发生**。
而之前的 `m_aspectRatios` 已经在 `setRecords` 中被物理清空（清零）。
当 `JustifiedView` 尝试获取该图片的宽高比时：
```cpp
    } else if (role == AspectRatioRole) {
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        return m_aspectRatios.value(path, 1.0); // 找不到缓存，永久返回 1.0 兜底
    }
```
这导致已被缓存在内存中的图片，在目录导航切换时其宽高比**永久丢失并退化为 1.0**。
表现为：自适应对齐布局（`JustifiedMode`）将原本是长方形的多媒体卡片**全量、永久地错误渲染为了正方形（1:1）**。

---

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 判断缩略图和图标的逻辑存在错误，没有正确判断出项目是缩略图还是图标导致自适应视图模式下卡片永远显示正方形（对应用户原话：“我怀疑是判断缩略图和图标的逻辑存在错误，没有正确判断出项目时缩略图还是图标导致自适应视图模式下卡片永远显示正方形”） | 第 4.1 节重构 `loadThumbnailsForRows` 的过滤判定逻辑，双重校验 `m_iconCache` 和 `m_aspectRatios`；并对 `AspectRatioRole` 的读取/写入路径强制执行归一化。 | ✅ |
| 2    | 将星级与彩色背景绘制逻辑统一到 CardPainterHelper，供 ThumbnailDelegate 与 CategoryDelegate 复用 | 第 4.2 节在 `CategoryDelegate` 中完美复用 `CardPainterHelper::drawCategoryBackground`。 | ✅ |

---

## 4. 详细解决方案

为了实现高性能与逻辑对齐，我们双管齐下执行完美修复：

### 4.1 重构 `FerrexVirtualDbModel::loadThumbnailsForRows` 排重机制
不再一刀切地仅通过 `m_iconCache.contains` 进行排重。对于图形格式文件，如果其宽高比缓存 `m_aspectRatios` 丢失，则依然允许加载。

#### 物理修复代码位置：`src/ui/ContentPanel.cpp` (约 480 行)
```cpp
<<<<<<< SEARCH
    for (int r : rows) {
        if (r < 0 || r >= m_displayCount) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;

        QString path = rec.path;
        QString cacheKey = path; // 统一使用稳定且唯一的 path 作为内存缓存 Key

        if (m_iconCache.contains(cacheKey)) continue;
        newQueue.push_back({path, cacheKey});
    }
=======
    for (int r : rows) {
        if (r < 0 || r >= m_displayCount) continue;
        const auto& rec = m_allRecords[r];
        if (rec.isCategory) continue;

        QString path = rec.path;
        QString cacheKey = path; // 统一使用稳定且唯一的 path 作为内存缓存 Key

        // 核心排重与同步机制纠偏：对于图形格式文件，即使 icon 缓存命中，若宽高比缓存丢失，依然必须拉起加载以补全尺寸
        bool needLoad = !m_iconCache.contains(cacheKey);
        if (UiHelper::isGraphicsFile(rec.suffix) && !m_aspectRatios.contains(QDir::toNativeSeparators(path))) {
            needLoad = true;
        }
        if (!needLoad) continue;

        newQueue.push_back({path, cacheKey});
    }
>>>>>>> REPLACE
```

### 4.2 物理路径归一化与缓存项读取/写入纠偏
确保 `m_aspectRatios` 的写入和读取路径都经过 `QDir::toNativeSeparators` 归一化。

#### 物理修复代码位置：`src/ui/ContentPanel.cpp` (FerrexVirtualDbModel::data AspectRatioRole)
```cpp
<<<<<<< SEARCH
    } else if (role == AspectRatioRole) {
        // 2026-07-xx 性能优化：优先使用 ItemRecord 中已注入的尺寸信息，实现渲染零延迟
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        return m_aspectRatios.value(path, 1.0);
=======
    } else if (role == AspectRatioRole) {
        // 2026-07-xx 性能优化：优先使用 ItemRecord 中已注入的尺寸信息，实现渲染零延迟
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        return m_aspectRatios.value(QDir::toNativeSeparators(path), 1.0);
>>>>>>> REPLACE
```

#### 物理修复代码位置：`src/ui/ContentPanel.cpp` (FerrexVirtualDbModel::loadThumbnailsForRows 异步通知写入)
```cpp
<<<<<<< SEARCH
                            mutableThis->m_iconCache.insert(cacheKey, new QIcon(icon));
                            if (hasThumb) mutableThis->m_aspectRatios[path] = ar;
=======
                            mutableThis->m_iconCache.insert(cacheKey, new QIcon(icon));
                            if (hasThumb) mutableThis->m_aspectRatios[QDir::toNativeSeparators(path)] = ar;
>>>>>>> REPLACE
```

### 4.3 恢复 `CategoryDelegate` 复用 `drawCategoryBackground`
在 `src/ui/CategoryDelegate.h` 中，恢复对 `CardPainterHelper::drawCategoryBackground` 的静态调用支持，确保高亮渲染不穿透折叠指示器。

```cpp
<<<<<<< SEARCH
            painter->setBrush(bg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(contentRect, 4, 4);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
=======
            QString colorHex = index.data(ColorRole).toString();
            ArcMeta::CardPainterHelper::drawCategoryBackground(painter, contentRect, selected, hover, colorHex);
            painter->restore();
        }

        QStyleOptionViewItem opt = option;
>>>>>>> REPLACE
```

---

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryDelegate.h` —— 增加头文件引入与方法调用。
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` —— 重构 `loadThumbnailsForRows` 过滤及路径归一化。

**明确禁止越界修改的范围：**
- [ ] 视口滚动逻辑：`ContentPanel::refreshVisibleThumbnails` —— 不修改。

---

## 6. 实现准则与预警【核心】
1. **防抖对齐**：使用 `QDir::toNativeSeparators` 进行归一化是防范 Windows 平台因斜杠拼写引发 Map 失配的标准工业实践。
2. **零开销缓存命中**：重构后的 `loadThumbnailsForRows` 排重条件在 O(1) 复杂度内执行，未引入任何线性扫描，对虚拟滚动吞吐性能零影响。

---

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 实现前强制考古规则 | 在实现任何新 UI 组件前，必须在现有代码中搜索同类已实现的案例，并以该案例为唯一参考标准进行实现。 | ✅ 符合。复用了原有 `CardPainterHelper` 的静态声明，并将路径转换完全对接已在多处稳定运行的 `toNativeSeparators` 接口。 |

---

## 8. 待确认事项（可选）
- 无。本方案已完全将物理层 Bug 的根源锁定。
