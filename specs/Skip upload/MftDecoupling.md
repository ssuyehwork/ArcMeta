# 实施方案：MftReader 杂糅解耦、图标缓存抽离与模块物理清理方案 (MftDecoupling)

## 所属大纲章节
**1.1 全局数据与内存管理**（1.1.8 MftReader 杂糅解耦与僵尸代码根除规范）

---

## 涉及代码文件
* `src/ui/IconCacheManager.h` （新增：抽离出的独立 UI 图标缓存单例类头文件）
* `src/ui/IconCacheManager.cpp` （新增：抽离出的独立 UI 图标缓存单例类实现文件）
* `src/meta/TagRepository.cpp` （修改：移除 `#include "../mft/MftReader.h"`，统一使用 `QDir::drives()` 标准接口）
* `src/ui/TrayController.cpp` （修改：移除 `#include "../mft/MftReader.h"`，删除 `MftReader::instance().clear()` 退场冗余调用）
* `src/core/CoreController.cpp` （修改：清理 `#include "../mft/MftReader.h"`）
* `src/meta/MetadataManager.cpp` （修改：清理 `#include "../mft/MftReader.h"`）
* `src/main.cpp` （修改：清理 `#include "mft/MftReader.h"`）
* `src/ui/MainWindow.cpp` （修改：清理 `#include "../mft/MftReader.h"`）
* `src/mft/MftReader.h` （删除：物理移除）
* `src/mft/MftReader.cpp` （删除：物理移除）
* `CMakeLists.txt` （修改：从工程构建源文件列表中移除 `src/mft/MftReader.h` 与 `src/mft/MftReader.cpp`）

---

## 功能描述
在 ArcMeta 500万+ 内存模式与 SQLite 托管库架构下，USN 变更日志与全盘物理扫描已弃用。`MftReader` 模块存在严重的职责杂糅（硬塞 UI 图标缓存 `getCachedIcon`）、退避打补丁（`TagRepository.cpp` 中的双重盘符查询）、退场冗余空跑以及提权隐患。
本方案执行彻底解耦与根除：
1. **抽离图标缓存**：创建专职工具类 `IconCacheManager`，接管 UI 图标缓存与加载功能；
2. **统一盘符感知**：收敛盘符枚举至 `QDir::drives()` 标准跨平台接口；
3. **清理退场与头文件依赖**：剔除全项目对 `MftReader` 的所有头文件包含与方法调用；
4. **物理彻底清理**：删除 `MftReader` 源文件并更新构建配置。

---

## 技术决策
1. **单功能独立封装**：新增 `IconCacheManager` 单例，仅负责全局扩展名与文件夹 `QIcon` 缓存，使用 `QReadWriteLock` 保证多线程安全，消解 UAF（Use-After-Free）风险，遵循单一职责原则（SRP）。
2. **标准接口替代**：全项目涉及系统盘符枚举的场景，一律使用 Qt 标准 `QDir::drives()` 接口，不再依赖 Win32 底层 MFT 引擎。
3. **彻底物理移除**：不留死角、不留僵尸文件，将 `MftReader` 模块彻底从磁盘与 CMake 构建清单中删除。

---

## 强制性七项断层排查清单

1. **头文件核对**：
   * `src/ui/IconCacheManager.h` 包含 `<QIcon>`, `<QHash>`, `<QString>`, `<QReadWriteLock>`, `<QFileIconProvider>`。
   * 清理 `TagRepository.cpp`、`TrayController.cpp`、`CoreController.cpp`、`MetadataManager.cpp`、`MainWindow.cpp` 和 `main.cpp` 中引用的 `mft/MftReader.h`。
2. **成员核对**：
   * `IconCacheManager` 声明 `static IconCacheManager& instance()`，`QIcon getCachedIcon(const QString& ext, bool isDir)`。
3. **残留核对**：
   * 全局检索 `MftReader` 与 `getCachedIcon`，将所有图标调用点替换为 `IconCacheManager::instance().getCachedIcon(...)`；确认零遗留调用点。
4. **断层核对（上下文连续性）**：
   * 检查 `TagRepository.cpp` 中的 `checkAndMigrate` 函数，替换双重判断为直接遍历 `QDir::drives()`。
5. **C++ 语法与特殊成员函数合规排查**：
   * `IconCacheManager` 构造函数显式声明为 `explicit IconCacheManager(QObject* parent = nullptr);`，在 `.cpp` 中实现。
6. **废除成员全量引用点清扫排查**：
   * 检查所有调用 `MftReader::instance().clear()` 的地方，一并删除该语句。
