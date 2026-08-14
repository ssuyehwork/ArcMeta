# 标记双轨制不隔离与代码耦合违规点 —— Modification_Plan-11.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在 ArcMeta 双轨制数据路由分流架构的精细化演进下，“磁盘目录模式（DiskNav）”与“内存数据库模式（Category/System/PathList）”应当保持 100% 的绝对物理隔离与高内聚运行（对应用户原话：“磁盘目录模式...内存数据库模式...这两条轨道各自独立运行，代码层面不该有任何交集——不共用状态变量，不互相判断对方在做什么，不互相调用对方的处理逻辑”）。
为防止在复杂的全量重构过程中出现逻辑漏洞或误修改，需要遵循“先标记，后逐个精准重构”的渐进式整改路径（对应用户原话：“我期望先将违反了双轨制的高内聚纯净性的部分标记出来，然后再按照标记的部分逐一修改”）。因此，本次任务将在代码中精准插入 6 处违规交集点的注释标记，绝不改变任何既有运行逻辑，保持系统的安全与纯粹。

## 2. 问题定位
通过对整个 `ContentPanel.cpp`、`IndexedEntry.cpp` 进行全量深度代码审计，共定位出 6 处不符合双轨 100% 隔离、存在交集、状态混叠与交叉调用的典型耦合违规点：
1. **磁盘导航卡片加载时偷偷查询 SQLite 库 (`IndexedEntry.cpp`)**：创建 `ItemRecord` 时无条件调用了 `MetadataManager::getMeta`，使磁盘纯净浏览模式在加载时发生了数据库查询倒灌。
2. **`isManagedContext` 判断导致两轨代码耦合 (`ContentPanel.cpp`)**：在磁盘模式下直接调用托管库的 `isInsideManagedLibrary` 判断当前路径是否安全托管，形成了逻辑交集。
3. **右键菜单在非镜像源（磁盘模式）下强行判断托管属性 (`ContentPanel.cpp`)**：强行通过判断 `isManaged` 与 `isInsideLib` 来转换 `isMirror` 的局部状态阀，污染了磁盘模式的纯净右键功能。
4. **磁盘模式拖拽粘贴中物理移动时直接调用 `syncAfterMove` (`ContentPanel.cpp`)**：磁盘模式拖拽物理移动文件后直接对数据库管理器进行了调用通知。
5. **磁盘模式剪贴粘贴中物理移动时直接调用 `syncAfterMove` (`ContentPanel.cpp`)**：剪贴板物理粘贴移动后同样发生相互调用，破坏了双轨不调用对方逻辑的底线。
6. **共享模型 `setData` 中无条件执行物理重命名 (`ContentPanel.cpp`)**：托管库内重命名理应为逻辑改写 SQLite，而此处在共享模型中无条件执行了 `ShellHelper::renameItem` 物理更名。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 我期望先将违反了双轨制的高内聚纯净性的部分标记出来 | 4.1、4.2 节，提供 6 处违规耦合点的精准物理 Merge Diff 替换块，在代码中安全嵌入说明注释 | ✅ |
| 2    | 然后再按照标记的部分逐一修改，不然工作量会很大 | 4.1、4.2 节，在代码中插入明确的 `// 🚨 [双轨不隔离违规点-N]` 注释，以备后续逐个击破 | ✅ |

## 4. 详细解决方案
本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 标记 `IndexedEntry.cpp` 中的双轨不隔离违规点

```
<<<<<<< SEARCH
    // 1. 物理属性采样 (零 I/O 核心)
    RuntimeMeta meta;
    if (providedMeta) {
        meta = *providedMeta;
    } else {
        meta = MetadataManager::instance().getMeta(wPath);
    }
=======
    // 1. 物理属性采样 (零 I/O 核心)
    // 🚨 [双轨不隔离违规点-1]: 磁盘导航模式下通过 MetadataManager::getMeta 直接读取了托管库 SQLite 数据库
    RuntimeMeta meta;
    if (providedMeta) {
        meta = *providedMeta;
    } else {
        meta = MetadataManager::instance().getMeta(wPath);
    }
>>>>>>> REPLACE
```

### 4.2 标记 `ContentPanel.cpp` 中的双轨不隔离违规点

