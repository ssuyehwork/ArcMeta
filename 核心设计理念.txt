# ArcMeta 核心架构理念备忘

## 一句话总结
ArcMeta 是一个"无库概念"的文件元数据管理系统。
元数据跟着文件走，索引全局唯一，界面只是视图。

---

## 三层架构

### 第一层：分布式存储层（跟文件走）
每个目录下的 .arcmeta/ 只负责该目录的直属文件。
文件复制到哪里，元数据就跟到哪里。
没有被用户操作过的文件，不产生任何文件。

    任意目录/
      photo.jpg
      .arcmeta/
        photo.jpg.scch      ← 只属于这一个文件
        __folder__.scch     ← 只属于这个文件夹自身

### 第二层：全局索引层（集中在 AppData）
分布式层解决"存"的问题。
全局索引层解决"查"的问题。

    AppData/ArcMeta/
      tags.index.scch           ← 标签 → FID 的反向索引
      arcmeta_categories.scch   ← 用户分类数据库
      All_FRN_metadata.scch     ← FRN → 物理路径 的追踪索引

### 第三层：内存缓存层（MetadataManager）
启动时将所有已知的 .arcmeta/ 加载进内存。
读取永远优先走内存，不走磁盘。
写入先写内存，异步落盘，1.5 秒防抖批量持久化。

---

## 五个核心原则

### 原则一：无库概念
没有 .library，没有需要"切换"的数据库边界。
AllFrnManager 追踪所有硬盘上所有出现过 .arcmeta/ 的目录。
切换查看哪个硬盘，只是 UI 导航行为，底层缓存从不卸载。

### 原则二：文件级粒度
一个文件的元数据变更，只写一个 .scch 文件。
不因为修改一个文件而重写整个目录的元数据。
这是与旧版 metadata.scch（目录级）最根本的区别。

### 原则三：按需创建
没有元数据 = 没有文件。
.arcmeta/ 目录和 .scch 文件只在用户真正操作后才创建。
系统不主动为任何文件创建空的元数据文件。

### 原则四：虚拟分类不落盘
"未分类"、"未标签"、"全部数据"、"最近访问"
这些系统分类永远是派生值，从不写入磁盘。

    未分类 = 全部已知文件 − 已归入用户分类的文件
    未标签 = 全部已知文件 − 有标签的文件

计数通过增量计数器维护，复杂度 O(1)，不做全量遍历。

### 原则五：信号不直连 UI 重建
MetadataManager::metaChanged 不直接触发任何全量重建。
侧边栏刷新必须经过防抖（800ms），批量操作只触发一次刷新。
系统分类计数更新只修改文字，不重建 QStandardItem。

---

## 文件身份体系

每个文件有唯一身份标识（fileId128），格式为：

    FRN:<卷序列号>:<NTFS文件索引号>

这个 ID 在同一块硬盘内，无论文件改名、移动，都不变。
所有跨模块的文件关联（分类、标签）都通过 FID 而非路径建立。
路径只是 FID 的"最后已知位置"，通过 AllFrnManager 追踪。

---

## 与 Eagle 的核心区别

| | Eagle | ArcMeta |
|---|---|---|
| 存储边界 | .library（封闭库） | 无边界（全局） |
| 切换数据集 | 需重新加载整个库 | 不存在切换概念 |
| 元数据粒度 | 每文件独立目录 | 每文件独立 .scch |
| 跨库搜索 | 不支持 | 天然支持 |
| 离线访问 | 库不在则不可用 | 内存缓存始终可用 |
| 未分类实现 | 不存在此概念 | 虚拟派生，不落盘 |

---

## 开发时的判断标准

每次做设计决策时，用这三个问题检验：

1. 这个操作会不会因为一个文件而影响其他无关文件？
   如果会，说明粒度设计错了。

2. 这个数据是"事实"还是"计算结果"？
   计算结果不应该落盘，应该实时派生。

3. 这个信号会不会在批量操作时被触发 N 次？
   如果会，必须加防抖，禁止直连 UI 重建逻辑。
   
// ===================|===================

根据我们的完整讨论，以下是给 Jules 的提示词：

---

