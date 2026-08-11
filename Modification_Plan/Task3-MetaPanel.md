# 任务 3 重构施工图纸

---

### 修改文件 1：`src/ui/MainWindow.cpp`（补全悬空信号绑定）

在 `MainWindow::initUi()` 中，找到 `m_metaPanel` 的信号绑定位置（约第 580 行），添加 `noteEdited`（备注）与 `linkEdited`（链接）的监听槽函数：

```cpp
// 1. 补全备注编辑保存信号连接
connect(m_metaPanel, &MetaPanel::noteEdited, this, [](const QStringList& paths, const QString& newNote) {
    for (const QString& path : paths) {
        MetadataManager::instance().setNote(path.toStdWString(), newNote.toStdWString());
    }
});

// 2. 补全链接编辑保存信号连接
connect(m_metaPanel, &MetaPanel::linkEdited, this, [](const QStringList& paths, const QString& newLink) {
    for (const QString& path : paths) {
        MetadataManager::instance().setURL(path.toStdWString(), newLink.toStdWString());
    }
});
```

---

### 修改文件 2：`src/meta/MetadataManager.cpp`（修复磁盘模式 `getMeta` 读取空包 Bug）

修改 `MetadataManager::getMeta` 函数，加入**磁盘导航模式下的 `.ArcMeta.json` 双轨预载**。当快照中没有该磁盘项时，自动回退读取文件夹下的 `.ArcMeta.json`，并填入快照：

```cpp
RuntimeMeta MetadataManager::getMeta(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    // 1. 无锁（Lock-Free）原子获取当前最新快照指针
    auto currentSnapshot = std::atomic_load(&m_snapshot);
    if (currentSnapshot) {
        auto it = currentSnapshot->find(nPath);
        if (it != currentSnapshot->end()) return it->second;
    }
    
    // 🚨 2. 磁盘导航模式双轨回退：若内存快照中不存在，尝试从对应目录的 .ArcMeta.json 预载并回填快照
    if (!isInsideManagedLibrary(nPath)) {
        QFileInfo info(QString::fromStdWString(nPath));
        if (info.exists()) {
            std::wstring folderPath = info.absolutePath().toStdWString();
            std::wstring fileName = info.fileName().toStdWString();

            AmMetaJson amJson(folderPath);
            if (amJson.load()) {
                const auto& items = amJson.items();
                auto it = items.find(fileName);
                if (it != items.end()) {
                    const ItemMeta& itemMeta = it->second;
                    RuntimeMeta rm;
                    rm.rating = itemMeta.rating;
                    rm.manualColor = itemMeta.color;
                    rm.pinned = itemMeta.pinned;
                    rm.note = itemMeta.note;
                    rm.url = itemMeta.url;
                    rm.encrypted = itemMeta.encrypted;
                    rm.isFolder = (itemMeta.type == L"folder");
                    for (const auto& t : itemMeta.tags) rm.tags.append(QString::fromStdWString(t));
                    rm.palettes = itemMeta.palettes;
                    rm.isManaged = false; // 标记为磁盘非受控资产

                    // 写入内存快照，确保后续切选无需重复物理 I/O
                    std::unique_lock<std::shared_mutex> lock(m_mutex);
                    auto currentSnapshot2 = std::atomic_load(&m_snapshot);
                    if (currentSnapshot2) {
                        auto newMap = std::make_shared<std::unordered_map<std::wstring, RuntimeMeta>>(*currentSnapshot2);
                        (*newMap)[nPath] = rm;
                        std::atomic_store(&m_snapshot, std::shared_ptr<const std::unordered_map<std::wstring, RuntimeMeta>>(newMap));
                    }
                    return rm;
                }
            }
        }
    }
    
    return RuntimeMeta();
}
```

---

### 修改文件 3：`src/meta/AmMetaJson.cpp`（修正数据校验保存条件）

确保 `AmMetaJson::save()` 在写入 `.ArcMeta.json` 时，不漏掉包含“备注说明”或“链接”的项目。检查 `AmMetaJson.cpp` 中的 `save()` 函数：

```cpp
bool AmMetaJson::save() const {
    QJsonObject root;
    root.insert("version", "2");
    root.insert("folder", folderToEntry(m_folder));

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
        // 只要存在任何一项用户修改（星级、颜色、标签、备注、链接、色板），即判定为有效记录写入 JSON
        if (meta.hasUserOperations()) {
            itemsObj.insert(toQString(name), itemToEntry(meta));
        }
    }
    root.insert("items", itemsObj);

    QByteArray jsonData = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QString tmpPath = toQString(m_filePath) + ".tmp";
    
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(jsonData);
    tmpFile.close();

    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }
    SetFileAttributesW(m_filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}
```

---

交付执行者更新这 3 处修改后，磁盘导航模式（Disk Nav Mode）下的元数据读写与选中显示问题将彻底解决。完成后请告知，我们继续推进最后的 **任务 4**。