# 修复内容面板选中项目无法拖拽到侧边栏分类功能 —— Modification_Plan-23.md

> 状态：已批准，执行中 / 已执行完成

## 1. 任务背景
在以前修改代码的过程中，内容面板中的拖拽功能被破坏，导致现在在内容面板选中项目后（对应用户原话：“在内容面板选中项目后”），无法拖拽项目到侧边栏分类（对应用户原话：“无法拖拽项目到侧边栏分类”）。本方案旨在从底层模型和视图机制彻底修复拖拽通路，恢复正常的拖拽交互逻辑。

## 2. 问题定位
1. **拖拽标识缺失**：内容面板对应的数据模型 `LibraryAssetModel`（受控逻辑分类模式）和 `DiskItemModel`（磁盘物理导航模式）继承自 `ItemModelBase`（继而继承自 `QAbstractTableModel`），但两者均没有重写 `flags()` 函数。`QAbstractTableModel::flags()` 的默认实现不包含 `Qt::ItemIsDragEnabled`。由于没有这个标识，`QAbstractItemView`（如 `DropJustifiedView`、`DropTreeView`）因安全校验而彻底阻断了拖放行为的触发。
2. **MimeData 空指针安全隐患**：在内容面板视图的 `startDrag` 函数中，直接使用 `model()->mimeData(indexes)` 的返回值而不进行非空校验，在底层未就绪时存在空指针解引用崩溃风险。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 在内容面板选中项目后（对应用户原话：“在内容面板选中项目后”） | 在 `DropTreeView::startDrag`、`DropListView::startDrag`、`DropJustifiedView::startDrag` 中对选中的项目进行 MimeData 装配与安全性强化 | ✅ |
| 2    | 无法拖拽项目到侧边栏分类（对应用户原话：“无法拖拽项目到侧边栏分类”） | 为底层核心模型 `LibraryAssetModel` 和 `DiskItemModel` 注入 `Qt::ItemIsDragEnabled` 标识，并完善 MimeData 空安全保护，彻底恢复拖拽通路 | ✅ |

## 4. 详细解决方案
> 本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改模型层以支持拖放交互（重写 flags 方法）

1. **修改 `src/ui/models/LibraryAssetModel.h`**
```
<<<<<<< SEARCH
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
=======
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
>>>>>>> REPLACE
```

2. **修改 `src/ui/models/LibraryAssetModel.cpp`**
```
<<<<<<< SEARCH
    if (metaUpdated) {
        if (!record.isCategory) {
            m_metaCache.remove(path);
            updateRecordMetadata(path);
        } else {
            emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount() - 1));
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CategoryOnly);
        }
        return true;
    }
    return false;
}

void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
=======
    if (metaUpdated) {
        if (!record.isCategory) {
            m_metaCache.remove(path);
            updateRecordMetadata(path);
        } else {
            emit dataChanged(this->index(index.row(), 0), this->index(index.row(), columnCount() - 1));
            MetadataManager::instance().notifyUI(MetadataManager::RefreshLevel::CategoryOnly);
        }
        return true;
    }
    return false;
}

Qt::ItemFlags LibraryAssetModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return QAbstractTableModel::flags(index);
    return QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
}

void LibraryAssetModel::loadThumbnailsForRows(const QList<int>& rows) {
>>>>>>> REPLACE
```

3. **修改 `src/ui/models/DiskItemModel.h`**
```
<<<<<<< SEARCH
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
=======
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    const std::vector<ArcMeta::ItemRecord>& allRecords() const override { return m_allRecords; }
>>>>>>> REPLACE
```

4. **修改 `src/ui/models/DiskItemModel.cpp`**
```
<<<<<<< SEARCH
QVariant DiskItemModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();
=======
Qt::ItemFlags DiskItemModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return QAbstractTableModel::flags(index);
    return QAbstractTableModel::flags(index) | Qt::ItemIsDragEnabled;
}

QVariant DiskItemModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();
>>>>>>> REPLACE
```

### 4.2 对视图层的拖拽初始化逻辑进行非空安全加固

