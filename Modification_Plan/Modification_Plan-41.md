# 批量重命名双轨制架构拆分与配套缩略图及序列自愈自适应 —— Modification_Plan-41.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在当前系统的批量重命名功能中，面临以下架构与逻辑设计上的缺陷：
1. **缩略图被遗落**（对应用户原话：“批量重命名的时候，缩略图的部分也应该一起被重命名才对”）：重命名、移动或复制主资产文件时，保存在胶囊内部的配套缩略图文件（`_thumbnail.png`）未同步重命名/物理移动，导致新资产无法感知现成的缩略图，不得不慢速重复提图。
2. **序列起始数未被记忆**（对应用户原话：“而且执行重命名后的序列数字应该被记住，而不该永远是同一个数字”）：每次执行完批量重命名后，“序列数字”组件的起始数字（Start）没有自动根据当前批次的处理进度增加，而是永远停留在最初填写的数值，导致下一次批量重命名时无法连续和继承。
3. **目标选项管控及双轨强解耦**（对应用户原话：“内存模式下，进行批量重命名时，箭头指向的这两个选项应该处于禁选状态，因为内存模式下采用的是DIR 00ms73182x000.arc文件夹，所以不适宜使用这两个选项”、“将批量重命名拆分成两个独立模块，各自执行各自的逻辑，避免判断失误导致造成严重的后果”）：内存模式下，底层采用逻辑胶囊（.arc），不支持物理移动或复制到其他文件夹。为了根除复杂条件判断、多分支混叠导致的脏数据和逻辑错误隐患，必须将批量重命名拆分为内存模式（`executeMemoryMode`）与磁盘模式（`executeDiskMode`）两个完全物理隔离、高内聚的执行子模块。

## 2. 问题定位
- **对话框调用位置**：`src/ui/ContentPanel.cpp`（未传入 `isMirrorSource()` 视图数据来源状态）。
- **界面单选框状态**：`src/ui/BatchRenameDialog.cpp` 的 `initContent()`（未在内存模式下禁选“移动到其他文件夹”和“复制到其他文件夹”）。
- **重命名主操作入口**：`src/ui/BatchRenameDialog.cpp` 的 `onExecute()`（未进行双轨模式完全拆分，未同步处理 `_thumbnail.png` 配套缩略图文件，未在成功后更新“序列数字”起始值并触发 `doAutoSave` 机制）。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 批量重命名的时候，缩略图的部分也应该一起被重命名才对 | 在内存和磁盘模式重命名/移动/复制时，同步检查并执行同级配套 `_thumbnail.png` 的物理重命名/移动/复制 | ✅ 一致 |
| 2    | 而且执行重命名后的序列数字应该被记住，而不该永远是同一个数字 | 执行成功后遍历 `m_ruleRows`，更新 Sequence 类型规则的起始值（`start = start + 选定文件数 * step`），刷新 UI 并调用 `doAutoSave()` 写入 `LastBatchRenameRules` 配置中 | ✅ 一致 |
| 3    | 内存模式下，进行批量重命名时，箭头指向的这两个选项应该处于禁选状态 | 在构造函数传入 `isMirrorSource()` 状态。当在内存模式下，在 `initContent()` 中将 `m_rbMove` 和 `m_rbCopy` 按钮设为 `setEnabled(false)`，且确保 `m_rbRename` 默认并强行选中 | ✅ 一致 |
| 4    | 将批量重命名拆分成两个独立模块，各自执行各自的逻辑，避免判断失误导致造成严重的后果 | 将 `onExecute()` 拆分成 `executeMemoryMode(...)` 和 `executeDiskMode(...)` 两个独立执行流，隔离双轨判断，杜绝逻辑交织 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/ui/BatchRenameDialog.h`
更新构造函数签名，接收 `isMirrorSource` 参数，定义 `m_isMirrorSource` 成员及双轨执行方法。

```
<<<<<<< SEARCH
    explicit BatchRenameDialog(const std::vector<std::wstring>& originalPaths, QWidget* parent = nullptr);
    ~BatchRenameDialog() override = default;

    QString getFirstNewName() const { return m_firstNewName; }

private slots:
    void onAddRow();
    void updatePreview();
    void onExecute();
    void onPreview();
    void onBrowseTarget();
    void onImportPreset();
    void onExportPreset();
    void onDeleteCurrentPreset();
    void scheduleAutoSave();
    void doAutoSave();

private:
    void initContent();
    void applyTheme();

    std::vector<std::wstring> m_originalPaths;
    
    // 预设相关
=======
    explicit BatchRenameDialog(const std::vector<std::wstring>& originalPaths, bool isMirrorSource, QWidget* parent = nullptr);
    ~BatchRenameDialog() override = default;

    QString getFirstNewName() const { return m_firstNewName; }

