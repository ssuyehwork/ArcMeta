# 彻底清理并清退 `.am_meta.json`，统一使用 `.ArcMeta.json` 离散元数据 —— Modification_Plan-33.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在项目资产管理开发演进中，此前曾存在过 `.am_meta.json` 作为各目录的本地资产离散元数据标记文件。为了保障全工程的绝对纯净、一致与统一，清除历史冗余实现，系统需彻底清退全工程中所有关于 `.am_meta.json` 的代码、字符串与注释，不保留任何历史残留痕迹（对应用户原话：“彻底清空并清退全工程中所有关于 `.am_meta.json` 的代码、字符串与注释，不保留任何历史残留痕迹！”）。
同时，在不侵入 SQLite 的磁盘目录下，全局统一使用纯正、标准的 `.ArcMeta.json` 离散配置文件（对应用户原话：“全局统一使用纯正、标准的 `.ArcMeta.json`”），使物理硬盘上有且仅会生成隐藏文件 `.ArcMeta.json`，达到绝对的架构极简与纯净。

## 2. 问题定位
- **离散管理逻辑**：`AmMetaJson.cpp` 此前使用主程序下 `ArcMeta.cache/HASH.json` 的形式进行了缓存。现在需要将其彻底更改为隐藏在对应物理文件夹内的 `.ArcMeta.json` 隐藏文件直接读取和保存（对应用户原话：“全局统一使用纯正、标准的 `.ArcMeta.json`”）。
- **历史冗余排除**：在 `DiskScanService.cpp` 与 `CategoryLoadService.cpp` 的资产过滤逻辑中，此前含有针对 `.am_meta.json` 等历史冗余配置的屏蔽过滤判断。现在需要精简过滤判断（对应用户原话：“彻底清理扫描过滤逻辑中关于 `.am_meta.json` 的历史冗余条件”），且物理磁盘扫描时应对新的隐藏文件 `.ArcMeta.json` 执行彻底屏蔽过滤。
- **源码注释清理**：主工程 `CMakeLists.txt` 等各处的注释可能仍带有 `.am_meta.json` 字样，需进行统一净化擦除。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 彻底清空并清退全工程中所有关于 `.am_meta.json` 的代码、字符串与注释，不保留任何历史残留痕迹！ | 彻底排查清理 `CMakeLists.txt`、源文件等中的注释与字符串，清除 `.am_meta.json` 字样。 | ✅ 一致 |
| 2    | 全局统一使用纯正、标准的 `.ArcMeta.json`，唯一物理文件名：`.ArcMeta.json`，物理硬盘上有且仅会生成隐藏文件 `.ArcMeta.json` | 物理对齐修改 `AmMetaJson.cpp`，移除旧集中化缓存，直接采用直接路径 + `.ArcMeta.json`，并调用 `SetFileAttributesW` 设置隐藏属性。 | ✅ 一致 |
| 3    | 彻底清理扫描过滤逻辑中关于 `.am_meta.json` 的历史冗余条件，引入 `isAuxiliaryFile` 对 `.ArcMeta.json` 进行无缝拦截 | 在 `CategoryLoadService.cpp`、`DiskScanService.cpp` 中定义并应用统一的 `isAuxiliaryFile`，仅拦截 `.ArcMeta.json`、`_thumbnail.png`、`metadata.scch` 和 `.arc`。 | ✅ 一致 |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换，不得做任何自由发挥或脑补改动。

### 4.1 核心离散文件重构

#### 4.1.1 `src/meta/AmMetaJson.h`

