# 半静态/半动态托管库热拔插与离线动态计数重构方案 —— ManagedLibraryHotPlugRefactor.md

> **核心原则**：半静态/半动态托管库分类（`arcmeta.library_*`）本质上是纯粹的虚拟分类，受系统属性保护（不可删除、不可重命名）。其节点显隐与全库资产有效性严格由物理硬件热拔插驱动：**拔盘时节点瞬间隐藏、盘内资产即时判定离线，全库所有分类计数同步扣减；插盘时节点恢复重现，资产恢复在线，全库计数同步加回**。

---

## 一、 实际代码中的 3 处核心缺陷分析 (Root Cause)

经过对 `src/ui/CategoryModel.cpp`、`src/meta/StatisticsService.cpp` 以及 `src/ui/CategoryPanel.cpp` 底层源码的深度排查，发现当前代码存在以下不符合规范的缺陷：

### 1. 侧边栏节点缺乏硬件热拔插过滤：拔盘后节点离线残留
- **代码位置**：`src/ui/CategoryModel.cpp`（第 65~85 行 `refresh`）
- **现象**：`CategoryModel::refresh()` 在渲染 `CategoryKind::SystemLibrary`（如 `arcmeta.library_g`）时，直接遍历数据库/缓存中的历史纪录，未校验对应盘符（如 `G:`）当前是否物理挂载/在线。
- **后果**：拔掉 G 盘后，侧边栏依然遗留显示 `arcmeta.library_g (0)` 离线节点，造成视觉混乱。

### 2. 统计计算缺失在线谓词断言：拔盘后全库计数未同步扣减
- **代码位置**：`src/meta/StatisticsService.cpp` 与 `src/meta/CategoryRepo.cpp`
- **现象**：静态分类桶（“全部数据”、“未分类”、“回收站”）以及全动态用户分类（“文件夹”、“测试”等）在计算关联资产数时，仅仅执行 `is_trash = 0` 过滤，未包含 `drive_letter IN (onlineDrives)` 物理在线判别。
- **后果**：当 G 盘被拔出后，G 盘上原本包含的资产（元数据保留在 SQLite 中）依然被算入“全部数据”、“未分类”以及用户分类的统计数字中，未能做到“盘拔数据无效、计数立刻扣减”。

### 3. 受保护节点交互未强隔离：允许对托管库误执行重命名或删除
- **代码位置**：`src/ui/CategoryPanel.cpp`（右键菜单构建逻辑 `onContextMenuRequested`）
- **现象**：系统未对 `CategoryKind::SystemLibrary` 类型节点建立 `isSystemProtected` 交互保护拦截，使得右键菜单依然弹出一普通分类的“重命名”、“删除分类”、“设置颜色”选项。
- **后果**：用户误操作试图删除或修改托管库节点，引发逻辑异常或报错。

---

## 二、 核心重构技术方案 (Technical Fix)

### 1. 硬件热拔插驱动器与在线盘符集合：`VolumeOnlineManager`

建立在线盘符监测管线，维护全应用实时在线盘符集合：

```cpp
// 在线盘符状态管理器概念设计
class VolumeOnlineManager : public QObject {
    Q_OBJECT
public:
    static VolumeOnlineManager& instance();
    
    // 获取当前物理在线的托管盘符集合 (如 {"C", "D", "Z"})
    QSet<QString> getOnlineDrives() const;
    
    // 校验特定托管库 (如 "arcmeta.library_g") 是否处于在线状态
    bool isLibraryOnline(const QString& libraryName) const {
        QString drive = extractDriveLetter(libraryName); // 提取 "G"
        return m_onlineDrives.contains(drive.toUpper());
    }

signals:
    // 当物理磁盘发生热拔插变更时发射广播信号
    void volumeStateChanged(const QString& driveLetter, bool isOnline);

private:
    QSet<QString> m_onlineDrives;
};
```

---

### 2. 动态计数断言谓词升级：`StatisticsService` 在线感知与即时扣减/恢复

重构 `StatisticsService` 与 `CategoryRepo` 的统计计算，引入物理卷在线断言：