5. **修改 `src/ui/DropTreeView.cpp`**
```
<<<<<<< SEARCH
void DropTreeView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    // 核心增强：拦截并注入物理路径 QUrl，确保 CategoryPanel 接收校验通过
    QMimeData* mimeData = model()->mimeData(indexes);
    QList<QUrl> urls;
=======
void DropTreeView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    // 核心增强：拦截并注入物理路径 QUrl，确保 CategoryPanel 接收校验通过
    QMimeData* mimeData = model()->mimeData(indexes);
    if (!mimeData) {
        mimeData = new QMimeData();
    }
    QList<QUrl> urls;
>>>>>>> REPLACE
```

6. **修改 `src/ui/DropListView.cpp`**
```
<<<<<<< SEARCH
void DropListView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    Logger::log(QString("[列表视图] 开始拖拽 | 选中项数量: %1").arg(indexes.count()));

    // 核心增强：拦截并注入物理路径 QUrl，确保 CategoryPanel 接收校验通过
    QMimeData* mimeData = model()->mimeData(indexes);
    QList<QUrl> urls;
=======
void DropListView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    Logger::log(QString("[列表视图] 开始拖拽 | 选中项数量: %1").arg(indexes.count()));

    // 核心增强：拦截并注入物理路径 QUrl，确保 CategoryPanel 接收校验通过
    QMimeData* mimeData = model()->mimeData(indexes);
    if (!mimeData) {
        mimeData = new QMimeData();
    }
    QList<QUrl> urls;
>>>>>>> REPLACE
```

7. **修改 `src/ui/DropJustifiedView.cpp`**
```
<<<<<<< SEARCH
void DropJustifiedView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    QMimeData* mimeData = model()->mimeData(indexes);
    QList<QUrl> urls;
=======
void DropJustifiedView::startDrag(Qt::DropActions supportedActions) {
    QModelIndexList indexes = selectedIndexes();
    if (indexes.isEmpty()) return;

    QMimeData* mimeData = model()->mimeData(indexes);
    if (!mimeData) {
        mimeData = new QMimeData();
    }
    QList<QUrl> urls;
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.h`
- [ ] 模块/文件：`src/ui/models/LibraryAssetModel.cpp`
- [ ] 模块/文件：`src/ui/models/DiskItemModel.h`
- [ ] 模块/文件：`src/ui/models/DiskItemModel.cpp`
- [ ] 模块/文件：`src/ui/DropTreeView.cpp`
- [ ] 模块/文件：`src/ui/DropListView.cpp`
- [ ] 模块/文件：`src/ui/DropJustifiedView.cpp`

**明确禁止越界修改的范围：**
- [ ] 侧边栏及内容面板其余 UI 视觉与结构代码——不修改

## 6. 实现准则与预警【核心】
1. **依赖项导入**：确保底层重写的 `flags()` 方法完全符合 Qt 签名 `Qt::ItemFlags flags(const QModelIndex& index) const override`，避免签名不匹配导致多态失效。
2. **空安全防护**：对从 `model()->mimeData` 取得的指针必须进行有效的非空保护判定，若为 null 则立即实例化一个新对象。
3. **结合上下文**：本次修改在 `LibraryAssetModel` 和 `DiskItemModel` 继承体系中自适应增加多态机制，完全不干扰其他业务和数据存储。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 拖拽 Mime 数据规范 | 本次不涉及 Memories.md 中的专属定制图像，但符合标准的原生 QMimeData 装配流程，且对 QMimeData 进行了空安全防护 | ✅ |
| 双轨模式隔离 | 在物理模式 `DiskItemModel` 和托管逻辑模式 `LibraryAssetModel` 中独立并重写各自的 flags，彼此在物理上完全断连和隔离，符合 100% 独立隔离机制 | ✅ |

## 8. 待确认事项（可选）
本方案中关于拖放模型 flags 和 MimeData 的安全性强化方法在用户原话中属于辅助性的技术实现细节，特此说明（不作为对用户意图的偏离）。