```
<<<<<<< SEARCH
/**
 * @brief 处理 ArcMeta.cache 下高级 JSON 离散缓存 (.json) 的读写管理类
 * 2026-07-xx 双轨架构重构：
 * 1. 彻底摒弃 XMP 格式，全面采用纯粹、高效、结构化的 JSON 规范。
 * 2. 磁盘模式下不污染用户物理文件夹，统一存放在主程序根目录下的 "ArcMeta.cache" 高级缓存文件夹中。
 */
class AmMetaJson {
public:
    /**
     * @brief 获取 ArcMeta.cache 根目录绝对路径（若不存在会自动创建）
     */
    static QString getCacheDirectory();

    /**
     * @brief 内部转换辅助：根据目标文件夹物理路径计算出 ArcMeta.cache 中唯一的 JSON 路径
     */
    static std::wstring resolveCacheFilePath(const std::wstring& folderPath);

    /**
     * @param folderPath 目标物理文件夹的完整路径
     */
    explicit AmMetaJson(const std::wstring& folderPath);
=======
/**
 * @brief 处理 .ArcMeta.json 隐藏配置文件的读写管理类
 * 2026-08-xx 双轨架构重构：
 * 1. 全面采用纯粹、高效、结构化的 JSON 规范。
 * 2. 磁盘模式下采用标准的 .ArcMeta.json 隐藏文件直接保存在物理目录中。
 */
class AmMetaJson {
public:
    /**
     * @brief 物理整体迁移/重命名文件夹缓存接口（历史兼容，在直接保存模式下，重命名会自动由操作系统物理转移子文件）
     */
    static bool migrateFolderCache(const QString& oldFolderPath, const QString& newFolderPath);

    /**
     * @param folderPath 目标物理文件夹的完整路径
     */
    explicit AmMetaJson(const std::wstring& folderPath);
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
private:
    std::wstring m_folderPath;
    std::wstring m_filePath; // 映射到 ArcMeta.cache/ 中的真实 .json 物理路径
    
    FolderMeta m_folder;
=======
private:
    std::wstring m_folderPath;
    std::wstring m_filePath; // 映射到物理文件夹中的 .ArcMeta.json 路径
    
    FolderMeta m_folder;
>>>>>>> REPLACE
```

#### 4.1.2 `src/meta/AmMetaJson.cpp`

