# 内容面板新建分类重构 —— contentpanel-create-subcategory-refactor.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在资源库内存托管库模式（UserCategory 模式）下，当用户在内容面板中执行“新建文件夹”时，由于底层运行逻辑需要物理隔离且实现职责单一，该动作不应直接在磁盘上建立空目录。
正确的交互逻辑是：自动在当前选中分类下，创建指向该父分类的“逻辑子分类”节点。

为了提供无缝连接的流程体验，内容面板需要对外广播信号，通知主窗口调度侧边栏，自动展开、自动在指定父分类下追加子分类、并立即激活 QTreeView 行内编辑重命名状态。

## 2. 问题定位
- **模块 1：** `src/ui/ContentPanel.h`
  - **操作：** 新增 `requestCreateSubCategory(int parentCategoryId)` 信号，解耦跨域操作。
- **模块 2：** `src/ui/ContentPanel.cpp`
  - **位置：** `ContentPanel::createNewItem(const QString& type)`
  - **原因：** 需要将原有逻辑重构，分流为“物理磁盘导航模式 (DiskNav)”与“内存受控托管库模式 (UserCategory)”。后者在 `type == "folder"` 时改为直接发射上述信号。
- **模块 3：** `src/ui/MainWindow.cpp`
  - **位置：** `initUi()` 信号槽初始化连接处
  - **原因：** 需要物理打通内容面板与侧边栏 `CategoryPanel` 的跨面板操作桥梁。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：内存受控模式新建文件夹转化为创建逻辑子分类信号联动 | 本方案核心事件名：内容面板新建分类重构 | ✅ |
| 1    | 在内存模式下，请求在指定分类下创建逻辑子分类（对应用户原话：“在内存模式下，请求在指定分类下创建逻辑子分类”） | 在 `ContentPanel.h` 声明 `void requestCreateSubCategory(int parentCategoryId);` | ✅ |
| 2    | 场景 B1：新建文件夹 ➔ 生成逻辑子分类（对应用户原话：“场景 B1：新建文件夹 ➔ 生成逻辑子分类”） | 托管模式下若类型为 `"folder"` 则发射信号：`emit requestCreateSubCategory(m_currentCategoryId);` | ✅ |
| 3    | 场景 B2：新建 Markdown / txt ➔ 生成受控物理资产胶囊文件（对应用户原话：“场景 B2：新建 Markdown / txt ➔ 生成受控物理资产胶囊文件”） | 保留并完善胶囊资产的创建与 SQLite 登记写入注册流程。 | ✅ |
| 4    | 绑定 `requestCreateSubCategory` 信号到侧边栏 `CategoryPanel` 的创建方法（对应用户原话：“绑定 `requestCreateSubCategory` 信号到侧边栏 `CategoryPanel` 的创建方法”） | 并在 MainWindow 驱动展开和行内编辑方法 `onCreateSubCategory()`。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/ContentPanel.h`
增加信号声明。

```
<<<<<<< SEARCH
signals:
    /**
     * @brief 当在内容区点击子分类时触发，告知 MainWindow 切换侧边栏选中状态
     */
    void categoryClicked(int categoryId);
=======
signals:
    /**
     * @brief 在内存模式下，请求在指定分类下创建逻辑子分类（对应用户原话：“在内存模式下，请求在指定分类下创建逻辑子分类”）
     */
    void requestCreateSubCategory(int parentCategoryId);

