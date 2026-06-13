# 文件夹入库进度标识实现方案 (Analysis_Modification_Plan-27)

## 1. 核心设计理念
本方案将文件夹的“已入库”状态从二元 (是/否) 升级为进度感知模式。通过递归统计文件夹内所有层级的文件总数与已登记数，计算出入库百分比，并以圆弧进度环形式呈现在卡片右上角。

## 2. 逻辑契约扩展 (ModelContract)

### 2.1 新增数据角色
在 `src/core/ModelContract.h` 的 `CommonRole` 枚举中添加：
```cpp
RegistrationProgressRole = Qt::UserRole + 205 // 文件夹入库进度 (double, 0.0 ~ 1.0)
```

### 2.2 结构体升级
在 `src/core/IndexedEntry.h` 的 `ItemRecord` 结构体中添加成员：
```cpp
double registrationProgress = -1.0; // 初始为 -1.0 表示未计算
```

## 3. 模型层实现 (Model Implementation)

在 `src/ui/ContentPanel.cpp` 中的 `FerrexVirtualDbModel::data` 函数内增加对新角色的支持：

```cpp
// 在 QVariant FerrexVirtualDbModel::data(const QModelIndex& index, int role) const 中
} else if (role == RegistrationProgressRole) {
    return record.registrationProgress;
}
```

## 4. 数据引擎计算逻辑 (ContentPanel)

在 `ContentPanel.cpp` 的加载逻辑中执行递归统计。建议在工作线程中调用此逻辑：

```cpp
// 在 ContentPanel 类中增加私有递归辅助函数
double ContentPanel::calculateFolderProgress(const QString& folderPath) {
    long totalCount = 0;
    long managedCount = 0;

    // 高效递归统计
    std::function<void(const QString&)> scan;
    scan = [&](const QString& p) {
        QDir dir(p);
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot);
        for (const auto& info : entries) {
            totalCount++;
            // 穿透元数据缓存判定
            if (MetadataManager::instance().getMeta(info.absoluteFilePath().toStdWString()).hasUserOperations()) {
                managedCount++;
            }
            if (info.isDir()) scan(info.absoluteFilePath());
        }
    };

    scan(folderPath);
    return (totalCount == 0) ? 0.0 : (double)managedCount / totalCount;
}

// 集成点：在 loadDirectory 或 loadCategory 的异步扫描 lambda 内：
if (r.isDir) {
    r.registrationProgress = calculateFolderProgress(absPath);
}
```

## 5. UI 渲染逻辑 (Delegate Painting)

针对 `ThumbnailDelegate.cpp` 与 `ContentPanel.cpp` (GridItemDelegate) 的 `paint` 函数进行如下改造：

```cpp
// 找到绘制状态图标的位置
bool isDir = index.data(TypeRole).toString() == "folder";
double progress = index.data(RegistrationProgressRole).toDouble();
QRect statusRect(m.cardRect.right() - 22, m.cardRect.top() + 8, 16, 16);

if (isDir && progress >= 0.0 && progress < 1.0) {
    // --- 绘制进度环 (开箱即用代码) ---
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    
    // 1. 底环
    painter->setPen(QPen(QColor(60, 60, 60, 180), 2));
    painter->drawEllipse(statusRect.adjusted(1, 1, -1, -1));
    
    // 2. 进度弧 (品牌蓝 #3498db)
    QPen pPen(QColor("#3498db"), 2);
    pPen.setCapStyle(Qt::RoundCap);
    painter->setPen(pPen);
    
    int spanAngle = -qRound(progress * 360 * 16); // 逆时针计算
    painter->drawArc(statusRect.adjusted(1, 1, -1, -1), 90 * 16, spanAngle);
    painter->restore();
} else {
    // 3. 维持原有打勾逻辑 (普通文件或 100% 目录)
    bool isManaged = index.data(ManagedRole).toBool();
    if (isManaged || progress >= 1.0) {
        UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16).paint(painter, statusRect);
    }
}
```

## 6. 交互提示 (Tooltip)

在 `ThumbnailDelegate::editorEvent` 或单独处理 `helpEvent`：

```cpp
// 建议在 Delegate 中实现
bool ThumbnailDelegate::helpEvent(QHelpEvent* event, QAbstractItemView* view, 
                                const QStyleOptionViewItem& option, const QModelIndex& index) {
    Metrics m = calculateMetrics(option);
    QRect statusRect(m.cardRect.right() - 22, m.cardRect.top() + 8, 16, 16);

    if (statusRect.contains(event->pos())) {
        double p = index.data(RegistrationProgressRole).toDouble();
        if (p >= 0.0) {
            ToolTipOverlay::instance()->showText(event->globalPos(), 
                QString("登记进度: %1%").arg(qRound(p * 100)));
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}
```

## 7. 性能优化总结
- **利用 SoA 索引**：计算过程中尽量通过 `MftReader` 的 SoA 结构查询子项，替代 `QDir` 的磁盘枚举以提升万级目录的响应速度。
- **线程分流**：计算逻辑必须位于 `QtConcurrent::run` 或 `QThreadPool` 中。
- **差异化刷新**：仅在进度值发生跨度变化（如超过 1%）时才发送 `dataChanged` 信号。

## 8. 预期效果
- 文件夹卡片右上角将出现一个灵动的蓝色进度弧。
- 普通文件继续保持简洁的绿色打勾。
- 鼠标悬停瞬间告知用户递归入库进度，极大提升了对大规模文件夹管理的可视化程度。