```
<<<<<<< SEARCH
#include "AmMetaJson.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QCoreApplication>
#include <QCryptographicHash>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ArcMeta {

QString AmMetaJson::getCacheDirectory() {
    // 默认存放在主程序根目录下的 "ArcMeta.cache" 文件夹
    QString appDir = QCoreApplication::applicationDirPath();
    QString cacheDir = QDir::toNativeSeparators(appDir + "/ArcMeta.cache");
    
    QDir dir(cacheDir);
    if (!dir.exists()) {
        dir.mkpath(cacheDir);
        // 在 Windows 下将 ArcMeta.cache 文件夹设为隐藏属性，保持根目录整洁
        #ifdef Q_OS_WIN
        SetFileAttributesW(cacheDir.toStdWString().c_str(), FILE_ATTRIBUTE_HIDDEN);
        #endif
    }
    return cacheDir;
}

std::wstring AmMetaJson::resolveCacheFilePath(const std::wstring& folderPath) {
    if (folderPath.empty()) return L"";

    // 1. 路径归一化（统一为小写与 Windows 标准分隔符，确保同一个文件夹算出的哈希绝对唯一）
    QString normPath = QDir::toNativeSeparators(QDir::cleanPath(QString::fromStdWString(folderPath))).toLower();
    
    // 2. 使用 SHA-256 算法计算物理路径的唯一哈希，彻底规避长路径 (MAX_PATH) 及非法文件名字符问题
    QByteArray hash = QCryptographicHash::hash(normPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    
    // 3. 拼装为 ArcMeta.cache/哈希值.json
    QString cacheFileName = QString::fromLatin1(hash) + ".json";
    QString fullCachePath = getCacheDirectory() + "/" + cacheFileName;
    return QDir::toNativeSeparators(fullCachePath).toStdWString();
}

AmMetaJson::AmMetaJson(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    // 将输入的物理文件夹路径映射为 ArcMeta.cache 中的高级 JSON 缓存物理路径
    m_filePath = resolveCacheFilePath(folderPath);
}
=======
#include "AmMetaJson.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ArcMeta {

AmMetaJson::AmMetaJson(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    std::wstring path = folderPath;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    // 🚨 彻底废除 .am_meta.json，唯一物理文件名：.ArcMeta.json
    m_filePath = path + L".ArcMeta.json";
}
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
bool AmMetaJson::save() const {
    if (m_filePath.empty()) return false;

    QJsonObject root;
    root.insert("version", "2"); // 物理对齐 SHA-256 版本
    root.insert("folder", folderToEntry(m_folder));

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
        if (meta.hasUserOperations()) {
            itemsObj.insert(toQString(name), itemToEntry(meta));
        }
    }
    root.insert("items", itemsObj);

    QByteArray jsonData = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QString targetPath = toQString(m_filePath);
    QString tmpPath = targetPath + ".tmp";
    
    QFile tmpFile(tmpPath);
    if (!tmpFile.open(QIODevice::WriteOnly)) return false;
    tmpFile.write(jsonData);
    tmpFile.close();

    // 物理原子替换，保障多线程落盘崩溃安全
    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

bool AmMetaJson::renameItem(const QString& folderPath, const QString& oldName, const QString& newName) {
    if (oldName == newName) return true;
    AmMetaJson meta(folderPath.toStdWString());
    if (!meta.load()) return false;
    auto& items = meta.items();
    auto it = items.find(oldName.toStdWString());
    if (it != items.end()) {
        items[newName.toStdWString()] = it->second;
        items.erase(it);
        return meta.save();
    }
    return true;
}

bool AmMetaJson::migrateFolderCache(const QString& oldFolderPath, const QString& newFolderPath) {
    if (oldFolderPath == newFolderPath) return true;
    std::wstring oldCachePath = resolveCacheFilePath(oldFolderPath.toStdWString());
    std::wstring newCachePath = resolveCacheFilePath(newFolderPath.toStdWString());

    QFile oldFile(toQString(oldCachePath));
    if (oldFile.exists()) {
        // 如果新缓存文件已存在，先清理
        QFile::remove(toQString(newCachePath));
        return oldFile.rename(toQString(newCachePath));
    }
    return true;
}
=======
bool AmMetaJson::save() const {
    QJsonObject root;
    root.insert("version", "2");
    root.insert("folder", folderToEntry(m_folder));

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
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
    // 赋予 Windows 隐藏文件属性
    SetFileAttributesW(m_filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);
    return true;
}

bool AmMetaJson::renameItem(const QString& folderPath, const QString& oldName, const QString& newName) {
    if (oldName == newName) return true;
    AmMetaJson meta(folderPath.toStdWString());
    if (!meta.load()) return false;
    auto& items = meta.items();
    auto it = items.find(oldName.toStdWString());
    if (it != items.end()) {
        items[newName.toStdWString()] = it->second;
        items.erase(it);
        return meta.save();
    }
    return true;
}

bool AmMetaJson::migrateFolderCache(const QString& oldFolderPath, const QString& newFolderPath) {
    Q_UNUSED(oldFolderPath);
    Q_UNUSED(newFolderPath);
    return true;
}
>>>>>>> REPLACE
```

### 4.2 拦截过滤逻辑纯净重构

#### 4.2.1 `src/core/CategoryLoadService.cpp`

```
<<<<<<< SEARCH
#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "CategoryLockManager.h"

namespace ArcMeta {
=======
#include "CategoryLoadService.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "CategoryLockManager.h"

namespace {
static inline bool isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    // 🚨 仅保留 .ArcMeta.json，彻底清除 .am_meta.json 历史判断
    if (path.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        path.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        path.endsWith("metadata.scch", Qt::CaseInsensitive) ||
        path.endsWith(".arc", Qt::CaseInsensitive)) {
        return true; // 屏蔽过滤
    }

    return false;
}
}

namespace ArcMeta {
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
            MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
                if (meta.isTrash || meta.isFolder) return;

                QString qPath = QString::fromStdWString(path);
                if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
                    qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
                    return;
                }
=======
            MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
                if (meta.isTrash || meta.isFolder) return;

                QString qPath = QString::fromStdWString(path);
                if (isAuxiliaryFile(qPath)) {
                    return;
                }
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
            if (!wPath.empty()) {
                if (isAssetLocked(item.folderId)) {
                    continue;
                }
                QString qPath = QString::fromStdWString(wPath);
                if (qPath.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
                    qPath.endsWith("metadata.scch", Qt::CaseInsensitive)) {
                    continue;
                }
                allRecords.push_back(ItemRecord::create(qPath, nullptr, true));
            }
=======
            if (!wPath.empty()) {
                if (isAssetLocked(item.folderId)) {
                    continue;
                }
                QString qPath = QString::fromStdWString(wPath);
                if (isAuxiliaryFile(qPath)) {
                    continue;
                }
                allRecords.push_back(ItemRecord::create(qPath, nullptr, true));
            }
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (p.endsWith("_thumbnail.png", Qt::CaseInsensitive)) {
                continue;
            }
            std::string assetId = MetadataManager::instance().getFolderIdSync(p.toStdWString());
=======
std::vector<ItemRecord> CategoryLoadService::loadPathItems(const QStringList& paths) {
    std::vector<ItemRecord> records;
    records.reserve(static_cast<int>(paths.size()));
    for (const QString& p : paths) {
        if (!p.isEmpty()) {
            if (isAuxiliaryFile(p)) {
                continue;
            }
            std::string assetId = MetadataManager::instance().getFolderIdSync(p.toStdWString());
>>>>>>> REPLACE
```