private slots:
    void onAddRow();
    void updatePreview();
    void onExecute();
    void onPreview();
    void onBrowseTarget();
    void onImportPreset();
    void onExportPreset();
    void onDeleteCurrentPreset();
    void scheduleAutoSave();
    void doAutoSave();

private:
    void initContent();
    void applyTheme();
    void executeMemoryMode(const std::vector<RenameRule>& rules, const std::vector<std::wstring>& newNames);
    void executeDiskMode(const std::vector<RenameRule>& rules, const std::vector<std::wstring>& newNames);

    std::vector<std::wstring> m_originalPaths;
    bool m_isMirrorSource = false;
    
    // 预设相关
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/BatchRenameDialog.cpp`
1. 更新构造函数以接收并存储 `isMirrorSource` 状态。
2. 在 `initContent()` 中当为内存模式时禁用 `m_rbMove` 和 `m_rbCopy`，确保 `m_rbRename` 选中。
3. 重构并解耦拆分 `onExecute()`，实现高内聚的 `executeMemoryMode` 与 `executeDiskMode` 两个方法。

```
<<<<<<< SEARCH
BatchRenameDialog::BatchRenameDialog(const std::vector<std::wstring>& originalPaths, QWidget* parent)
    : FramelessDialog("批量重命名 - ArcMeta", parent), m_originalPaths(originalPaths) {
    resize(850, 600); // 2026-04-11 按照用户要求：给予窗口更多弹性空间，提高初始显示质量
=======
BatchRenameDialog::BatchRenameDialog(const std::vector<std::wstring>& originalPaths, bool isMirrorSource, QWidget* parent)
    : FramelessDialog("批量重命名 - ArcMeta", parent), m_originalPaths(originalPaths), m_isMirrorSource(isMirrorSource) {
    resize(850, 600); // 2026-04-11 按照用户要求：给予窗口更多弹性空间，提高初始显示质量
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
    m_rbRename = new QRadioButton("在同一文件夹中重命名", targetGroup);
    m_rbMove = new QRadioButton("移动到其他文件夹", targetGroup);
    m_rbCopy = new QRadioButton("复制到其他文件夹", targetGroup);
    m_rbRename->setChecked(true);
    targetL->addWidget(m_rbRename);
    targetL->addWidget(m_rbMove);
    targetL->addWidget(m_rbCopy);
=======
    m_rbRename = new QRadioButton("在同一文件夹中重命名", targetGroup);
    m_rbMove = new QRadioButton("移动到其他文件夹", targetGroup);
    m_rbCopy = new QRadioButton("复制到其他文件夹", targetGroup);
    m_rbRename->setChecked(true);
    targetL->addWidget(m_rbRename);
    targetL->addWidget(m_rbMove);
    targetL->addWidget(m_rbCopy);

    if (m_isMirrorSource) {
        m_rbMove->setEnabled(false);
        m_rbCopy->setEnabled(false);
        m_rbRename->setChecked(true);
    }
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
void BatchRenameDialog::onExecute() {
    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) rules.push_back(row->getRule());
    
    auto newNames = BatchRenameEngine::instance().preview(m_originalPaths, rules);
    QString targetDir = m_targetPathEdit->text();
    
    if ((m_rbMove->isChecked() || m_rbCopy->isChecked()) && targetDir.isEmpty()) {
        FramelessMessageBox::warning(this, "错误", "请先选择目标文件夹");
        return;
    }

    int successCount = 0;

    // 🚨 开启防抖与内部操作锁定，防止高密集 Windows IOCP 更名重命名变动反馈产生严重的系统刷新和竞态！
    MetadataManager::instance().beginInternalOperation();

    for (int i = 0; i < (int)m_originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(m_originalPaths[i]);
        QFileInfo oldInfo(oldPath);
        QString finalTargetDir = m_rbRename->isChecked() ? oldInfo.absolutePath() : targetDir;
        QString newPathStr = QDir(finalTargetDir).filePath(QString::fromStdWString(newNames[i]));

        bool ok = false;
        if (m_rbCopy->isChecked()) {
            ok = QFile::copy(oldPath, newPathStr);
        } else if (m_rbMove->isChecked()) {
            if (QFile::copy(oldPath, newPathStr)) {
                ok = QFile::remove(oldPath);
            }
        } else {
            ok = QFile::rename(oldPath, newPathStr);
        }

        if (ok) {
            successCount++;
            if (!m_rbCopy->isChecked()) {
                std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
                std::wstring newW = QDir(finalTargetDir).absoluteFilePath(QString::fromStdWString(newNames[i])).toStdWString();
                
                // 1. 同步进行元数据迁移
                MetadataManager::instance().renameItem(oldW, newW);
                
                // 2. 双轨制同步：同步修正 categories 表分类定义与 category_items 表中的 pathHint 引用，保持 1:1 分类镜像不损坏！
                CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
            }
        }
    }

    if (successCount > 0 && !newNames.empty()) {
        m_firstNewName = QString::fromStdWString(newNames.front());
    }

    // 🚨 关闭内部操作锁定并提交
    MetadataManager::instance().endInternalOperation();

    // 发射全量 UI 刷新信号，使侧边栏分类树、内容视图同频重新计数 and 对账
    MetadataManager::instance().notifyFullUIRebuild();

    FramelessMessageBox::information(this, "操作完成", QString("成功处理 %1 个文件").arg(successCount));
    accept();
}
=======
void BatchRenameDialog::onExecute() {
    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) rules.push_back(row->getRule());
    
    auto newNames = BatchRenameEngine::instance().preview(m_originalPaths, rules);

    if (m_isMirrorSource) {
        executeMemoryMode(rules, newNames);
    } else {
        executeDiskMode(rules, newNames);
    }
}