signals:
    /**
     * @brief 当在内容区点击子分类时触发，告知 MainWindow 切换侧边栏选中状态
     */
    void categoryClicked(int categoryId);
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/ContentPanel.cpp`
重构 `ContentPanel::createNewItem` 逻辑，在受控托管模式下分流新建文件夹。

```
<<<<<<< SEARCH
void ContentPanel::createNewItem(const QString& type) {
    if (m_currentPath.isEmpty()) return;

    QString baseName = (type == "folder") ? "新建文件夹" : "未命名";
    QString ext = (type == "md") ? ".md" : ((type == "txt") ? ".txt" : "");
    QString finalName = baseName + ext;
    QString fullPath = m_currentPath + "/" + finalName;

    int counter = 1;
    while (QFileInfo::exists(fullPath)) {
        finalName = baseName + QString(" (%1)").arg(counter++) + ext;
        fullPath = m_currentPath + "/" + finalName;
    }

    bool success = false;
    if (type == "folder") {
        success = QDir(m_currentPath).mkdir(finalName);
    } else {
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
            success = true;
        }
    }

    if (success) {
        m_pendingSelectName = finalName;
        m_isPendingEdit = true;
        loadDirectory(m_currentPath, m_isRecursive);
    }
}
=======
void ContentPanel::createNewItem(const QString& type) { 
    // --- 分流 A：物理磁盘导航模式 (DiskNav) ---
    if (dataSourceType() == DataSourceType::DiskNav) {
        if (m_currentPath.isEmpty() || m_currentPath == "computer://") return; 

        QString baseName = (type == "folder") ? "新建文件夹" : "未命名"; 
        QString ext = (type == "md") ? ".md" : ((type == "txt") ? ".txt" : ""); 
        QString finalName = baseName + ext; 
        QString fullPath = m_currentPath + "/" + finalName; 

        int counter = 1; 
        while (QFileInfo::exists(fullPath)) { 
            finalName = baseName + QString(" (%1)").arg(counter++) + ext; 
            fullPath = m_currentPath + "/" + finalName; 
        } 

        bool success = false; 
        if (type == "folder") { 
            success = QDir(m_currentPath).mkdir(finalName); 
        } else { 
            QFile file(fullPath); 
            if (file.open(QIODevice::WriteOnly)) { 
                file.close(); 
                success = true; 
            } 
        } 

        if (success) { 
            m_pendingSelectName = finalName;
            m_isPendingEdit = true;
            loadDirectory(m_currentPath, m_isRecursive); 
        }
        return;
    }

    // --- 分流 B：内存受控托管库模式 (UserCategory) ---
    if (m_currentCategoryId <= 0) return;

    // 场景 B1：新建文件夹 ➔ 生成逻辑子分类（对应用户原话：“场景 B1：新建文件夹 ➔ 生成逻辑子分类”）
    if (type == "folder") {
        // 向侧边栏发射请求，由 CategoryPanel 自动展开并直接进入行内编辑重命名
        emit requestCreateSubCategory(m_currentCategoryId);
        return;
    }

    // 场景 B2：新建 Markdown / txt ➔ 生成受控物理资产胶囊文件（对应用户原话：“场景 B2：新建 Markdown / txt ➔ 生成受控物理资产胶囊文件”）
    QString baseName = "未命名";
    QString ext = (type == "md") ? ".md" : ".txt";
    QString fileName = baseName + ext;

    // 1. 获取当前盘符托管库根目录 (例如 G:\ArcMeta.Library_G)
    QString drive = QCoreApplication::applicationDirPath().left(3);
    if (!m_currentPath.isEmpty() && m_currentPath.length() >= 3 && m_currentPath[1] == ':') {
        drive = m_currentPath.left(3);
    }
    QString managedRoot = drive + "ArcMeta.Library_" + drive.at(0).toUpper();
    if (!QDir().exists(managedRoot)) {
        QDir().mkpath(managedRoot);
    }

    // 2. 分配 13 位 Base36 胶囊 ID 并物理创建文件
    QString fileId = ShellHelper::generateBase36Id();
    QString containerDir = managedRoot + "/" + fileId + ".arc";
    if (!QDir().mkpath(containerDir)) return;

    QString destPath = containerDir + "/" + fileName;
    QFile file(destPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QDir(containerDir).removeRecursively();
        return;
    }
    file.close();

    // 3. 登记写入 SQLite 数据库并绑定至当前分类 ID
    std::wstring wDestPath = QDir::toNativeSeparators(destPath).toStdWString();
    if (MetadataManager::instance().registerAsset(fileId.toStdString(), wDestPath, m_currentCategoryId)) {
        // 4. 定位高亮并进入行内编辑状态
        m_pendingSelectName = fileName;
        m_isPendingEdit = true;
        refreshAll();
    }
}
>>>>>>> REPLACE
```

### 4.3 修改 `src/ui/MainWindow.cpp`
绑定信号槽连接。

```
<<<<<<< SEARCH
    // 面板四：内容面板
    connect(m_contentPanel, &ContentPanel::categoryClicked, this, [this](int categoryId) {
        if (m_categoryPanel) {
            m_categoryPanel->selectCategory(categoryId);
        }
    });
=======
    // 面板四：内容面板
    connect(m_contentPanel, &ContentPanel::categoryClicked, this, [this](int categoryId) {
        if (m_categoryPanel) {
            m_categoryPanel->selectCategory(categoryId);
        }
    });

    // 绑定 requestCreateSubCategory 信号到侧边栏 CategoryPanel 的创建方法（对应用户原话：“绑定 requestCreateSubCategory 信号到侧边栏 CategoryPanel 的创建方法”）
    connect(m_contentPanel, &ContentPanel::requestCreateSubCategory, this, [this](int parentCatId) {
        if (m_categoryPanel) {
            m_categoryPanel->selectCategory(parentCatId);
            m_categoryPanel->onCreateSubCategory();
        }
    });
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] 模块/文件：`src/ui/ContentPanel.h` 增加跨面板通信信号。
- [x] 模块/文件：`src/ui/ContentPanel.cpp` 中的 `createNewItem()` 核心方法。
- [x] 模块/文件：`src/ui/MainWindow.cpp` 面板信号槽互连。

**明确禁止越界修改的范围：**
- [x] 侧边栏（`CategoryPanel`）自身的创建与展开等原始基础业务函数——不修改。
- [x] `createNewItem()` 在 DiskNav 模式下的运作机制——不修改。

## 6. 实现准则与预警【核心】
- **一键行内编辑跨面板安全性**：由于创建过程由 CategoryPanel 接管，主窗口在调用 `m_categoryPanel->onCreateSubCategory()` 之前必须优先定位激活 parent 文件夹（通过 `selectCategory(parentCatId)`），确保树节点在创建时的焦点与层级完美自愈。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 使用 Win32 原生 SetWindowPos，不涉及本方案 | ✅ |
| 标题栏按钮样式 | 标题栏及按钮颜色规范，不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
暂无。