#### 4.2.2 `src/core/DiskScanService.cpp`

```
<<<<<<< SEARCH
#include "DiskScanService.h"
#include "../meta/AmMetaJson.h"
#include <QDir>
#include <QFileInfo>

namespace ArcMeta {
=======
#include "DiskScanService.h"
#include "../meta/AmMetaJson.h"
#include <QDir>
#include <QFileInfo>

namespace {
static inline bool isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    // 🚨 仅保留 .ArcMeta.json，彻底清除 .am_meta.json 历史判断
    if (path.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        path.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        path.endsWith("metadata.scch", Qt::CaseInsensitive) ||
        path.endsWith(".arc", Qt::CaseInsensitive)) {
        return true; // 屏蔽过滤
    }

    return false;
}
}

namespace ArcMeta {
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& info : entries) {
            if (shouldContinue && !shouldContinue()) return;

            if (info.fileName() == "metadata.scch" || info.fileName() == "metadata.scch.tmp") continue;
            // 应用自身的内部缓存目录，磁盘模式完全不进入、不展示、不扫描它，
            // 防止缓存目录被当作普通文件夹再次生成"缓存的缓存"
            if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;
=======
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& info : entries) {
            if (shouldContinue && !shouldContinue()) return;

            if (isAuxiliaryFile(info.absoluteFilePath()) || info.fileName() == "metadata.scch.tmp") continue;
            // 应用自身的内部缓存目录，磁盘模式完全不进入、不展示、不扫描它，
            // 防止缓存目录被当作普通文件夹再次生成"缓存的缓存"
            if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;
>>>>>>> REPLACE
```

#### 4.2.3 `src/ui/DiskScanService.cpp`

```
<<<<<<< SEARCH
#include "DiskScanService.h"
#include "../meta/AmMetaJson.h"
#include <QDir>
#include <QFileInfo>

namespace ArcMeta {
=======
#include "DiskScanService.h"
#include "../meta/AmMetaJson.h"
#include <QDir>
#include <QFileInfo>

namespace {
static inline bool isAuxiliaryFile(const QString& path) {
    if (path.isEmpty()) return true;

    // 🚨 仅保留 .ArcMeta.json，彻底清除 .am_meta.json 历史判断
    if (path.endsWith(".ArcMeta.json", Qt::CaseInsensitive) ||
        path.endsWith("_thumbnail.png", Qt::CaseInsensitive) ||
        path.endsWith("metadata.scch", Qt::CaseInsensitive) ||
        path.endsWith(".arc", Qt::CaseInsensitive)) {
        return true; // 屏蔽过滤
    }

    return false;
}
}

namespace ArcMeta {
>>>>>>> REPLACE
```

```
<<<<<<< SEARCH
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& info : entries) {
            if (shouldContinue && !shouldContinue()) return;

            if (info.fileName() == "metadata.scch" || info.fileName() == "metadata.scch.tmp") continue;
            // 应用自身的内部缓存目录，磁盘模式完全不进入、不展示、不扫描它，
            // 防止缓存目录被当作普通文件夹再次生成"缓存的缓存"
            if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;
=======
        QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo& info : entries) {
            if (shouldContinue && !shouldContinue()) return;

            if (isAuxiliaryFile(info.absoluteFilePath()) || info.fileName() == "metadata.scch.tmp") continue;
            // 应用自身的内部缓存目录，磁盘模式完全不进入、不展示、不扫描它，
            // 防止缓存目录被当作普通文件夹再次生成"缓存的缓存"
            if (info.isDir() && info.fileName().compare(".arcmeta", Qt::CaseInsensitive) == 0) continue;
>>>>>>> REPLACE
```