```
# ArcMeta 架构重构：文件级元数据 + 全局索引体系

## 背景
当前系统使用目录级 metadata.scch，即一个目录下所有文件共享一个
元数据文件。这导致修改单个文件的元数据时必须重写整个目录的 scch，
同时侧边栏刷新逻辑与 MetadataManager::metaChanged 信号直连，
造成批量操作时 UI 线程假死。本次重构目标是将存储粒度降至文件级，
并建立全局索引体系，彻底解决上述问题。

---

## 第一部分：AmMetaScch 重构（文件级存储）

### 1.1 新的目录结构规范

每个目录下创建隐藏子目录 .arcmeta/，按需存放该目录下各文件的元数据：

    任意目录/
      photo.jpg
      document.pdf
      子文件夹/
      .arcmeta/
        photo.jpg.scch       ← photo.jpg 的元数据
        document.pdf.scch    ← document.pdf 的元数据
        __folder__.scch      ← 该目录自身的元数据（替代原 FolderMeta）
        子文件夹/
          .arcmeta/
            __folder__.scch  ← 子文件夹自身的元数据

规则：
- .arcmeta/ 按需创建，没有任何元数据的目录不创建此文件夹
- 只有被用户操作过（打星、设颜色、加标签）的文件才创建对应 .scch
- .arcmeta/ 目录创建后立即设置 FILE_ATTRIBUTE_HIDDEN

### 1.2 AmMetaScch 构造函数修改

将现有单参数构造函数改为双参数：

    // 文件夹自身元数据
    AmMetaScch(const std::wstring& folderPath, const std::wstring& fileName = L"");

    // fileName 为空时：操作 folderPath/.arcmeta/__folder__.scch
    // fileName 非空时：操作 folderPath/.arcmeta/fileName.scch

内部路径计算：
    m_arcmetaDir = folderPath + L"\\.arcmeta";
    if (fileName.empty()) {
        m_filePath = m_arcmetaDir + L"\\__folder__.scch";
        m_isFileMode = false;
    } else {
        m_filePath = m_arcmetaDir + L"\\" + fileName + L".scch";
        m_isFileMode = true;
    }

### 1.3 数据结构简化

文件模式（m_isFileMode = true）：只存储一个 ItemMeta，废弃
map<wstring, ItemMeta> 结构，新增：

    ItemMeta m_item;      // 文件模式专用
    bool m_isFileMode;

文件夹模式（m_isFileMode = false）：只存储 FolderMeta，与现在一致。

对外新增接口：
    ItemMeta& item() { return m_item; }
    const ItemMeta& item() const { return m_item; }
    void setItem(const ItemMeta& item) { m_item = item; }

保留原有接口（向后兼容过渡期使用）：
    FolderMeta& folder();
    std::map<std::wstring, ItemMeta>& items(); // 标记为 deprecated

### 1.4 save() 修改

在 save() 开头加入目录创建逻辑：

    // 按需创建 .arcmeta 目录
    QDir dir(QString::fromStdWString(m_arcmetaDir));
    if (!dir.exists()) {
        dir.mkpath(".");
        SetFileAttributesW(m_arcmetaDir.c_str(), FILE_ATTRIBUTE_HIDDEN);
    }

其余写入逻辑根据 m_isFileMode 分支：
- 文件模式：只序列化 m_item 的字段
- 文件夹模式：只序列化 m_folder 的字段

### 1.5 旧格式迁移

在 MetadataManager::initFromScchMode() 开头加入一次性迁移逻辑：

    static void migrateLegacyScch(const std::wstring& folderPath) {
        QString legacyPath = QString::fromStdWString(folderPath) + "/metadata.scch";
        if (!QFile::exists(legacyPath)) return;

        // 读取旧格式
        AmMetaScch legacy(folderPath);  // 用旧版单参数构造（保留过渡期）
        if (!legacy.load()) return;

        // 拆分写入新格式
        // 1. 文件夹自身
        AmMetaScch folderScch(folderPath, L"");
        folderScch.folder() = legacy.folder();
        folderScch.save();

        // 2. 每个文件项
        for (const auto& kv : legacy.items()) {
            AmMetaScch fileScch(folderPath, kv.first);
            fileScch.setItem(kv.second);
            fileScch.save();
        }

        // 3. 删除旧文件
        QFile::remove(legacyPath);
    }

---

## 第二部分：MetadataManager 调用方式更新

### 2.1 getMeta() 修改

    // 改前：加载整个目录的 scch，再从 map 里找
    AmMetaScch loader(parentDir);
    loader.load();
    auto it = loader.items().find(fileName);

    // 改后：直接加载该文件的 scch
    AmMetaScch loader(parentDir, fileName);
    loader.load();
    const ItemMeta& item = loader.item();

### 2.2 persistAsync() 修改

    // 改前：读整目录 → 改一项 → 写整目录
    AmMetaScch loader(parentDir);
    loader.load();
    loader.items()[fileName] = newMeta;
    loader.save();

    // 改后：直接写该文件的 scch
    AmMetaScch loader(parentDir, fileName);
    loader.setItem(newMeta);
    loader.save();

    // 文件夹自身：
    AmMetaScch loader(parentDir, L"");
    loader.folder() = folderMeta;
    loader.save();

### 2.3 setItemVisualMetadata() / setColor() / setPalettes() 等

这些方法内部最终都走 debouncePersist()，不需要单独修改，
persistAsync() 改好后自动生效。

### 2.4 removeMetadataSync() 修改

    // 删除文件时：只删该文件对应的 scch
    QString scchPath = QString::fromStdWString(parentDir) +
                       "/.arcmeta/" + fileName + ".scch";
    QFile::remove(scchPath);

    // 删除文件夹时：删除整个 .arcmeta/ 子目录
    QDir arcmetaDir(QString::fromStdWString(folderPath) + "/.arcmeta");
    arcmetaDir.removeRecursively();

### 2.5 AllFrnManager 追踪目标更新

将追踪对象从 metadata.scch 的位置改为 .arcmeta/ 目录的位置：

    // 改前
    std::wstring metaPath = parentDir + L"\\metadata.scch";
    if (fetchWinApiMetadataDirect(metaPath, metaFid, &metaFrn))
        AllFrnManager::registerFrn(metaFrn, parentDir);

    // 改后
    std::wstring arcmetaPath = parentDir + L"\\.arcmeta";
    std::wstring arcmetaFrn; std::string arcmetaFid;
    if (fetchWinApiMetadataDirect(arcmetaPath, arcmetaFid, &arcmetaFrn))
        AllFrnManager::registerFrn(arcmetaFrn, parentDir);

---

## 第三部分：全局标签索引（TagRepo）

### 3.1 新建 TagRepo.h / TagRepo.cpp

    namespace ArcMeta {

    struct TagEntry {
        std::wstring tagName;               // 唯一键
        std::vector<std::string> fileIds;   // 关联的 fileId128 列表
    };

    class TagRepo {
    public:
        // 给文件绑定标签（标签不存在则自动创建）
        static bool bindTag(const std::string& fid,
                            const std::wstring& tag,
                            const std::wstring& pathHint = L"");

        // 解绑标签
        static bool unbindTag(const std::string& fid, const std::wstring& tag);

        // 查询某标签下所有文件的 FID
        static std::vector<std::string> getFidsByTag(const std::wstring& tag);

        // 查询某文件的所有标签
        static std::vector<std::wstring> getTagsByFid(const std::string& fid);

        // 获取所有标签及其文件数量（用于侧边栏标签管理面板）
        static std::vector<std::pair<std::wstring, int>> getAllTagsWithCount();

        // 重命名标签（全局生效）
        static bool renameTag(const std::wstring& oldName,
                              const std::wstring& newName);

        // 删除标签（从所有文件解绑）
        static bool deleteTag(const std::wstring& tag);
    };

    } // namespace ArcMeta

### 3.2 存储格式

存储路径：AppData/ArcMeta/tags.index.scch（使用现有 SCCH 二进制格式）

二进制结构：
    Header:
      char magic[4] = 'TAGS'
      uint32_t version = 1
      uint32_t tagCount

    Per tag:
      wstring tagName
      uint32_t fidCount
      string fid[fidCount]

### 3.3 MetadataManager::setTags() 联动

在 setTags() 里同步更新全局标签索引：

    void MetadataManager::setTags(const std::wstring& path,
                                   const QStringList& tags,
                                   bool notify) {
        std::wstring nPath = normalizePath(path);

        // 1. 获取旧标签
        QStringList oldTags;
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            auto it = m_cache.find(nPath);
            if (it != m_cache.end()) oldTags = it->second.tags;
        }

        // 2. 更新内存缓存
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            m_cache[nPath].tags = tags;
        }

        // 3. 增量更新全局标签索引
        std::string fid = m_cache[nPath].fileId128;
        if (!fid.empty()) {
            // 解绑已移除的标签
            for (const QString& t : oldTags) {
                if (!tags.contains(t)) {
                    TagRepo::unbindTag(fid, t.toStdWString());
                }
            }
            // 绑定新增的标签
            for (const QString& t : tags) {
                if (!oldTags.contains(t)) {
                    TagRepo::bindTag(fid, t.toStdWString(), nPath);
                }
            }
        }

        if (notify) emit metaChanged(QString::fromStdWString(nPath));
        debouncePersist(nPath);
    }

---

## 第四部分：未分类的虚拟化实现

### 4.1 核心原则

未分类不写入任何数据库记录。它是一个派生值：

    未分类文件集合 = 全部已知文件（m_cache，isFolder=false）
                   − 已归入任何用户分类的文件（CategoryRepo 全局索引）

### 4.2 CategoryRepo 新增增量计数接口

在 CategoryRepo 内部维护两个原子计数器：

    static std::atomic<int> s_totalFileCount;      // m_cache 中非文件夹项总数
    static std::atomic<int> s_categorizedCount;    // 已归类的唯一 FID 数量

对外接口：
    static int getUncategorizedCount() {
        return s_totalFileCount.load() - s_categorizedCount.load();
    }

在以下时机更新计数器：
- MetadataManager::initFromScchMode() 完成后：
  遍历 m_cache 初始化 s_totalFileCount
- addItemToCategory() 时：s_categorizedCount++
- removeItemFromCategory() 时：检查该 FID 是否还在其他分类，
  若彻底移除则 s_categorizedCount--
- 文件被物理删除时（removeMetadataSync）：s_totalFileCount--

### 4.3 侧边栏计数更新方式

废弃现有 CategoryRepo::getSystemCounts() 里的全量遍历逻辑，
改为直接读取增量计数器：

    QMap<QString, int> CategoryRepo::getSystemCounts() {
        QMap<QString, int> counts;
        counts["all"]            = s_totalFileCount.load();
        counts["uncategorized"]  = getUncategorizedCount();
        counts["untagged"]       = s_totalFileCount.load()
                                   - TagRepo::getTaggedFileCount();
        // 其他系统分类保持现有逻辑
        return counts;
    }

---

## 第五部分：侧边栏刷新去抖（修复信号风暴）

### 5.1 在 MainWindow 中断开直连

找到 MainWindow 中将 MetadataManager::metaChanged 连接到
CategoryModel::refresh() 的代码，将其改为带防抖的延迟刷新：

    // 改前（直连，每次信号都触发全量重建）
    connect(&MetadataManager::instance(), &MetadataManager::metaChanged,
            m_categoryModel, &CategoryModel::refresh);

    // 改后（防抖，800ms 内多次触发只执行最后一次）
    m_sidebarRefreshTimer = new QTimer(this);
    m_sidebarRefreshTimer->setInterval(800);
    m_sidebarRefreshTimer->setSingleShot(true);

    connect(&MetadataManager::instance(), &MetadataManager::metaChanged,
            this, [this](const QString& path) {
        // __RELOAD_ALL__ 信号立即刷新，其他信号防抖
        if (path == "__RELOAD_ALL__") {
            m_categoryModel->refresh();
        } else {
            m_sidebarRefreshTimer->start(); // 重置计时器
        }
    });

    connect(m_sidebarRefreshTimer, &QTimer::timeout,
            m_categoryModel, &CategoryModel::refresh);

### 5.2 CategoryModel::refresh() 改为增量更新

对于系统分类节点（全部数据、未分类、未标签等），
不重建 QStandardItem，只更新显示文本：

    void CategoryModel::updateSystemCounts() {
        auto counts = CategoryRepo::getSystemCounts();
        // 遍历已有的系统项，只修改文本，不重建
        for (int i = 0; i < invisibleRootItem()->rowCount(); ++i) {
            QStandardItem* item = invisibleRootItem()->child(i);
            QString type = item->data(TypeRole).toString();
            if (counts.contains(type)) {
                QString name = item->data(NameRole).toString();
                item->setText(QString("%1 (%2)").arg(name).arg(counts[type]));
            }
        }
    }

    // refresh() 里区分场景：
    void CategoryModel::refresh() {
        if (m_isFirstLoad) {
            fullRebuild();       // 首次加载：全量重建
            m_isFirstLoad = false;
        } else {
            updateSystemCounts(); // 后续刷新：只更新计数文字
            // 用户分类树只在结构真正发生变化时重建
        }
    }

---

## 执行顺序

请按以下顺序实现，每步完成后确保编译通过再进行下一步：

1. AmMetaScch 构造函数和路径计算（1.2）
2. AmMetaScch 数据结构和读写逻辑（1.3 / 1.4）
3. MetadataManager 调用方式更新（第二部分）
4. 旧格式迁移逻辑（1.5）
5. TagRepo 新建（第三部分）
6. 未分类虚拟化（第四部分）
7. 侧边栏防抖和增量刷新（第五部分）

## 不要改动的部分

- AmMetaScch 的二进制序列化格式（BinaryHeader 魔数、版本号、字段顺序）
- MetadataManager 的锁机制（shared_mutex）
- AllFrnManager 的注册接口签名
- CategoryRepo 的现有公开接口签名（只新增，不修改）
- ContentPanel、CategoryPanel 的 UI 逻辑
```