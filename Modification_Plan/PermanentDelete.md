# 内存模式“永久删除”极简原子根除重构方案 —— PermanentDelete.md

> **核心架构原则**：无论数据当前被挂载在任何分类或视口（全部数据、回收站、托管库、自定义分类）中，只要执行“永久删除”，**直接且干脆地将该数据彻底脱离所有分类关系（全量清空 `category_items` 中的关联记录）**。绝对禁止根据删除视口/上下文编写繁琐交织的条件分支特判！

---

## 一、 旧架构变复杂与缺陷根因分析 (Root Cause)

经过对 `src/ui/ContentPanel.cpp`、`src/meta/MetadataManager.cpp` 以及 `src/meta/StatisticsService.cpp` 底层源码的深度排查，发现旧代码陷入了“补丁思维”：

### 1. 条件分支缠绕交织，导致某些视图删除时扣减被跳过
- **缺陷表现**：代码中大量充斥着 `if (!wasTrash)`、`if (targetCatId > 0)` 等条件分支判断，试图去“猜测”用户是在哪个视口发起的删除。
- **后果**：一旦用户在“回收站”或“托管库”视口发起永久删除，由于条件判断被跳过，导致侧边栏 **`arcmeta.library_g (5)`** 或其他分类节点的计数无法更新，依然停留显示旧数字 **(5)**。

### 2. 物理层级销毁不彻底：解包视图删除遗留空白 `.arc` 胶囊壳
- **代码位置**：`ContentPanel.cpp`（`recursiveRemove`）
- **现象**：在解包视图下对素材执行永久删除时，仅删除了内部素材文件，**未将外层的空 `.arc` 胶囊文件夹本身销毁**，造成磁盘垃圾残留。

### 3. ID 强校准缺失：路径不匹配导致跳过 DB 元数据擦除
- **代码位置**：`MetadataManager.cpp`（`deletePermanently`）
- **现象**：传入路径未与 Base36 ID 对齐，导致 `m_folderIdToPath` 匹配失败，系统错误跳过 SQLite `metadata` 主表的删除动作，产生“僵尸元数据”。

---

## 二、 极简原子根除管线 (Atomic Purge Pipeline)

彻底废除所有特定上下文的 `if-else` 条件特判，建立统一、干净、原子化的彻底注销管线：

```
[ 用户触发“永久删除” ]
       │
       ├── 1. 物理层彻底销毁：递归删除内部文件，并同步销毁外层空白 `.arc` 胶囊文件夹
       │
       ├── 2. 数据库级彻底解绑：
       │     ├── 彻底删除 SQLite `metadata` 主表对应条目
       │     └── 彻底擦除关联表（DELETE FROM category_items WHERE asset_id = :id）
       │         👉 无论资产挂载在多少个分类下，直接 100% 强行彻底脱离所有分类关系！
       │
       └── 3. 内存与 UI 计数全向原子扣减：
             ├── 反查该资产删除前关联的所有分类集合（托管库 ID + 用户分类 ID）
             └── 无视视口上下文，对涉及的所有维度（全局静态桶、托管库、用户分类）统一全向 -1 并刷重绘！
```

### 1. 物理层级彻底销毁：向上追溯并销毁空 `.arc` 胶囊

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

### 2. 数据库级原子解绑：强行彻底脱离所有分类

```cpp
void CategoryRepo::removeAllCategoryAssociations(const QString& assetId) {
    // 🛡️ 极简管线：直接彻底清除 category_items 中该资产的所有关联记录！
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM category_items WHERE asset_id = :asset_id");
    query.bindValue(":asset_id", assetId);
    query.exec();
}
```

### 3. 内存与 UI 全向原子扣减：摒弃分支特判

```cpp
// 彻底废除旧有的 conditional branch，统一全向扣减
void StatisticsService::purgeAsset(const AssetInfo& deletedAsset) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);

    // 1. 全局静态桶扣减
    if (m_cachedSnapshot.systemCounts["all"] > 0) m_cachedSnapshot.systemCounts["all"]--;
    if (deletedAsset.isTrash) {
        if (m_cachedSnapshot.systemCounts["trash"] > 0) m_cachedSnapshot.systemCounts["trash"]--;
    } else {
        if (deletedAsset.categories.isEmpty() && m_cachedSnapshot.systemCounts["uncategorized"] > 0) {
            m_cachedSnapshot.systemCounts["uncategorized"]--;
        }
    }

    // 2. 托管库分类扣减 (直击 arcmeta.library_g 扣减归零)
    int libCatId = deletedAsset.libraryCatId;
    if (libCatId > 0 && m_cachedSnapshot.libraryCounts[libCatId] > 0) {
        m_cachedSnapshot.libraryCounts[libCatId]--;
    }

    // 3. 该资产挂载过的所有用户分类全量扣减
    for (int userCatId : deletedAsset.categories) {
        if (m_cachedSnapshot.userCategoryCounts[userCatId] > 0) {
            m_cachedSnapshot.userCategoryCounts[userCatId]--;
        }
    }

    // 4. 广播统一刷新，驱动侧边栏数字 instant 归零
    emit statisticsUpdated(m_cachedSnapshot);
}
```

---

## 三、 重构后效果验证矩阵 (Verification Matrix)

| 场景维度 | 旧代码表现（复杂分支特判） | 极简原子管线表现 |
| :--- | :--- | :--- |
| **分类脱离彻底性** | 仅根据当前视口试图删除局部关系 | **直接彻底清空该资产在 `category_items` 中的所有关联，100% 彻底脱离所有分类** |
| **物理磁盘** | 残留空白 `00ms73182x000.arc` 文件夹 | **物理胶囊文件夹及其内部文件 100% 被销毁清空** |
| **SQLite 数据库** | `metadata` 主表与 `category_items` 表残存记录 | **数据库中该资产 ID 的所有行记录被 100% 原子擦除** |
| **侧边栏 UI 各类计数** | 永久删除 G 盘 5 个项目后 `arcmeta.library_g (5)` 数字不变 | **侧边栏所有关联维度（回收站、托管库 `arcmeta.library_g`、全部数据等）数字统一即时精准扣减归零** |

---
*文档更新时间：2026-08-13*
