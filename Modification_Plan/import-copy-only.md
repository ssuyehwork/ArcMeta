# 受控库导入一律安全复制化 —— import-copy-only.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在现行的导入机制（`AssetImporter`）中，当源文件和资源托管库处于同一硬盘分区时，系统会隐式自动执行重命名（`QFile::rename`），从而在同盘拖拽或粘贴导入时将用户的物理原文件剪切搬走。这违反了“用户主动导入必须保留其物理源资产”的产品准则，极易造成用户在不知情下丢失原文件。

本方案旨在重构 `AssetImporter` 类体系及跨库资产迁移，将其默认重构为全自动的安全复制。

## 2. 问题定位
- **模块一：** `src/util/AssetImporter.h` / `src/util/AssetImporter.cpp`
  - `importAssets`（两参数重载）、`importSingleFile` 及 `importDirectoryRecursive` 默认均缺乏显式逻辑标志，隐式采用同盘剪切、异盘复制判断。
  - 需要在所有相关导入方法中，全部增加显式控制参数 `bool allowMove = false`，默认值强制设为 `false`，确保 100% 默认复制。
- **模块二：** `src/meta/MetadataManager.cpp` —— `migrateCapsuleToLibrary`
  - 第 591 行在将胶囊移动到目标资源库时：`ShellHelper::copyOrMoveItems({containerDir.absolutePath()}, targetLibraryPath, true)`，其中第三参数 `isMove` 硬编码为 `true`。
  - 需要修改为 `false`，将其改为复制逻辑。
- **模块三：** 关联调用者。
  - 属于自动导入场景的 `src/core/CoreController.cpp`（监控目录迁移）和 `src/ui/MainWindow.cpp`（`showNewAutoImportDialog` 首次建立监控）应当传入 `allowMove = true`。
  - 其他地方（包括拖拽、粘贴导入）默认不传值或显式传入 `false`，走默认的安全复制。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 0    | Step 1 中确认的"核心问题"：同盘一律复制，仅自动导入允许剪切 | 本方案核心事件名：受控库导入一律安全复制化 | ✅ |
| 1    | 明白，不再纠结历史注释，一律执行"无论同盘异盘一律复制"，含 `migrateCapsuleToLibrary` 这条库内跨库迁移路径。（对应用户原话：“一律执行'无论同盘异盘一律复制'，含 `migrateCapsuleToLibrary` 这条库内跨库迁移路径。”） | 修改 `AssetImporter` 参数和 `migrateCapsuleToLibrary` 的第三参数为复制模式 | ✅ |
| 2    | 这个函数改成复制后，旧库里的 `.arc` 胶囊物理文件夹会原样留在原处……请明确要不要接受孤儿文件夹这个后果。 | 库内跨库迁移从剪切改为复制，在方案第 4 节中对此副作用进行说明，保留旧物理胶囊。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 修改 `src/util/AssetImporter.h`
新增参数 `bool allowMove = false` 并对齐接口：

```
<<<<<<< SEARCH
    static void importAssets(const QStringList& paths,
                             int targetCatId,
                             QWidget* parent = nullptr,
                             std::function<void()> onComplete = nullptr);

    static void importAssets(const QStringList& paths,
                             int targetCatId,
                             QWidget* parent,
                             std::function<void(const QStringList& newlyImportedPaths)> onComplete);

private:
    static bool importSingleFile(const QString& srcPath,
                                 int targetCatId,
                                 const QString& managedRoot,
                                 QStringList* newlyImportedPaths = nullptr);

    static bool importDirectoryRecursive(const QString& srcDir,
                                         int parentCatId,
                                         const QString& managedRoot,
                                         QStringList* newlyImportedPaths = nullptr);
=======
    static void importAssets(const QStringList& paths,
                             int targetCatId,
                             QWidget* parent = nullptr,
                             std::function<void()> onComplete = nullptr,
                             bool allowMove = false);

    static void importAssets(const QStringList& paths,
                             int targetCatId,
                             QWidget* parent,
                             std::function<void(const QStringList& newlyImportedPaths)> onComplete,
                             bool allowMove = false);

private:
    static bool importSingleFile(const QString& srcPath,
                                 int targetCatId,
                                 const QString& managedRoot,
                                 QStringList* newlyImportedPaths = nullptr,
                                 bool allowMove = false);

    static bool importDirectoryRecursive(const QString& srcDir,
                                         int parentCatId,
                                         const QString& managedRoot,
                                         QStringList* newlyImportedPaths = nullptr,
                                         bool allowMove = false);
>>>>>>> REPLACE
```

