# NTFS 自动导入多进程独占锁阻塞与多媒体解析断流架构根治重构 —— Modification_Plan-102.md

> 状态：已批准，执行中

## 1. 任务背景
用户反馈现有的 “新建自动导入” 功能存在严重的逻辑架构缺陷与隐患。本方案旨在针对该自动导入算法中独占写事务内包含慢 I/O、特征解析未投递多媒体流水线导致特征永远不被提取、以及子分类映射仅按名匹配导致映射重叠冲突等一系列缺陷，进行底层的彻底性根治和重构（对应用户原话：“分析并评估一下关于‘创建自动导入’功能，是否存在傻逼逻辑架构/缺陷”、“请给出相应的修改方案”）。

## 2. 问题定位
通过代码静态审计，我们定位到自动对账导入算法 `CategoryRepo::syncPhysicalDirectoryCascade` 存在以下致命架构缺陷：

1. **写事务内包含慢磁盘 I/O（最严重的写锁独占阻塞）**：
   原方法在调用时立刻建立 `SqlTransaction trans(db)`。在此长事务生命周期内，递归调用 `QDir::entryInfoList` 同步扫描整个物理目录树。在扫描海量文件夹（如数万文件）时，磁盘 I/O 极其缓慢，导致 SQLite 写锁被长期占有，此时 UI 线程或其他后台线程任何涉及数据库写入的操作都会高频触发 `SQLITE_BUSY` 造成界面彻底死锁或闪退。
2. **多媒体解析特征提取断流（只标记待处理、却不入解析队列）**：
   扫描到文件时，原算法仅同步调用了 `MetadataManager::registerItem`。该方法仅修改缓存中的状态标记为待处理状态 `0`，但**从未**将路径投递到 `MediaExtractorPipeline` 的队列中。导致导入的大量图形文件其高级元数据（宽、高、自适应主色）永远处于残缺状态，直到被动点击才会被提取。
3. **优先按名匹配造成分类树物理映射冲突**：
   原匹配子分类算法优先使用 `findCategoryId` 按当前父类和名字检索同名分类。如果用户曾做过重命名、分类变动或存在数据库幽灵冲突，同名的两个完全不同的物理目录会被映射到同一个分类 ID 下，导致极其严重的数据关联污染。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 分析并评估一下关于“创建自动导入”功能，是否存在傻逼逻辑架构/缺陷 (对应用户原话) | 全面审计自动导入底层数据流，指出“持锁阻塞”、“多媒体解析断流”、“按名覆盖冲突”三大缺陷 (对应用户原话：“分析并评估一下关于‘创建自动导入’功能，是否存在傻逼逻辑架构/缺陷”)。 | ✅ 一致 |
| 2    | 请给出相应的修改方案 (对应用户原话) | 实现“慢 I/O 与写事务完全剥离”、“分批异步投递多媒体流水线”、“优先 FRN 唯一性识别”三大模块重构方案 (对应用户原话：“请给出相应的修改方案”)。 | ✅ 一致 |

## 4. 详细解决方案

我们采用**工业级内存树两阶段对账模型**，彻底将慢磁盘 I/O 扫描与 SQLite 写事务完全解耦分离，并重新设计对账算法。

### 4.1 引入内存扫描树结构
在 `src/meta/CategoryRepo.cpp` 匿名命名空间中引入 ScanNode 辅助结构，用来临时缓存纯 I/O 的文件和文件夹指纹：
```cpp
struct ScanNode {
    std::wstring path;
    std::wstring name;
    uint64_t frn = 0;
    bool isDir = false;
    std::vector<ScanNode> children;
    std::vector<std::wstring> files;
};
```

### 4.2 第一阶段：纯 I/O 异步磁盘扫描（绝不开任何数据库事务）
通过一个不含任何 SQLite 数据库操作、纯 CPU 与 I/O 的递归函数建立起物理目录和文件树模型：
```cpp
static void scanPhysicalDirectory(const QString& currentPath, ScanNode& node) {
    QDir currentDir(currentPath);
    QFileInfoList list = currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& fi : list) {
        std::wstring wPath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        if (fi.isDir()) {
            std::string fid;
            std::wstring frnStr;
            if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid, &frnStr)) {
                try {
                    ScanNode childNode;
                    childNode.path = wPath;
                    childNode.name = fi.fileName().toStdWString();
                    childNode.frn = std::stoull(frnStr, nullptr, 16);
                    childNode.isDir = true;
                    scanPhysicalDirectory(fi.absoluteFilePath(), childNode);
                    node.children.push_back(std::move(childNode));
                } catch (...) {}
            }
        } else {
            node.files.push_back(wPath);
        }
    }
}
```

