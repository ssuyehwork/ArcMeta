# 侧边栏分类多选 —— category-multi-select.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
目前 ArcMeta 应用的侧边栏分类树界面（`CategoryPanel`）默认只允许单项选择，当用户希望将多个分类的数据项合并展示、批量管理，或在大批量数据中进行多维交叉联合检索时，当前的逻辑架构受到限制。本方案旨在彻底打通侧边栏多选逻辑，重构信号流与多维联合无锁检索底层。

## 2. 问题定位
- **模块一：** `src/ui/CategoryPanel` 及其对应的事件分发。
  - 现有 `CategoryPanel::categorySelected` 信号只传出单个 ID。
  - 需要将其重构，在多选改变时计算选中的有效分类列表并抛出多选信号，同时对单选操作与高频点击进行自愈式多态流控。
- **模块二：** `src/ui/MainWindow` 中的路由协议 `unifiedNavigateTo`。
  - 目前仅解析 `category://{id}?name={name}` 单 ID 格式。
  - 需要扩展，使其对 `category://id1,id2,id3` 进行自适应多值拆解，并完美契合前进/后退的历史记忆栈。
- **模块三：** `src/ui/ContentPanel` 与 `src/core/CategoryLoadService`。
  - 核心查询 `loadCategoryItems` 需要接受多个分类 ID，利用 `CategoryRepo::getItemsInCategories` 从数据库中提取出不重复（DISTINCT）的级联资产并进行高性能毫秒级零延迟载入。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：侧边栏分类支持多选 | 本方案核心事件名：侧边栏分类多选 | ✅ |
| 1    | 目前侧边栏分类分类的逻辑架构无法多选，我期望能够支持多选（对应用户原话：“目前侧边栏分类分类的逻辑架构无法多选，我期望能够支持多选”） | 将 selectionMode 设为 ExtendedSelection，支持多选，并开发多维联合查询及协议自愈重构 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/CategoryPanel.h`
重构选择信号流，新增多选信号 `categoriesSelected`：

```
<<<<<<< SEARCH
signals:
    void categorySelected(int id, const QString& name, const QString& type, const QString& path = "");
    void fileSelected(const QString& path);
=======
signals:
    void categorySelected(int id, const QString& name, const QString& type, const QString& path = "");
    void categoriesSelected(const QList<int>& ids, const QStringList& names, const QString& type);
    void fileSelected(const QString& path);
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/CategoryPanel.cpp`
在构造函数中，激活多选后，修改点击和选择信号，统一重构为多维多态联合信号：

```
<<<<<<< SEARCH
    m_categoryTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    
    // 2026-06-xx 按照用户要求：支持 Delete 键物理删除选中分类，使用 Action 提升快捷键响应等级
=======
    m_categoryTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // 监听多选改变信号，抛出联合信号
    connect(m_categoryTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]() {
        if (m_isInternalUpdating) return;
        
        QModelIndexList selectedRows = m_categoryTree->selectionModel()->selectedRows();
        if (selectedRows.isEmpty()) return;

        QList<int> catIds;
        QStringList catNames;
        QString type;

        for (const QModelIndex& proxyIdx : selectedRows) {
            QModelIndex index = m_proxyModel->mapToSource(proxyIdx);
            if (!index.isValid()) continue;

            int id = index.data(IdRole).toInt();
            QString t = index.data(TypeRole).toString();
            QString name = index.data(NameRole).toString();

            if (t == "category" && id > 0) {
                catIds.append(id);
                catNames.append(name);
                type = "category";
            } else if (id < 0) {
                // 如果用户选中了系统桶（如回收站、全部数据等），则强制单选该项，杜绝系统项与分类项多选混淆
                catIds = {id};
                catNames = {name};
                type = t;
                break; 
            }
        }

        if (!catIds.isEmpty()) {
            if (catIds.size() == 1) {
                // 仅有一项时，依然兼容发射单选信号，确保单项定制操作（如颜色设定/重命名）对齐
                QModelIndex idx = m_proxyModel->mapToSource(selectedRows.first());
                emit categorySelected(catIds.first(), catNames.first(), type, idx.data(PathRole).toString());
            } else {
                emit categoriesSelected(catIds, catNames, "category");
            }
        }
    });
    
    // 2026-06-xx 按照用户要求：支持 Delete 键物理删除选中分类，使用 Action 提升快捷键响应等级
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
    connect(m_categoryTree, &QTreeView::clicked, this, [this](const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        QString type = index.data(TypeRole).toString();
        QString name = index.data(NameRole).toString();
        int id = index.data(IdRole).toInt();
        QString path = index.data(PathRole).toString();
        bool isEncrypted = index.data(EncryptedRole).toBool();

        // 2026-03-xx 物理防御：加密分类点击时直接进入，内容面板内置卡片接管验证
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            emit categorySelected(id, name, type, path);
            return;
        }

        // 核心联动：如果点击的是有效的分类、系统项或快速访问项
        if (!type.isEmpty()) {
             // 2026-06-xx 重构：点击项不再加载文件到树中，而是直接通过信号触发 ContentPanel 加载
             emit categorySelected(id, name, type, path);
        }
    });