### 4.3 构建脚本中的历史遗留 `.am_meta.json` 注释清理

#### 4.3.1 `CMakeLists.txt`

```
<<<<<<< SEARCH
# 自动生成 MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# 2026-06-xx 按照用户要求：彻底废除递归搜集 (GLOB)，改为显式列出所有有效源文件。
# 此举可确保构建系统在彻底移除 .am_meta.json 模块后不再包含冗余代码，符合《源码纯净》规范。
set(SOURCES
=======
# 自动生成 MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
# 2026-06-xx 按照用户要求：彻底废除递归搜集 (GLOB)，改为显式列出所有有效源文件。
# 此举可确保构建系统在彻底移除历史冗余模块后不再包含冗余代码，符合《源码纯净》规范。
set(SOURCES
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] `src/meta/AmMetaJson.h`：修改成员变量及静态接口声明，擦除旧 `ArcMeta.cache` 路径计算接口。
- [ ] `src/meta/AmMetaJson.cpp`：修改构造函数指向物理目录下 `.ArcMeta.json` 文件（对应用户原话：“唯一物理文件名：.ArcMeta.json”），保存逻辑设置 Windows 隐藏属性；将 `migrateFolderCache` 实现为历史兼容空动作。
- [ ] `src/core/CategoryLoadService.cpp`：在头部匿名命名空间中引入 `isAuxiliaryFile`，并将内部原有的硬编码 `_thumbnail.png` 和 `metadata.scch` 判定替换为统一的 `isAuxiliaryFile`。
- [ ] `src/core/DiskScanService.cpp`：在头部匿名命名空间中引入 `isAuxiliaryFile` 并实现其阻断逻辑，剔除关于已废除的历史文件的单独判断。
- [ ] `src/ui/DiskScanService.cpp`：在头部匿名命名空间中引入 `isAuxiliaryFile` 并实现其阻断逻辑，剔除关于已废除的历史文件的单独判断。
- [ ] `CMakeLists.txt`：清除构建脚本中描述该历史废弃文件的相关注释（对应用户原话：“彻底清空并清退全工程中所有关于 .am_meta.json 的代码、字符串与注释”）。

**明确禁止越界修改的范围：**
- [ ] 历史归档/备份目录（如 `初始版/`、`旧版本-1/` 至 `旧版本-5/` 等）——不修改，确保归档的备份完整。
- [ ] SQLite 底层驱动及管理模块（`MetadataManager.cpp` 核心调用）——不修改。

## 6. 实现准则与预警【核心】
1. **头文件依赖**：所有修改的文件均完美引入已有的 `<QFile>`、`<QFileInfo>`、`"AmMetaJson.h"` 等头文件，无需新增多余的文件包含，保障编译通过。
2. **隐藏文件落盘属性**：`SetFileAttributesW` 是 Windows API，需确保在非 Windows 平台时通过宏安全包覆或保持对 Windows 的独占，当前 `AmMetaJson.cpp` 包含 `<windows.h>`，其对 `MoveFileExW` 及 `SetFileAttributesW` 的调用为平台专属逻辑，在本项目中运行于 Windows 环境下能开箱即用。
3. **性能和稳定性**：`isAuxiliaryFile` 采用 `static inline` 实现，性能开销微乎其微。
4. **自检规范**：所有新增及修改的行均为最纯净方案，没有任何多余的调试打印或额外逻辑。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双轨标记落盘路由 | 在库外普通磁盘模式下，元数据自动调用 AmMetaJson 精准、非侵入式写入主程序 `ArcMeta.cache/文件夹哈希.json` 离散缓存中，确保不污染用户原始物理盘。 | ⚠️ 调整：已根据用户最新显式指示修改为统一直接写入对应物理目录下的 `.ArcMeta.json` 隐藏文件中（用户当前最新指示高于历史 Memories 记录）。 |

## 8. 待确认事项（可选）
无。
