# 内存模式“永久删除”三重彻底根除修复方案 —— PermanentDelete.md

> **核心原则**：在内存模式（托管库）下，当用户选中项目并执行“永久删除”（Delete Permanently）时，系统必须 100% 执行 **物理胶囊 + 数据库元数据 + 内存快照** 的三重彻底根除，严禁产生 any 遗留垃圾或“幽灵记录”。

---

## 一、 实际代码中的 3 处核心缺陷分析 (Root Cause)

经过对 `src/ui/ContentPanel.cpp`、`src/meta/MetadataManager.cpp` 以及 `src/meta/StatisticsService.cpp` 底层源码的深度排查，发现当前代码存在以下不符合规范的缺陷：

### 1. 物理层级不彻底：解包视图删除遗留空白 `.arc` 胶囊壳
- **代码位置**：`ContentPanel.cpp`（第 2226~2238 行）
- **现象**：当在解包视图下对素材（如 `00ms73182x000.arc/artwork.png`）执行“永久删除”时，`recursiveRemove` 仅删除了内部的 `artwork.png` 文件，**未将外层的空 `.arc` 胶囊文件夹本身销毁**。
- **后果**：磁盘上残留无用的 `.arc` 物理空胶囊。

### 2. 数据库与内存层级失效：路径不匹配导致跳过元数据擦除
- **代码位置**：`MetadataManager.cpp`（第 2280~2300 行 `deletePermanently`）
- **现象**：因为传入的路径与内存快照映射表中的 Key 未进行 Base36 ID 归一化转换，导致 `currentSnapshot->find(nPath)` 匹配失败，系统错误打印 `永久删除项不在数据库中，跳过清理动作` 并直接退出。
- **后果**：SQLite `metadata` 主表、`category_items` 分类关联表里的数据库记录未被删除，产生“僵尸元数据”。

### 3. UI 状态不同步：回收站项永久删除未扣减侧边栏计数
- **代码位置**：`MetadataManager.cpp`（第 1935 行 `removeMetadataSync`）
- **现象**：代码中判定 `if (isManagedAsset(...) && !it->second.isTrash)` 含有 `!it->second.isTrash` 限制。当在“回收站”选项卡中执行永久删除时，因为文件的 `isTrash` 已经是 `true`，导致 `notifyAssetRemoved` 扣减通知被跳过。
- **后果**：侧边栏“回收站 (N)”数字未能及时归零扣减。

---

## 二、 核心修复技术方案 (Technical Fix)

### 1. 物理层级彻底销毁：`ContentPanel.cpp` 向上追溯并销毁 `.arc` 容器

在物理递归删除函数中，删除内部文件后增加对父级 `.arc` 容器的检查与销毁：

```cpp
// 彻底物理递归删除逻辑修正
std::function<bool(const QString&)> recursiveRemove;
recursiveRemove = [&](const QString& target) -> bool {
    QFileInfo info(target);
    bool ok = false;
    if (info.isDir()) {
        QDir dir(target);
        for (const QString& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
            recursiveRemove(target + "/" + entry);
        }
        ok = QDir().rmdir(target);
    } else {
        ok = QFile::remove(target);
        
        // 🛡️ 物理加固：如果删除的是 .arc 胶囊内部的文件，检查并销毁父目录 .arc
        QDir parentDir = info.dir();
        if (parentDir.dirName().endsWith(".arc", Qt::CaseInsensitive)) {
            QStringList remaining = parentDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
            if (remaining.isEmpty()) {
                parentDir.rmdir(parentDir.absolutePath()); // 物理销毁空白 .arc 胶囊壳
            }
        }
    }
    return ok;
};
```

---

### 2. 数据库与内存彻底擦除：`MetadataManager.cpp` ID 强校准反查

在 `deletePermanently` 入口，通过 Base36 ID 强行校准路径，确保 100% 触发擦除：

```cpp
void MetadataManager::deletePermanently(const std::wstring& path) {
    std::wstring nPath = MetadataManager::normalizePath(path);
    
    // 🛡️ 优先通过路径中的 13 位 Base36 ID 反查内存缓存 Key，防止路径解包不一致导致的匹配失败
    std::string base36Id = extractBase36Id(nPath);
    if (!base36Id.empty()) {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        auto it = m_folderIdToPath.find(base36Id);
        if (it != m_folderIdToPath.end()) {
            nPath = it->second; // 强行对齐为数据库与缓存中存储的标准路径
        }
    }

    // 执行彻底根除 (removeMetadataSync 会级联擦除 SQLite metadata 与 category_items)
    removeMetadataSync(nPath);

    // 广播 UI 全量刷新信号
    notifyUI(RefreshLevel::FullRebuild);
}
```

---

### 3. UI 计数及时扣减：`removeMetadataSync` 移除 `!isTrash` 拦截

修正 `removeMetadataSync`，保证回收站资产永久删除时依然触发计数扣减：

```cpp
// 在 removeMetadataSync 遍历扣减逻辑中：
if (isManagedAsset(it->second.isFolder, curPath)) {
    totalDelta--;
    // 🚨 无论是否处于回收站 (isTrash)，永久删除均必须通知 StatisticsService 扣减计数！
    StatisticsService::instance().notifyAssetRemoved(0, !it->second.tags.isEmpty(), it->second.isTrash); 
}
```

---

## 三、 修复后效果验证 (Verification Matrix)

| 擦除维度 | 旧代码表现 | 修复后标准表现 |
| :--- | :--- | :--- |
| **物理磁盘** | 残留空白 `00ms73182x000.arc` 文件夹 | **物理胶囊文件夹及其内部文件 100% 被销毁清空** |
| **SQLite 数据库** | `metadata` 主表与 `category_items` 表残存记录 | **数据库中该 ID 的所有行记录被 100% 清除** |
| **侧边栏 UI 计数** | 永久删除后“回收站 (5)”数字不发生改变 | **侧边栏“回收站”以及“全部数据”等数字立刻精准扣减归零** |

---
*文档建立时间：2026-08-13*