### 4.2 修改 `src/util/AssetImporter.cpp`
在具体搬运节点 `importSingleFile` 及 `importDirectoryRecursive` 处对 `allowMove` 进行严格的同盘逻辑控制：

```
<<<<<<< SEARCH
void AssetImporter::importAssets(const QStringList& paths, 
                                 int targetCatId, 
                                 QWidget* parent, 
                                 std::function<void()> onComplete) { 
    importAssets(paths, targetCatId, parent, [onComplete](const QStringList& newlyImportedPaths) {
        Q_UNUSED(newlyImportedPaths);
        if (onComplete) onComplete();
    });
}

void AssetImporter::importAssets(const QStringList& paths, 
                                 int targetCatId, 
                                 QWidget* parent, 
                                 std::function<void(const QStringList& newlyImportedPaths)> onComplete) { 
=======
void AssetImporter::importAssets(const QStringList& paths, 
                                 int targetCatId, 
                                 QWidget* parent, 
                                 std::function<void()> onComplete,
                                 bool allowMove) { 
    importAssets(paths, targetCatId, parent, [onComplete](const QStringList& newlyImportedPaths) {
        Q_UNUSED(newlyImportedPaths);
        if (onComplete) onComplete();
    }, allowMove);
}

void AssetImporter::importAssets(const QStringList& paths, 
                                 int targetCatId, 
                                 QWidget* parent, 
                                 std::function<void(const QStringList& newlyImportedPaths)> onComplete,
                                 bool allowMove) { 
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
            QFileInfo srcInfo(src); 
            bool ok = false; 
            if (srcInfo.isFile()) { 
                ok = importSingleFile(src, targetCatId, managedRoot, &newlyImportedPaths); 
            } else if (srcInfo.isDir()) { 
                ok = importDirectoryRecursive(src, targetCatId, managedRoot, &newlyImportedPaths); 
            } 
=======
            QFileInfo srcInfo(src); 
            bool ok = false; 
            if (srcInfo.isFile()) { 
                ok = importSingleFile(src, targetCatId, managedRoot, &newlyImportedPaths, allowMove); 
            } else if (srcInfo.isDir()) { 
                ok = importDirectoryRecursive(src, targetCatId, managedRoot, &newlyImportedPaths, allowMove); 
            } 
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
bool AssetImporter::importSingleFile(const QString& srcPath, 
                                     int targetCatId, 
                                     const QString& managedRoot,
                                     QStringList* newlyImportedPaths) { 
    QFileInfo srcInfo(srcPath); 
    if (!srcInfo.exists() || !srcInfo.isFile()) return false; 
 
    // 1. 生成 13 位唯一 Base36 胶囊 ID 
    QString fileId = ShellHelper::generateBase36Id(); 
 
    // 2. 建立物理容器 [ID].arc 
    QString containerDir = managedRoot + "/" + fileId + ".arc"; 
    if (!QDir().mkpath(containerDir)) return false; 
 
    // 3. 将真实资产放入物理容器中 
    QString fileName = srcInfo.fileName(); 
    QString destPath = containerDir + "/" + fileName; 
 
    QString srcDrive = QFileInfo(srcPath).absolutePath().left(3); 
    QString destDrive = QFileInfo(destPath).absolutePath().left(3); 
 
    bool copied = false; 
    if (srcDrive.compare(destDrive, Qt::CaseInsensitive) == 0) { 
        copied = QFile::rename(srcPath, destPath); 
    } else { 
        copied = QFile::copy(srcPath, destPath); 
    } 
=======
bool AssetImporter::importSingleFile(const QString& srcPath, 
                                     int targetCatId, 
                                     const QString& managedRoot,
                                     QStringList* newlyImportedPaths,
                                     bool allowMove) { 
    QFileInfo srcInfo(srcPath); 
    if (!srcInfo.exists() || !srcInfo.isFile()) return false; 
 
    // 1. 生成 13 位唯一 Base36 胶囊 ID 
    QString fileId = ShellHelper::generateBase36Id(); 
 
    // 2. 建立物理容器 [ID].arc 
    QString containerDir = managedRoot + "/" + fileId + ".arc"; 
    if (!QDir().mkpath(containerDir)) return false; 
 
    // 3. 将真实资产放入物理容器中 
    QString fileName = srcInfo.fileName(); 
    QString destPath = containerDir + "/" + fileName; 
 
    QString srcDrive = QFileInfo(srcPath).absolutePath().left(3); 
    QString destDrive = QFileInfo(destPath).absolutePath().left(3); 
 
    bool copied = false; 
    // 同盘一律复制，仅限自动导入允许剪切（对应用户原话：“一律执行'无论同盘异盘一律复制'”）
    if (allowMove && srcDrive.compare(destDrive, Qt::CaseInsensitive) == 0) { 
        copied = QFile::rename(srcPath, destPath); 
    } else { 
        copied = QFile::copy(srcPath, destPath); 
    } 
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
bool AssetImporter::importDirectoryRecursive(const QString& srcDir, 
                                             int parentCatId, 
                                             const QString& managedRoot,
                                             QStringList* newlyImportedPaths) { 
    QFileInfo dirInfo(srcDir); 
    if (!dirInfo.exists() || !dirInfo.isDir()) return false; 
 
    if (dirInfo.fileName().endsWith(".arc", Qt::CaseInsensitive)) { 
        return false; // 跳过物理容器本身 
    } 
 
    Category cat; 
    // 🚨 安全下限防护：若 parentCatId < 0（如 -2），自动修正为 0（顶级分类），防止生成幽灵隐形分类
    cat.parentId = (parentCatId < 0) ? 0 : parentCatId; 
    cat.name = dirInfo.fileName().toStdWString(); 
    cat.color = CategoryRepo::getDefaultColor(); 
    if (!CategoryRepo::add(cat)) return false; 
 
    QDir dir(srcDir); 
    QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
    for (const QFileInfo& entry : entries) { 
        if (entry.isFile()) { 
            importSingleFile(entry.absoluteFilePath(), cat.id, managedRoot, newlyImportedPaths); 
        } else if (entry.isDir()) { 
            importDirectoryRecursive(entry.absoluteFilePath(), cat.id, managedRoot, newlyImportedPaths); 
        } 
    } 
=======
bool AssetImporter::importDirectoryRecursive(const QString& srcDir, 
                                             int parentCatId, 
                                             const QString& managedRoot,
                                             QStringList* newlyImportedPaths,
                                             bool allowMove) { 
    QFileInfo dirInfo(srcDir); 
    if (!dirInfo.exists() || !dirInfo.isDir()) return false; 
 
    if (dirInfo.fileName().endsWith(".arc", Qt::CaseInsensitive)) { 
        return false; // 跳过物理容器本身 
    } 
 
    Category cat; 
    // 🚨 安全下限防护：若 parentCatId < 0（如 -2），自动修正为 0（顶级分类），防止生成幽灵隐形分类
    cat.parentId = (parentCatId < 0) ? 0 : parentCatId; 
    cat.name = dirInfo.fileName().toStdWString(); 
    cat.color = CategoryRepo::getDefaultColor(); 
    if (!CategoryRepo::add(cat)) return false; 
 
    QDir dir(srcDir); 
    QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
    for (const QFileInfo& entry : entries) { 
        if (entry.isFile()) { 
            importSingleFile(entry.absoluteFilePath(), cat.id, managedRoot, newlyImportedPaths, allowMove); 
        } else if (entry.isDir()) { 
            importDirectoryRecursive(entry.absoluteFilePath(), cat.id, managedRoot, newlyImportedPaths, allowMove); 
        } 
    } 
>>>>>>> REPLACE
```