```
<<<<<<< SEARCH
bool ContentPanel::isManagedContext() const {
    if (isMirrorSource()) return true;
    return MetadataManager::instance().isInsideManagedLibrary(m_currentPath.toStdWString());
}
=======
bool ContentPanel::isManagedContext() const {
    // 🚨 [双轨不隔离违规点-2]: 磁盘模式（isMirrorSource() == false）下通过 isInsideManagedLibrary 判断当前路径是否在托管库中，导致双轨制逻辑交叉混叠
    if (isMirrorSource()) return true;
    return MetadataManager::instance().isInsideManagedLibrary(m_currentPath.toStdWString());
}
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
        bool isMirror = isMirrorSource();
        if (!isMirror && onItem) {
            // 物理修复：只要该项已被登记（isManaged），或者是托管库内部的项，一律允许显示“设定颜色标签”和“归类”
            bool isManaged = currentIndex.data(ManagedRole).toBool();
            bool isInsideLib = MetadataManager::instance().isInsideManagedLibrary(path.toStdWString());
            isMirror = isManaged || isInsideLib;
        }
=======
        // 🚨 [双轨不隔离违规点-3]: 右键菜单在非镜像源（磁盘模式）下，强行判断 isManaged 或 isInsideLib 以允许数据库修改操作（归类/颜色标签/置顶），破坏了磁盘模式行为等同于资源管理器的纯粹性
        bool isMirror = isMirrorSource();
        if (!isMirror && onItem) {
            // 物理修复：只要该项已被登记（isManaged），或者是托管库内部的项，一律允许显示“设定颜色标签”和“归类”
            bool isManaged = currentIndex.data(ManagedRole).toBool();
            bool isInsideLib = MetadataManager::instance().isInsideManagedLibrary(path.toStdWString());
            isMirror = isManaged || isInsideLib;
        }
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
        if (ShellHelper::copyOrMoveItems(paths, destDir, isMove)) {
            if (isMove) {
                for (const QString& src : paths) {
                    QString destPath = QDir(destDir).absoluteFilePath(QFileInfo(src).fileName());
                    MetadataManager::instance().syncAfterMove(
                        src.toStdWString(), destPath.toStdWString());
                }
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(paths, QFileInfo(paths.first()).absolutePath(), destDir));
            }
            loadDirectory(m_currentPath, m_isRecursive);
        }
=======
        if (ShellHelper::copyOrMoveItems(paths, destDir, isMove)) {
            if (isMove) {
                for (const QString& src : paths) {
                    QString destPath = QDir(destDir).absoluteFilePath(QFileInfo(src).fileName());
                    // 🚨 [双轨不隔离违规点-4]: 磁盘模式（DiskNav）物理移动文件后直接调用 MetadataManager::syncAfterMove 相互调用对方的处理逻辑，存在耦合
                    MetadataManager::instance().syncAfterMove(
                        src.toStdWString(), destPath.toStdWString());
                }
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(paths, QFileInfo(paths.first()).absolutePath(), destDir));
            }
            loadDirectory(m_currentPath, m_isRecursive);
        }
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
        if (ShellHelper::copyOrMoveItems(fromPaths, m_currentPath, isMove)) { 
            if (isMove) {
                for (const QString& src : fromPaths) {
                    QString destPath = QDir(m_currentPath).absoluteFilePath(QFileInfo(src).fileName());
                    MetadataManager::instance().syncAfterMove(src.toStdWString(), destPath.toStdWString());
                }
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(fromPaths, QFileInfo(fromPaths.first()).absolutePath(), m_currentPath));
            }
            loadDirectory(m_currentPath, m_isRecursive); 
=======
        if (ShellHelper::copyOrMoveItems(fromPaths, m_currentPath, isMove)) { 
            if (isMove) {
                for (const QString& src : fromPaths) {
                    QString destPath = QDir(m_currentPath).absoluteFilePath(QFileInfo(src).fileName());
                    // 🚨 [双轨不隔离违规点-5]: 磁盘模式（DiskNav）物理移动文件后直接调用 MetadataManager::syncAfterMove 相互调用对方的处理逻辑，存在耦合
                    MetadataManager::instance().syncAfterMove(src.toStdWString(), destPath.toStdWString());
                }
                UndoManager::instance().pushCommand(std::make_unique<MoveCommand>(fromPaths, QFileInfo(fromPaths.first()).absolutePath(), m_currentPath));
            }
            loadDirectory(m_currentPath, m_isRecursive); 
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
    if (role == Qt::EditRole && index.column() == 0) {
        if (record.isCategory) return false; // 2026-07-xx 按照 Plan-73：子分类暂不支持在此重命名

        QString newName = value.toString().trimmed();
        if (newName.isEmpty()) return false;

        auto& mutableRecord = m_allRecords[index.row()];
        QString oldPath = mutableRecord.path;
        QFileInfo info(oldPath);
        QString newPath = info.absolutePath() + "/" + newName;
=======
    if (role == Qt::EditRole && index.column() == 0) {
        // 🚨 [双轨不隔离违规点-6]: 在共享模型的 setData 中，无论处于托管库（内存）模式还是磁盘导航模式，重命名操作都无条件执行了物理重命名 ShellHelper::renameItem，混淆了两轨的重命名逻辑（托管库内应为仅改写 SQLite 映射字段的逻辑重命名，磁盘模式下应为物理重命名）
        if (record.isCategory) return false; // 2026-07-xx 按照 Plan-73：子分类暂不支持在此重命名

        QString newName = value.toString().trimmed();
        if (newName.isEmpty()) return false;

        auto& mutableRecord = m_allRecords[index.row()];
        QString oldPath = mutableRecord.path;
        QFileInfo info(oldPath);
        QString newPath = info.absolutePath() + "/" + newName;
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/core/IndexedEntry.cpp`（标记磁盘导航卡片加载时偷偷查询 SQLite 库点）
- [ ] `src/ui/ContentPanel.cpp`（标记 isManagedContext、右键菜单 isMirror 交叉、粘贴/拖拽 syncAfterMove 相互调用、以及共享模型重命名耦合点）

**明确禁止越界修改的范围：**
- [ ] 严禁进行任何实际的功能逻辑代码修改与执行。

## 6. 实现准则与预警【核心】
1. **纯注释标记**：本方案仅作纯代码注释的标记插入，严禁涉及任何实际变量、类方法或信号流的代码逻辑改写。
2. **Git merge diff 精准对齐**：在物理实施中，必须精准匹配代码文件的原有行号及特征代码，杜绝因缩进或括号匹配不符导致的编译失败。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨制路由分流 | 数据流绝对不交叉。托管库仅改写 SQLite 映射字段，磁盘模式进行物理处理同步缓存 | ✅ 符合。本方案正是为了物理标记此类违规交叉点而设计，为后续的完美隔离整改奠定最严谨、安全的基础。 |

## 8. 待确认事项（可选）
- **无**。