7. **未引用局部变量（-Wunused-variable）防断层排查**：
   * 在删除 `MftReader` 相关逻辑后，核对是否遗留了仅用于保存 `MftReader` 返回值的临时变量，一并擦除。

---

## 核心代码实现与改动对照

### 新增文件：`src/ui/IconCacheManager.h`
```cpp
#pragma once

#include <QObject>
#include <QIcon>
#include <QHash>
#include <QString>
#include <QReadWriteLock>

namespace ArcMeta {

class IconCacheManager : public QObject {
    Q_OBJECT
public:
    static IconCacheManager& instance();

    QIcon getCachedIcon(const QString& ext, bool isDir);

private:
    explicit IconCacheManager(QObject* parent = nullptr);

    mutable QReadWriteLock m_cacheLock;
    QHash<QString, QIcon> m_iconCache;
};

} // namespace ArcMeta
```

### 新增文件：`src/ui/IconCacheManager.cpp`
```cpp
#include "IconCacheManager.h"
#include <QFileIconProvider>
#include <QFileInfo>

namespace ArcMeta {

IconCacheManager& IconCacheManager::instance() {
    static IconCacheManager inst;
    return inst;
}

IconCacheManager::IconCacheManager(QObject* parent)
    : QObject(parent) {
}

QIcon IconCacheManager::getCachedIcon(const QString& ext, bool isDir) {
    QString key = isDir ? "folder" : ext.toLower();
    {
        QReadLocker lock(&m_cacheLock);
        auto it = m_iconCache.find(key);
        if (it != m_iconCache.end()) return *it;
    }

    QFileIconProvider provider;
    QIcon icon;
    if (isDir) {
        icon = provider.icon(QFileIconProvider::Folder);
    } else {
        if (key.length() > 12) key = "unknown";
        icon = provider.icon(QFileInfo("dummy." + key));
        if (icon.isNull()) icon = provider.icon(QFileIconProvider::File);
    }

    {
        QWriteLocker lock(&m_cacheLock);
        m_iconCache[key] = icon;
    }
    return icon;
}

} // namespace ArcMeta
```

### 修改文件：`src/meta/TagRepository.cpp`
```cpp
<<<<<<< SEARCH
#include "TagRepository.h"
#include "DatabaseManager.h"
#include "../mft/MftReader.h"
#include "MetadataManager.h"
=======
#include "TagRepository.h"
#include "DatabaseManager.h"
#include "MetadataManager.h"
>>>>>>> REPLACE

<<<<<<< SEARCH
    // 3. 执行迁移
    if (!globalHasGroups) {
        std::vector<std::wstring> drives = MftReader::instance().getDriveList();
        if (drives.empty()) {
            for (const QFileInfo& driveInfo : QDir::drives()) {
                drives.push_back(driveInfo.absolutePath().toStdWString());
            }
        }
=======
    // 3. 执行迁移
    if (!globalHasGroups) {
        std::vector<std::wstring> drives;
        for (const QFileInfo& driveInfo : QDir::drives()) {
            drives.push_back(driveInfo.absolutePath().toStdWString());
        }
>>>>>>> REPLACE
```

### 修改文件：`src/ui/TrayController.cpp`
```cpp
<<<<<<< SEARCH
#include "../mft/MftReader.h"

void TrayController::onQuitApp() {
    MftReader::instance().clear(); 
    qApp->quit();
}
=======
void TrayController::onQuitApp() {
    qApp->quit();
}
>>>>>>> REPLACE
```

---

## 已知问题 / 待办
* 无。

---

## 涉及文件清单
1. `src/ui/IconCacheManager.h`（新增：UI 图标缓存专职工具类头文件）
2. `src/ui/IconCacheManager.cpp`（新增：UI 图标缓存专职工具类实现文件）
3. `src/meta/TagRepository.cpp`（修改：收敛盘符获取至 QDir::drives，移除 MftReader 依赖）
4. `src/ui/TrayController.cpp`（修改：移除 MftReader 退场多余调用与头文件引用）
5. `src/core/CoreController.cpp`（修改：清理多余头文件引用）
6. `src/meta/MetadataManager.cpp`（修改：清理多余头文件引用）
7. `src/main.cpp`（修改：清理多余头文件引用）
8. `src/ui/MainWindow.cpp`（修改：清理多余头文件引用）
9. `src/mft/MftReader.h`（删除：物理文件删除）
10. `src/mft/MftReader.cpp`（删除：物理文件删除）
11. `CMakeLists.txt`（修改：剔除 MftReader 编译源文件项）