### 4.3 修改 `src/meta/MetadataManager.cpp` 中 `migrateCapsuleToLibrary`
将 `ShellHelper::copyOrMoveItems` 迁移中第三个参数 `isMove` 改为 `false`（复制）：

```
<<<<<<< SEARCH
    // 1. 物理跨盘剪切整个 .arc 胶囊文件夹 
    if (!ShellHelper::copyOrMoveItems({containerDir.absolutePath()}, targetLibraryPath, true)) { 
        return false; 
    } 
=======
    // 1. 物理跨盘复制整个 .arc 胶囊文件夹（同盘异盘一律复制，含库内跨库迁移路径。对应用户原话：“无论同盘异盘一律复制，含 migrateCapsuleToLibrary 这条库内跨库迁移路径”）
    if (!ShellHelper::copyOrMoveItems({containerDir.absolutePath()}, targetLibraryPath, false)) { 
        return false; 
    } 
>>>>>>> REPLACE
```

### 4.4 修改关联自动监控调用方（传参 `allowMove = true`）

1. 修改 `src/core/CoreController.cpp`
查找并替换自动迁移监控资产处，传入 `allowMove = true`：

```
<<<<<<< SEARCH
                        // 直接调用资产打包导入器进行剪切迁移入库 (targetCatId = 0)，后台静默进行
                        AssetImporter::importAssets(QStringList() << topLevelPath, 0, nullptr, [wTopLevelPath]() {
                            s_currentlyMigrating.erase(wTopLevelPath);
                            // 迁移完成后，自动调用 MetadataManager::instance().notifyFullUIRebuild() 进行自愈式刷新
                            MetadataManager::instance().notifyFullUIRebuild();
                        });
=======
                        // 直接调用资产打包导入器进行剪切迁移入库 (targetCatId = 0)，后台静默进行。自动监控目录迁移允许剪切
                        AssetImporter::importAssets(QStringList() << topLevelPath, 0, nullptr, [wTopLevelPath]() {
                            s_currentlyMigrating.erase(wTopLevelPath);
                            // 迁移完成后，自动调用 MetadataManager::instance().notifyFullUIRebuild() 进行自愈式刷新
                            MetadataManager::instance().notifyFullUIRebuild();
                        }, true);
>>>>>>> REPLACE
```