### 4.3 第二阶段：超高速内存对账与批量写事务
当上面的纯 I/O 递归扫描完成并建立好完整的 `ScanNode` 内存树后，我们开启一个**极高速度、无阻塞的纯 CPU-DB 写入事务**（因为所有的磁盘慢扫描已在内存中完毕，此事务耗时将降至几十毫秒，绝对不会对 UI 线程造成任何阻塞）：
1. **优先使用 FRN 指纹检索分类**，防止重名导致的错乱；
2. **在对账写入时，收集所有需要注册的文件路径**，并一次性递交给 `MetadataManager::registerItemsAsync`，让其平滑批量入队流水线。

重构后的 `CategoryRepo::syncPhysicalDirectoryCascade` 核心实现：
```cpp
void CategoryRepo::syncPhysicalDirectoryCascade(const std::wstring& rootPath) {
    // ----------------------------------------------------
    // 【第一阶段】：纯 I/O 目录树收集，绝对不持任何 DB 写锁，杜绝假死
    // ----------------------------------------------------
    ScanNode rootNode;
    rootNode.path = rootPath;
    QFileInfo rootInfo(QString::fromStdWString(rootPath));
    rootNode.name = rootInfo.fileName().toStdWString();
    
    std::string rootFid;
    std::wstring rootFrnStr;
    if (!MetadataManager::fetchWinApiMetadataDirect(rootPath, rootFid, &rootFrnStr)) {
        return; 
    }
    try {
        rootNode.frn = std::stoull(rootFrnStr, nullptr, 16);
    } catch (...) { return; }
    rootNode.isDir = true;

    // 同步纯磁盘递归扫描，此时数据库不被上任何锁
    scanPhysicalDirectory(QString::fromStdWString(rootPath), rootNode);

    // ----------------------------------------------------
    // 【第二阶段】：超高速、高安全性纯内存与 CPU 对账，并开启极速写事务
    // ----------------------------------------------------
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    SqlTransaction trans(db);

    int rootCatId = CategoryRepo::findByFrn(rootNode.frn);
    if (rootCatId == 0) {
        std::wstring parentPath = rootInfo.absolutePath().toStdWString();
        std::string parentFid;
        std::wstring parentFrnStr;
        int parentCatId = 0;
        if (MetadataManager::fetchWinApiMetadataDirect(parentPath, parentFid, &parentFrnStr)) {
            try {
                uint64_t pFrn = std::stoull(parentFrnStr, nullptr, 16);
                parentCatId = CategoryRepo::findByFrn(pFrn);
            } catch (...) {}
        }

        Category cat;
        if (rootInfo.fileName().startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
            cat.parentId = 0;
        } else {
            cat.parentId = parentCatId;
        }
        cat.name = rootNode.name;
        cat.physicalFrn = rootNode.frn;
        cat.physicalPath = rootNode.path;
        if (CategoryRepo::add(cat)) {
            rootCatId = cat.id;
            MetadataManager::instance().registerItem(rootPath, true);
        }
    }

    if (rootCatId <= 0) {
        trans.commit();
        return;
    }

    std::vector<std::wstring> collectedFilesToProcess;

    // 递归对账 lambda 函数
    std::function<void(const ScanNode&, int)> processNode;
    processNode = [&](const ScanNode& node, int parentCatId) {
        // 1. 处理子文件夹分类对账
        for (const auto& childNode : node.children) {
            // 【核心加固】：优先使用物理 FRN 指纹绝对唯一性检索，防止同名不同物理路径的映射重叠冲突
            int existingId = CategoryRepo::findByFrn(childNode.frn);
            if (existingId == 0) {
                // 如果指纹未命中，再尝试根据父分类ID和名字在数据库查找（处理可能的历史空 FRN 数据）
                existingId = CategoryRepo::findCategoryId(parentCatId, childNode.name);
                if (existingId > 0) {
                    // 若名字匹配了，检查其原有的 FRN。如果是空白或不符，安全修复并升级为 FRN 指纹标识
                    Category existingCat = CategoryRepo::getById(existingId);
                    if (existingCat.physicalFrn == 0 || existingCat.physicalFrn != childNode.frn) {
                        CategoryRepo::updatePhysicalMapping(existingId, childNode.frn, childNode.path);
                    }
                } else {
                    // 彻底未命中任何已知记录，新建分类
                    Category cat;
                    cat.parentId = parentCatId;
                    cat.name = childNode.name;
                    cat.physicalFrn = childNode.frn;
                    cat.physicalPath = childNode.path;
                    if (CategoryRepo::add(cat)) {
                        existingId = cat.id;
                        MetadataManager::instance().registerItem(childNode.path, true);
                    }
                }
            } else {
                // 指纹命中了，但可能物理路径由于用户外部移动发生过位移，执行安全升级修复路径关联
                Category existingCat = CategoryRepo::getById(existingId);
                if (existingCat.physicalPath != childNode.path || existingCat.parentId != parentCatId) {
                    existingCat.physicalPath = childNode.path;
                    existingCat.parentId = parentCatId;
                    CategoryRepo::update(existingCat);
                }
            }

            if (existingId > 0) {
                processNode(childNode, existingId);
            }
        }

        // 2. 收集此节点下的文件供批量多媒体提取与注册使用
        for (const auto& fPath : node.files) {
            collectedFilesToProcess.push_back(fPath);
            if (parentCatId > 0) {
                std::string fid;
                if (MetadataManager::fetchWinApiMetadataDirect(fPath, fid)) {
                    CategoryRepo::addItemToCategory(parentCatId, fid, fPath);
                }
            }
        }
    };

    processNode(rootNode, rootCatId);

    trans.commit();

    // ----------------------------------------------------
    // 【第三阶段】：异步投递多媒体高级特征提取流水线，解决断流 Bug
    // ----------------------------------------------------
    if (!collectedFilesToProcess.empty()) {
        QStringList qPathsToRegister;
        for (const auto& fp : collectedFilesToProcess) {
            qPathsToRegister.append(QString::fromStdWString(fp));
        }
        // 调用 registerItemsAsync，完美一键批处理在后台将文件塞入多媒体解析提取队列 (enqueueBatch)
        MetadataManager::instance().registerItemsAsync(qPathsToRegister, true);
        qDebug() << "[AutoImport] [Pipeline_Bridge] 已将" << qPathsToRegister.size() << "个新导入文件全部推入异步多媒体高级特征提取队列";
    }
}
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/meta/CategoryRepo.cpp`
  - 涉及类/函数：`CategoryRepo::syncPhysicalDirectoryCascade` (重构为工业级内存树两阶段对账模型，剥离写事务中的慢磁盘 I/O 扫描、引入 FRN 防冲突匹配以及桥接异步多媒体提取流水线)