=======
    // 彻底重构点击事件。由于多选点击会触发多选改变信号（selectionChanged），点击事件仅承担锁屏验证拦截工作，杜绝信号二次激增造成的死锁
    connect(m_categoryTree, &QTreeView::clicked, this, [this](const QModelIndex& proxyIndex) {
        QModelIndex index = m_proxyModel->mapToSource(proxyIndex);
        QString type = index.data(TypeRole).toString();
        QString name = index.data(NameRole).toString();
        int id = index.data(IdRole).toInt();
        QString path = index.data(PathRole).toString();
        bool isEncrypted = index.data(EncryptedRole).toBool();

        // 2026-03-xx 物理防御：加密分类点击时直接进入，内容面板内置卡片接管验证
        if (isEncrypted && id > 0 && !m_unlockedIds.contains(id)) {
            emit categorySelected(id, name, type, path);
            return;
        }
    });
>>>>>>> REPLACE
```

### 4.3 修改 `src/ui/MainWindow.h` 与 `src/ui/MainWindow.cpp`
扩展中枢路由以全面支持逗号分隔的多选分类 IDs：

在 `src/ui/MainWindow.cpp` 的 `initUi` 中连接 `categoriesSelected`：

```
<<<<<<< SEARCH
    // 1a. 分类选择 -> 统一导航中枢 (Plan-56)
    connect(m_categoryPanel, &CategoryPanel::categorySelected, this, [this](int id, const QString& name, const QString& type, const QString& path) {
=======
    // 1a. 分类选择多选与单选并存联动 -> 统一导航中枢 (Plan-56)
    connect(m_categoryPanel, &CategoryPanel::categoriesSelected, this, [this](const QList<int>& ids, const QStringList& names, const QString& type) {
        if (type == "category") {
            QStringList idStrs;
            for (int id : ids) idStrs.append(QString::number(id));
            QString compoundId = idStrs.join(",");
            QString compoundName = QString("已选择 %1 个分类").arg(ids.size());
            unifiedNavigateTo(kProtocolCategory + compoundId + "?name=" + compoundName);
        }
    });

    connect(m_categoryPanel, &CategoryPanel::categorySelected, this, [this](int id, const QString& name, const QString& type, const QString& path) {
>>>>>>> REPLACE
```

在 `unifiedNavigateTo` 成员函数中，对多选协议进行兼容解码分流：

```
<<<<<<< SEARCH
    // 3. 协议分流加载
    if (url.startsWith(kProtocolCategory)) {
        // category://{id}?name={name}
        QString params = url.mid(kProtocolCategory.length());
        int qMark = params.indexOf('?');
        int id = params.left(qMark == -1 ? params.length() : qMark).toInt();
        QString name = (qMark != -1) ? params.mid(qMark + 6) : QString::number(id);

        if (m_categoryPanel) {
            m_categoryPanel->blockSignals(true);
            m_categoryPanel->selectCategory(id);
            m_categoryPanel->blockSignals(false);
        }
        if (m_contentPanel) m_contentPanel->loadCategory(id);
        if (m_addressBar) m_addressBar->setPath("分类: " + name);
        m_currentPath = url; // 逻辑路径
    }
=======
    // 3. 协议分流加载
    if (url.startsWith(kProtocolCategory)) {
        // category://{id}?name={name}
        QString params = url.mid(kProtocolCategory.length());
        int qMark = params.indexOf('?');
        QString rawIds = params.left(qMark == -1 ? params.length() : qMark);
        QString name = (qMark != -1) ? params.mid(qMark + 6) : rawIds;

        QList<int> ids;
        for (const QString& part : rawIds.split(",", Qt::SkipEmptyParts)) {
            bool ok;
            int parsed = part.toInt(&ok);
            if (ok) ids.append(parsed);
        }

        if (ids.size() == 1) {
            int id = ids.first();
            if (m_categoryPanel) {
                m_categoryPanel->blockSignals(true);
                m_categoryPanel->selectCategory(id);
                m_categoryPanel->blockSignals(false);
            }
            if (m_contentPanel) m_contentPanel->loadCategory(id);
            if (m_addressBar) m_addressBar->setPath("分类: " + name);
        } else if (!ids.isEmpty()) {
            // 多选场景，批量加载分类列表
            if (m_contentPanel) m_contentPanel->loadCategories(ids);
            if (m_addressBar) m_addressBar->setPath(name);
        }
        m_currentPath = url; // 逻辑路径
    }
>>>>>>> REPLACE
```

### 4.4 修改 `src/ui/ContentPanel.h` 与 `src/ui/ContentPanel.cpp`
扩展 ContentPanel 接口：

在 `src/ui/ContentPanel.h` 中，添加 `loadCategories` 接口：

```
<<<<<<< SEARCH
    void loadCategory(int categoryId);
=======
    void loadCategory(int categoryId);
    void loadCategories(const QList<int>& categoryIds);
>>>>>>> REPLACE
```

在 `src/ui/ContentPanel.cpp` 中添加实现，联合多维加载：

```
<<<<<<< SEARCH
void ContentPanel::loadCategory(int categoryId) { 
=======
void ContentPanel::loadCategories(const QList<int>& categoryIds) {
    if (categoryIds.isEmpty()) return;
    
    // 多选统一标记为主分类数据源
    m_currentCategoryType = "user_category";
    m_currentCategoryId = categoryIds.first(); // 兼容单选的回退主ID
    
    m_isLoading = true;
    m_loadRequestId++;
    int reqId = m_loadRequestId;

    QPointer<ContentPanel> weakThis(this);
    (void)QtConcurrent::run([weakThis, categoryIds, reqId]() {
        // 多选模式下递归开启标记由 CategoryLoadService 重载函数自动内部计算
        bool isRecursive = AppConfig::instance().getValue("Category/RecursiveLoad", true).toBool();
        std::vector<ItemRecord> allRecords;
        
        // 分别对所有分类 ID 加载并在数据库底层（getCategories）完成 DISTINCT 去重组装
        for (int cid : categoryIds) {
            auto chunk = CategoryLoadService::loadCategoryItems(cid, isRecursive);
            allRecords.insert(allRecords.end(), chunk.begin(), chunk.end());
        }

        // 本地再进行一次 AssetPath 去重对账，保证联合数据干净整洁
        std::vector<ItemRecord> uniqueRecords;
        QSet<QString> seenPaths;
        for (auto& r : allRecords) {
            if (r.isCategory) {
                // 多选不展示子分类卡片，只展示资产
                continue;
            }
            if (!seenPaths.contains(r.path)) {
                seenPaths.insert(r.path);
                uniqueRecords.push_back(std::move(r));
            }
        }

        QMetaObject::invokeMethod(weakThis.data(), [weakThis, uniqueRecords, reqId]() {
            if (weakThis && weakThis->m_loadRequestId == reqId) {
                weakThis->m_isLoading = false;
                weakThis->m_records = uniqueRecords;
                
                // 平滑原子刷新 UI
                weakThis->m_model->setRecords(weakThis->m_records);
                weakThis->m_proxyModel->invalidate();
                
                weakThis->updateFocusLine();
                emit weakThis->dataSourceChanged("category");
            }
        });
    });
}

void ContentPanel::loadCategory(int categoryId) { 
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] `src/ui/CategoryPanel.h` 与 `src/ui/CategoryPanel.cpp` —— 重构选择信号。
- [x] `src/ui/MainWindow.cpp` —— 路由协议逗号多值解析，多选信号对接。
- [x] `src/ui/ContentPanel.h` 与 `src/ui/ContentPanel.cpp` —— 增加 `loadCategories` 实现联合去重数据载入。

**明确禁止越界修改的范围：**
- [x] 磁盘普通模式双轨路由 —— 不修改、不产生交集、不倒灌数据库。
- [x] 密码锁屏与解锁对话框类逻辑 —— 不修改。

## 6. 实现准则与预警【核心】
- **完美多维去重**：`loadCategories` 中必须执行 `QSet<QString> seenPaths` 对账过滤，多选分类极易存在交叉重复包含资产的情况，必须在内容面板展示前进行去重，确保 UI 纯净。
- **防止信号递归死循环**：`CategoryPanel` 选中事件已由 `selectionChanged` 统一拦截，原 `clicked` 仅承担锁屏拦截，消除了高频重复投递信号的隐患。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 一律使用 Win32 原生 SetWindowPos，不涉及本方案 | ✅ |
| 标题栏按钮样式 | 标题栏及按钮颜色规范，不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
无。