2. 修改 `src/ui/MainWindow.cpp`
在自定义文件夹首次同步时，传入 `allowMove = true`：

```
<<<<<<< SEARCH
                // 统一调用 AssetImporter::importAssets 搬运，targetCatId = 0，不弹窗问询
                AssetImporter::importAssets(pathsToImport, 0, nullptr, [this]() {
                    MetadataManager::instance().notifyFullUIRebuild();
                });
=======
                // 统一调用 AssetImporter::importAssets 搬运，targetCatId = 0，不弹窗问询。自动首次监控同步允许剪切
                AssetImporter::importAssets(pathsToImport, 0, nullptr, [this]() {
                    MetadataManager::instance().notifyFullUIRebuild();
                }, true);
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [x] `src/util/AssetImporter.h` / `src/util/AssetImporter.cpp` —— 重构显式 `allowMove` 搬运隔离。
- [x] `src/meta/MetadataManager.cpp` —— `migrateCapsuleToLibrary` 改剪切为复制，保留旧库磁盘物理胶囊文件夹。
- [x] `src/core/CoreController.cpp` —— 自动导入监控传入 `allowMove = true`。
- [x] `src/ui/MainWindow.cpp` —— 自定义监控首次对账导入传入 `allowMove = true`。

**明确禁止越界修改的范围：**
- [x] 主动剪切粘贴、拖拽等用户主动导入链路 —— 保持默认值 `false`（复制），不修改。

## 6. 实现准则与预警【核心】
- **一键安全复制防护**：多媒体资产复制需要较长磁盘 I/O 耗时，已有的 `BatchProgressDialog` 异步加载线程完美保障了界面无阻碍性。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 输入框清除功能 | 一律使用 Qt 原生 setClearButtonEnabled(true)，不涉及本方案 | ✅ |
| 窗口置顶 | 一律使用 Win32 原生 SetWindowPos，不涉及本方案 | ✅ |
| 标题栏按钮样式 | 标题栏及按钮颜色规范，不涉及本方案 | ✅ |

## 8. 待确认事项（可选）
无。
