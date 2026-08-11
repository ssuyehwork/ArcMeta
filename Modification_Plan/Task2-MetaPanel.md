开始单独输出 **任务 2：元数据面板（`MetaPanel`）“文件夹/分类”区域双轨重构** 的详细修改图纸。

---

# 任务 2 重构施工图纸

---

### 修改文件 1：`src/ui/MetaPanel.h`

#### 1.1 在 `public` 接口区域添加：
```cpp
/**
 * @brief 设置分类胶囊气泡列表 (托管库模式)
 * @param categories 包含 (分类ID, 分类名称) 的对列表
 */
void setCategoryPills(const std::vector<std::pair<int, QString>>& categories);

/**
 * @brief 切换显示模式（托管库模式 vs 磁盘模式）
 * @param isDiskMode true: 磁盘模式(显示原始路径文本); false: 托管库模式(显示胶囊气泡)
 * @param rawPath 磁盘模式下的物理路径
 */
void setDiskPathMode(bool isDiskMode, const QString& rawPath);
```

#### 1.2 在 `signals` 信号区域添加：
```cpp
void unbindCategoryRequested(const QString& path, int categoryId);
void bindCategoryRequested(const QString& path);
```

#### 1.3 在 `private` 成员变量区域添加：
```cpp
bool m_isDiskNavMode = false;
QWidget* m_categoryBox = nullptr;
FlowLayout* m_categoryFlowLayout = nullptr;
```

---

### 修改文件 2：`src/ui/MetaPanel.cpp`

#### 2.1 在 `initUi()` 中重构“所属分类”区域：
找到原本初始化 `m_categoryEdit` 的位置，修改为支持胶囊流式布局的容器：

```cpp
// 原有 m_categoryEdit 保留用于磁盘模式文本展示，追加胶囊容器 m_categoryBox
m_categoryEdit = new ElasticEdit(m_container);
m_categoryEdit->setReadOnly(true);
m_categoryEdit->setPlaceholderText("所属分类...");
m_categoryEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 8px; font-size: 12px; color: #EEEEEE; font-weight: normal; }");
m_containerLayout->addWidget(m_categoryEdit);

// 新增胶囊气泡容器 (托管库模式使用)
m_categoryBox = new QWidget(m_container);
QVBoxLayout* catBoxL = new QVBoxLayout(m_categoryBox);
catBoxL->setContentsMargins(0, 0, 0, 0);
catBoxL->setSpacing(4);

QWidget* catFlowWidget = new QWidget(m_categoryBox);
m_categoryFlowLayout = new FlowLayout(catFlowWidget, 0, 4, 4);
catBoxL->addWidget(catFlowWidget);

m_categoryLayoutBox = catBoxL;
m_containerLayout->addWidget(m_categoryBox);
```

#### 2.2 实现 `setDiskPathMode` 与 `setCategoryPills`：

```cpp
void MetaPanel::setDiskPathMode(bool isDiskMode, const QString& rawPath) {
    m_isDiskNavMode = isDiskMode;
    if (m_isDiskNavMode) {
        // 磁盘导航模式：隐藏胶囊容器，直接渲染纯文本物理路径
        while (QLayoutItem* item = m_categoryFlowLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        m_categoryEdit->setVisible(true);
        m_categoryEdit->setPlainText(rawPath);
        m_categoryEdit->adjustHeight();
        m_categoryBox->setVisible(false);
    } else {
        // 托管库模式：隐藏原始路径框，展示胶囊布局
        m_categoryEdit->setVisible(false);
        m_categoryBox->setVisible(true);
    }
}

void MetaPanel::setCategoryPills(const std::vector<std::pair<int, QString>>& categories) {
    if (m_isDiskNavMode) return;

    // 清空现有胶囊
    while (QLayoutItem* item = m_categoryFlowLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    // 绘制分类胶囊气泡
    for (const auto& cat : categories) {
        int catId = cat.first;
        QString catName = cat.second;

        TagPill* pill = new TagPill(catName, m_categoryBox);
        pill->setStyleSheet("background: #2D2D30; border: 1px solid #3E3E42; color: #EEE; border-radius: 4px;");
        
        // 点击胶囊上的 "×" 发射解绑信号
        connect(pill, &TagPill::deleteRequested, [this, catId]() {
            if (!m_selectedPaths.isEmpty()) {
                emit unbindCategoryRequested(m_selectedPaths.first(), catId);
            }
        });
        m_categoryFlowLayout->addWidget(pill);
    }

    // 追加 "+" 绑定分类按钮
    QPushButton* btnAddCat = new QPushButton("+", m_categoryBox);
    btnAddCat->setFixedSize(20, 20);
    btnAddCat->setCursor(Qt::PointingHandCursor);
    btnAddCat->setStyleSheet(
        "QPushButton { background: #3E3E42; color: #888; border: none; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background: #4E4E52; color: #FFF; }"
    );
    connect(btnAddCat, &QPushButton::clicked, [this]() {
        if (!m_selectedPaths.isEmpty()) {
            emit bindCategoryRequested(m_selectedPaths.first());
        }
    });
    m_categoryFlowLayout->addWidget(btnAddCat);
    m_adjustTimer->start();
}
```

---

### 修改文件 3：`src/ui/MainWindow.cpp`

#### 3.1 替换 `selectionChanged` 监听函数内部的逻辑：
定位到 `MainWindow.cpp` 中 `selectionChanged` 信号槽位置（原第 316 行附近），将原有截取物理路径的代码完全替换为：

```cpp
// 替换原有的 path.left(lastSlash) 物理路径计算代码：
bool isDiskMode = !m_contentPanel->isMirrorSource() && !MetadataManager::isInsideManagedLibrary(path.toStdWString());
m_metaPanel->setDiskPathMode(isDiskMode, path);

if (!isDiskMode) {
    // 托管库模式：拉取文件绑定的真实分类列表并转为胶囊展示
    std::string fid = MetadataManager::instance().getFolderIdSync(path.toStdWString());
    std::vector<int> catIds = CategoryRepo::getItemCategoryIds(fid, path.toStdWString());
    std::vector<std::pair<int, QString>> catPills;
    
    for (int cid : catIds) {
        Category c = CategoryRepo::getById(cid);
        if (c.id > 0) {
            catPills.push_back({c.id, QString::fromStdWString(c.name)});
        }
    }
    m_metaPanel->setCategoryPills(catPills);
}
```

#### 3.2 绑定解绑与新增分类信号：
在 `MainWindow::initUi()` 中添加：

```cpp
// 解绑分类事件响应
connect(m_metaPanel, &MetaPanel::unbindCategoryRequested, this, [this](const QString& path, int catId) {
    std::string fid = MetadataManager::instance().getFolderIdSync(path.toStdWString());
    if (!fid.empty()) {
        CategoryRepo::removeItemFromCategory(catId, fid);
        CategoryRepo::s_countsDirty.store(true);
        if (m_categoryPanel) m_categoryPanel->requestRefresh(true);
        m_contentPanel->updateItemMetadata(path);
    }
});

// 绑定分类事件响应
connect(m_metaPanel, &MetaPanel::bindCategoryRequested, this, [this](const QString& path) {
    // 触发绑定逻辑，弹出分类选择列表
});
```

---

请交付给执行者将上述图纸更新到代码中。完成修改后告诉我，我们继续推进**任务 3**。