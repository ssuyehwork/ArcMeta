# 工业级 UI 架构深度分析与修改方案 (Analysis_Modification_Plan-21.md)

## 1. MainWindow 主界面容器架构分析

经过对 `src/ui/MainWindow.cpp` 的物理排查，主界面采用 **5 容器物理分割架构**，通过 `QSplitter` 进行管理。

### 1.1 容器像素分配 (初始状态)
| 容器名称 | 对应对象 | 初始像素宽度 | 拉伸权重 (Stretch Factor) | 视觉功能 |
| :--- | :--- | :--- | :--- | :--- |
| **分类面板** | `CategoryPanel` | 230px | 0 (固定) | 系统预设分类、我的分类 |
| **目录导航** | `NavPanel` | 230px | 0 (固定) | 磁盘目录树、收藏夹 |
| **内容面板** | `ContentPanel` | **600px** | **1 (主拉伸区)** | 文件列表/网格显示核心区 |
| **元数据面板** | `MetaPanel` | 230px | 0 (固定) | 文件详细属性、标签、色板 |
| **筛选面板** | `FilterPanel` | 230px | 0 (固定) | 动态属性过滤 |

### 1.2 物理边界规则
- **分割条宽度**: 5px。
- **拉伸逻辑**: 当窗口缩放时，只有 **内容面板** 会动态调整宽度，其余四个侧边面板保持 230px 的物理宽度，除非用户手动拖拽分割条。
- **最小尺寸约束**: `setMinimumSize(1180, 653)`，确保在低分辨率下依然能完整显示各容器。

---

## 2. ContentPanel 列表视图列宽分析

当前 `ContentPanel` 的 `m_treeView` 采用 **7 列架构**，但在代码实现中存在逻辑缺失，导致无法满足“修改日期 150px 最小值”且“可手动调整”的需求。

### 2.1 当前列架构与像素现状
| 索引 | 列名 | 当前代码设定 | 物理表现 |
| :--- | :--- | :--- | :--- |
| 0 | 名称 | 默认 | 自动分配 |
| 1 | 状态 | 默认 | 自动分配 |
| 2 | 星级 | 默认 | 自动分配 |
| 3 | 颜色标记 | 默认 | 自动分配 |
| 4 | 类型 | 默认 | 自动分配 |
| 5 | 大小 | 默认 | 自动分配 |
| 6 | 修改日期 | **未设定最小值** | 随容器拉伸 (Stretch) |

### 2.2 逻辑故障点分析 (为何无法手动调整？)
用户反馈“设定 150px 最小值后无法手动调整”，底层原因在于：
1. **Stretch 模式锁定**: 代码中开启了 `setStretchLastSection(true)`。在 Qt 默认行为下，最后一列如果设为 Stretch 模式，其宽度是由视口剩余空间计算出来的，手动调整往往会被布局引擎即时重置。
2. **持久化干扰**: `restoreState()` 逻辑会在启动时覆盖所有手动设置的列宽。
3. **ResizeMode 冲突**: 默认可能处于非 `Interactive` 模式。

---

## 3. 详细修改方案 (逻辑修正)

为了实现 **“修改日期最小 150px，且允许手动调整”**，必须打破“最后一列 Stretch”的傻瓜式限制。

### 3.1 方案：交互式自平衡布局 (核心代码修改建议)
**目标文件：** `src/ui/ContentPanel.cpp` 中的 `initListView()`

1. **废除最后一列拉伸**: 改为由“名称”列吸收多余空间。
2. **显式设定最小值**: 使用 `setMinimumSectionSize`。
3. **开启交互模式**: 强制所有列为 `Interactive`。

```cpp
// 修改建议代码段：
void ContentPanel::initListView() {
    auto* header = m_treeView->header();

    // 1. 物理红线：设定全局最小列宽，确保修改日期不会被挤压
    header->setMinimumSectionSize(50); // 全局最小值

    // 2. 模式重构：禁止最后一列自动拉伸，防止它锁死
    header->setStretchLastSection(false);

    // 3. 关键：设定每一列的调整模式为 Interactive
    for(int i = 0; i < 7; ++i) {
        header->setSectionResizeMode(i, QHeaderView::Interactive);
    }

    // 4. 物理锁定：单独为“修改日期”设定 150px 物理红线
    header->setMinimumSectionSize(6, 150); // 假设索引6是修改日期
    header->resizeSection(6, 150);

    // 5. 弹性平衡：让第一列（名称）占据剩余所有空间
    header->setSectionResizeMode(0, QHeaderView::Stretch);
}
```

### 3.2 逻辑闭环：防止持久化失效
在 `sectionResized` 信号中增加保护，确保当用户调整列宽后，状态能正确保存，且不会在 `loadDirectory` 重置模型时丢失：

```cpp
connect(header, &QHeaderView::sectionResized, [this, header]() {
    // 仅在非初始化状态下保存，防止启动时的抖动
    if (!m_isLoading) {
        QSettings s("ArcMeta团队", "ArcMeta");
        s.setValue("UI/ListHeaderState", header->saveState());
    }
});
```

---

## 4. 结论
目前的架构在侧边栏容器分配上非常清晰（230px * 4），但在列表视图的列宽控制上过于依赖 Qt 的默认自动布局。通过上述**“名称列 Stretch + 日期列 Minimum + 全员 Interactive”**的方案，可以完美解决用户遇到的无法手动调整宽度的逻辑错误。