$$\text{有效资产} = (is\_trash == 0) \ \mathbf{AND}\ (drive\_letter \in \text{m\_onlineDrives})$$

```cpp
// 权威计数出账引擎重构
StatisticsSnapshot StatisticsService::calculateSnapshot() {
    StatisticsSnapshot snapshot;
    QSet<QString> onlineDrives = VolumeOnlineManager::instance().getOnlineDrives();

    // 1. 全局静态桶在线聚合 (仅计算物理在线盘符下的有效资产)
    snapshot.systemCounts["all"] = CategoryRepo::countAllOnlineAssets(onlineDrives);
    snapshot.systemCounts["uncategorized"] = CategoryRepo::countUncategorizedOnlineAssets(onlineDrives);
    snapshot.systemCounts["trash"] = CategoryRepo::countTrashOnlineAssets(onlineDrives);

    // 2. 半静态托管库统计 (仅为物理在线的盘符输出真实数量)
    for (const QString& drive : onlineDrives) {
        int catId = CategoryRepo::getLibraryCategoryIdByDrive(drive);
        snapshot.libraryCounts[catId] = CategoryRepo::countLibraryAssets(drive);
    }

    // 3. 全动态用户分类统计 (仅计算属于在线盘符的关联资产)
    snapshot.categoryCounts = CategoryRepo::countUserCategoriesOnlineAssets(onlineDrives);

    return snapshot;
}

// 当热拔插事件触发时，300ms 防抖后瞬间重新出账并广播刷新 UI
void StatisticsService::onVolumeStateChanged(const QString& driveLetter, bool isOnline) {
    m_debounceTimer->start(300); // 300ms 防抖
}
```

---

### 3. UI 节点受保护隔离与路由自愈平滑回退

在 `CategoryModel` 与 `CategoryPanel` 中实现节点保护与自动平滑回退：

```cpp
// 1. CategoryModel 侧边栏渲染在线过滤
void CategoryModel::refresh() {
    // ...
    QSet<QString> onlineDrives = VolumeOnlineManager::instance().getOnlineDrives();
    for (const auto& cat : categories) {
        if (cat.kind == CategoryKind::SystemLibrary && cat.parentId == 0) {
            QString driveLetter = extractDriveLetter(cat.name);
            // 🛡️ 离线拦截：如果该盘符已拔出，直接跳过，不在侧边栏渲染该节点！
            if (!onlineDrives.contains(driveLetter.toUpper())) {
                continue; 
            }
            // 正常渲染在线的 arcmeta.library_* 节点
            appendLibraryItem(cat);
        }
    }
}

// 2. 视口路由自愈回退机制 (主窗口平滑处理)
void MainWindow::onVolumeUnplugged(const QString& driveLetter) {
    QString targetLib = "arcmeta.library_" + driveLetter.toLower();
    if (m_currentRoute.contains(targetLib)) {
        // 🛡️ 如果当前正在浏览已被拔出的托管库，平滑自动回退至“全部数据”
        navigateTo("category://-1?name=全部数据");
    }
}
```

---

## 三、 重构后效果验证矩阵 (Verification Matrix)

| 场景事件 | 修复前表现 | 重构后标准表现 |
| :--- | :--- | :--- |
| **拔出 G 盘（热拔脱机）** | `arcmeta.library_g (0)` 残留显示；“全部数据”和用户分类计数保持原样不扣减 | **`arcmeta.library_g` 节点瞬间隐藏；“全部数据”与用户分类数字立即精准扣减 G 盘资产数量** |
| **插回 G 盘（热插联机）** | 界面无感知，数字不同步 | **`arcmeta.library_g (N)` 瞬间恢复显示；全库所有分类计数立即精准加回加总** |
| **右键点击 `arcmeta.library_*`** | 弹出“重命名”、“删除分类”选项，可点击 | **重命名/删除/设色选项物理置灰或隐藏，严格限制篡改** |
| **停留在 G 盘视口时拔盘** | 界面显示空白或报错卡死 | **路由系统自动、平滑回退至“全部数据”主视口，无缝防崩** |

---
*文档建立时间：2026-08-13*