void BatchRenameDialog::executeMemoryMode(const std::vector<RenameRule>& rules, const std::vector<std::wstring>& newNames) {
    int successCount = 0;

    // 🚨 开启防抖与内部操作锁定
    MetadataManager::instance().beginInternalOperation();

    for (int i = 0; i < (int)m_originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(m_originalPaths[i]);
        QFileInfo oldInfo(oldPath);
        QString finalTargetDir = oldInfo.absolutePath();
        QString newPathStr = QDir(finalTargetDir).filePath(QString::fromStdWString(newNames[i]));

        if (QFile::rename(oldPath, newPathStr)) {
            successCount++;

            // 同步对配套 _thumbnail.png 缩略图进行物理重命名
            QString oldThumbPath = oldInfo.absolutePath() + "/" + oldInfo.completeBaseName() + "_thumbnail.png";
            if (QFile::exists(oldThumbPath)) {
                QString newThumbPath = QFileInfo(newPathStr).absolutePath() + "/" + QFileInfo(newPathStr).completeBaseName() + "_thumbnail.png";
                QFile::rename(oldThumbPath, newThumbPath);
            }

            std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
            std::wstring newW = QDir(finalTargetDir).absoluteFilePath(QString::fromStdWString(newNames[i])).toStdWString();
            
            // 1. 内存模型下的元数据索引及路径迁移
            MetadataManager::instance().renameItem(oldW, newW);
            
            // 2. 双轨制同步：更新分类关系与 pathHint 映射，确保侧边栏分类计数精确无误
            CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
        }
    }

    if (successCount > 0 && !newNames.empty()) {
        m_firstNewName = QString::fromStdWString(newNames.front());
    }

    // 记住并自动持久化更新后的序列起始值
    for (auto* row : m_ruleRows) {
        RenameRule rule = row->getRule();
        if (rule.type == RenameComponentType::Sequence) {
            rule.start = rule.start + (int)m_originalPaths.size() * rule.step;
            row->setRule(rule);
        }
    }
    doAutoSave();

    // 🚨 关闭内部操作锁定并提交
    MetadataManager::instance().endInternalOperation();

    // 发射全量 UI 刷新信号
    MetadataManager::instance().notifyFullUIRebuild();

    FramelessMessageBox::information(this, "操作完成", QString("成功处理 %1 个文件").arg(successCount));
    accept();
}