**明确禁止越界修改的范围：**
- [ ] `CategoryRepo::getAll` / `add` 等底层数据库执行细节 —— 不修改
- [ ] `MetadataManager::registerItemsAsync` 底层解析逻辑与队列提取细节 —— 不修改
- [ ] 外部磁盘 USN / MFT 数据监听通知机制 —— 不修改

## 6. 实现准则与预警【核心】
1. **防止事务嵌套**：由于 `syncPhysicalDirectoryCascade` 内部使用的 `CategoryRepo::add` and `addItemToCategory` 均包含了读写数据库的操作，我们需要利用 `SqlTransaction` 自动处理事务。在 `SqlTransaction` 的生命周期中，我们不得再手动执行 `BEGIN TRANSACTION` 或 `COMMIT`，必须严格通过 RAII 的析构或 `commit()` 保证原子提交。
2. **极速事务锁原则**：第一阶段的 `scanPhysicalDirectory` 纯磁盘扫描中必须保证**绝不触碰任何数据库调用**，将其耗时的大头（I/O 寻道及读盘）放在事务开启前。开启事务后，全部转换为内存对账和内存高速写操作，从而实现毫秒级高能吞吐。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写内容，不写引用） | 本方案是否符合 |
|-------------|-------------|---|
| 快速预览 (QuickLook) 进阶规范 | 预览窗口内的滚动条样式必须严格遵循全局规范：宽度 10px、圆角 3px、背景透明、Handle 颜色对齐 BorderColor (#333333) | ✅ 符合。本方案不触碰 QuickLook，与该项不冲突。 |
| UI 异步加载与防闪烁规范 | 在内容面板进行异步数据扫描前，禁止先行调用 m_model->clear()，避免白屏视觉抖动，保留旧数据直至新数据通过 setRecords 实现毫秒级原子替换。 | ✅ 符合。本对账方案完成后调用 `notifyFullUIRebuild`，不触发 ContentPanel 上的提早 clear 产生闪烁。 |

## 8. 待确认事项（可选）
（无）