void BatchRenameDialog::executeDiskMode(const std::vector<RenameRule>& rules, const std::vector<std::wstring>& newNames) {
    QString targetDir = m_targetPathEdit->text();
    if ((m_rbMove->isChecked() || m_rbCopy->isChecked()) && targetDir.isEmpty()) {
        FramelessMessageBox::warning(this, "错误", "请先选择目标文件夹");
        return;
    }

    int successCount = 0;

    // 🚨 开启防抖与内部操作锁定
    MetadataManager::instance().beginInternalOperation();

    for (int i = 0; i < (int)m_originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(m_originalPaths[i]);
        QFileInfo oldInfo(oldPath);
        QString finalTargetDir = m_rbRename->isChecked() ? oldInfo.absolutePath() : targetDir;
        QString newPathStr = QDir(finalTargetDir).filePath(QString::fromStdWString(newNames[i]));

        bool ok = false;
        if (m_rbCopy->isChecked()) {
            ok = QFile::copy(oldPath, newPathStr);
            if (ok) {
                QString oldThumbPath = oldInfo.absolutePath() + "/" + oldInfo.completeBaseName() + "_thumbnail.png";
                if (QFile::exists(oldThumbPath)) {
                    QString newThumbPath = QFileInfo(newPathStr).absolutePath() + "/" + QFileInfo(newPathStr).completeBaseName() + "_thumbnail.png";
                    QFile::copy(oldThumbPath, newThumbPath);
                }
            }
        } else if (m_rbMove->isChecked()) {
            if (QFile::copy(oldPath, newPathStr)) {
                ok = QFile::remove(oldPath);
                if (ok) {
                    QString oldThumbPath = oldInfo.absolutePath() + "/" + oldInfo.completeBaseName() + "_thumbnail.png";
                    if (QFile::exists(oldThumbPath)) {
                        QString newThumbPath = QFileInfo(newPathStr).absolutePath() + "/" + QFileInfo(newPathStr).completeBaseName() + "_thumbnail.png";
                        if (QFile::copy(oldThumbPath, newThumbPath)) {
                            QFile::remove(oldThumbPath);
                        }
                    }
                }
            }
        } else {
            ok = QFile::rename(oldPath, newPathStr);
            if (ok) {
                QString oldThumbPath = oldInfo.absolutePath() + "/" + oldInfo.completeBaseName() + "_thumbnail.png";
                if (QFile::exists(oldThumbPath)) {
                    QString newThumbPath = QFileInfo(newPathStr).absolutePath() + "/" + QFileInfo(newPathStr).completeBaseName() + "_thumbnail.png";
                    QFile::rename(oldThumbPath, newThumbPath);
                }
            }
        }

        if (ok) {
            successCount++;
            if (!m_rbCopy->isChecked()) {
                std::wstring oldW = oldInfo.absoluteFilePath().toStdWString();
                std::wstring newW = QDir(finalTargetDir).absoluteFilePath(QString::fromStdWString(newNames[i])).toStdWString();
                
                // 同步进行磁盘离散元数据、哈希 JSON 的重命名与平滑迁移
                MetadataManager::instance().renameItem(oldW, newW);
                CategoryRepo::renamePhysicalCategoryPath(oldW, newW);
            }
        }
    }

    if (successCount > 0 && !newNames.empty()) {
        m_firstNewName = QString::fromStdWString(newNames.front());
    }

    // 记住并自动持久化更新后的序列起始值
    for (auto* row : m_ruleRows) {
        RenameRule rule = row->getRule();
        if (rule.type == RenameComponentType::Sequence) {
            rule.start = rule.start + (int)m_originalPaths.size() * rule.step;
            row->setRule(rule);
        }
    }
    doAutoSave();

    // 🚨 关闭内部操作锁定并提交
    MetadataManager::instance().endInternalOperation();

    // 发射全量 UI 刷新信号
    MetadataManager::instance().notifyFullUIRebuild();

    FramelessMessageBox::information(this, "操作完成", QString("成功处理 %1 个文件").arg(successCount));
    accept();
}
>>>>>>> REPLACE
```

### 4.3 修改 `src/ui/ContentPanel.cpp`
在实例化 `BatchRenameDialog` 时，将当前 `isMirrorSource()` 结果传入作为模式参数。

```
<<<<<<< SEARCH
    BatchRenameDialog dlg(originalPaths, this); 
    if (dlg.exec() == QDialog::Accepted) { 
=======
    BatchRenameDialog dlg(originalPaths, isMirrorSource(), this); 
    if (dlg.exec() == QDialog::Accepted) { 
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/BatchRenameDialog.h`（修改构造函数入参，新增成员变量与方法声明）
- [ ] 模块/文件：`src/ui/BatchRenameDialog.cpp`（实现双轨解耦执行，支持序列数字累加自愈以及配套 `_thumbnail.png` 重命名/复制/移动）
- [ ] 模块/文件：`src/ui/ContentPanel.cpp`（传递 `isMirrorSource()`）

**明确禁止越界修改的范围：**
- [ ] 侧边栏及元数据面板核心计数与列表刷新逻辑 —— 不修改

## 6. 实现准则与预警【核心】
1. **防抖与竞态保护**：批量操作必须包裹在 `beginInternalOperation()` / `endInternalOperation()` 中，防止多文件重命名产生大量多余的异步 IOCP 变化通知造成 UI 闪烁及线程爆满。
2. **辅件同频操作**：在对 `_thumbnail.png` 进行复制和移动时，若遇到同名旧缩略图，应确保原图操作成功后才进行擦除和覆盖。
3. **序列自愈保存**：修改 SpinBox 值时必须调用 `setRule()` 使得组件状态与界面同频，并确确实实通过 `doAutoSave()` 将其保存入配置 JSON。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|--------------------------------------------|----------------|
| 内容面板数据源判定与强类型契约规范 | ContentPanel 必须公开 isMirrorSource() (返回是否为逻辑/镜像源数据) 与 isManagedContext() (返回当前是否处于已激活的托管库内可读写 SQLite DB 的可信生命周期内)。 | ✅ 符合（完全复用 `isMirrorSource()`） |

## 8. 待确认事项
无。
