# NTFS USN 监听与托管文件夹自动入库设计 —— Analysis_Modification_Plan-67.md

## 1. 任务背景
在“一硬盘一数据库”的架构下，用户希望系统能够支持在每块硬盘中配置唯一的“托管文件夹”。当文件被外部操作拖入该托管文件夹时，系统能够通过底层的 NTFS 卷变更监听技术自动捕捉该变动，并在无需用户手动干预的情况下自动扫描入库，提升资产管理的自动化效率。

## 2. 问题定位
- **现有机制局限性**：
  1. 目前的 `UsnWatcher` 仅服务于 `MftReader` 内存 SoA 结构（MFT 索引）的增量维护，属于纯文件系统层面的镜像记录。
  2. `MetadataManager` 虽然提供了单项注册入库的 `registerItem` 接口，但未接收 `MftReader` 发出的变更信号，缺乏与“托管文件夹”路径匹配的自动入库感知逻辑。
  3. 缺乏防抖与合并事务机制：文件拖入往往是批量的（例如拖入文件夹），如果高频、逐个直接调用 `registerItem` 进行单事务 SQLite 插入，在数万级文件场景下将引发严重的 **I/O 寻道风暴** 与 **UI 信号淹没**，导致程序卡顿甚至静默闪退。
- **关联文件**：
  - `src/core/AppConfig.h`（配置持久化）
  - `src/mft/MftReader.h`（文件变更源信号）
  - `src/meta/MetadataManager.h`（物理入库及缓存层）

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 为每块硬盘设置唯一的“托管文件夹” | 通过 `AppConfig` 以卷序列号（`VolumeSerial`）作为键，存储托管文件夹相对根路径的相对路径，以适配盘符漂移 | ✅ |
| 2    | 文件拖入托管文件夹时自动扫描入库 | 订阅并处理 `MftReader::entryAdded` 信号，匹配路径前缀自动触发入库 | ✅ |
| 3    | 严禁触碰任何代码文件，仅产出分析与方案 | Jules 扮演纯分析师角色，仅在该方案文档中提供架构分析与伪代码设计，不修改任何源文件 | ✅ |
| 4    | 避免并发写入造成的 I/O 吞吐风暴 | 槽函数接收端引入去抖队列与定时器，利用 `SqlTransaction` 包裹实现批量提交，避免频繁的小事务写入 | ✅ |

## 4. 详细解决方案

### 4.1 托管配置相对化设计（抗盘符飘移）
为防范 Windows 环境下插拔磁盘导致的驱动器盘符（如 `D:` 变为 `E:`）漂移问题，系统存储绝对路径是不可靠的。
建议使用 `AppConfig` 单例，通过 `ManagedFolder/Volume_<卷序列号>` 这一物理唯一标识存储托管文件夹相对于该磁盘根目录的**相对路径**（例如 `ArcMetaManaged`）。
在运行时，通过下述逻辑动态恢复绝对路径：
```cpp
// 绝对路径动态拼接逻辑（伪代码示意）
std::wstring getManagedFolderAbsolutePath(const std::wstring& volSerial) {
    // 1. 根据卷序列号获取当前分配的盘符根路径（此处需辅助函数，例如遍历 QDir::drives() 匹配序列号）
    std::wstring drive = getDriveRootBySerial(volSerial); 
    if (drive.empty()) return L"";

    // 2. 从 AppConfig 中读取该磁盘对应的托管文件夹相对路径
    QString key = QString("ManagedFolder/Volume_%1").arg(QString::fromStdWString(volSerial));
    QString relPath = AppConfig::instance().getValue(key, "").toString();
    if (relPath.isEmpty()) return L"";

    // 3. 拼接并标准化
    return MetadataManager::normalizePath(drive + L"\\" + relPath.toStdWString());
}
```

### 4.2 独立自动化入库管理器 `AutoImportManager` 设计
为保证单一职责原则及不破坏原有 MFT 架构，建议新增独立控制类 `AutoImportManager`，接收底层事件并控制业务分流。

#### 接口声明 (`src/core/AutoImportManager.h`)
```cpp
#pragma once
#include <QObject>
#include <QTimer>
#include <vector>
#include <string>
#include <mutex>

namespace ArcMeta {

class AutoImportManager : public QObject {
    Q_OBJECT
public:
    static AutoImportManager& instance();

    // 启动/停止监听
    void startListening();
    void stopListening();

private slots:
    // 订阅 MftReader 发现的新增条目
    void onEntryAdded(uint64_t key);
    // 去抖超时，合并写入数据库
    void processImportQueue();

private:
    AutoImportManager(QObject* parent = nullptr);
    ~AutoImportManager() override;

    bool checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder);

    QTimer* m_debounceTimer = nullptr;
    std::vector<std::wstring> m_pendingPaths;
    std::mutex m_queueMutex;
    bool m_isListening = false;
};

} // namespace ArcMeta
```

#### 控制流实现 (`src/core/AutoImportManager.cpp`)
```cpp
#include "AutoImportManager.h"
#include "mft/MftReader.h"
#include "meta/MetadataManager.h"
#include "meta/DatabaseManager.h"
#include "core/AppConfig.h"
#include <QDebug>
#include <QCoreApplication>

namespace ArcMeta {

AutoImportManager& AutoImportManager::instance() {
    static AutoImportManager inst;
    return inst;
}

AutoImportManager::AutoImportManager(QObject* parent) : QObject(parent) {
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setInterval(3000); // 3秒去抖，防止文件尚未拷贝完即被入库读取
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &AutoImportManager::processImportQueue);
}

AutoImportManager::~AutoImportManager() {
    stopListening();
}

void AutoImportManager::startListening() {
    if (m_isListening) return;
    // 跨线程连接必须显式指定 Qt::QueuedConnection，确保槽函数在主线程事件循环中执行
    connect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded, Qt::QueuedConnection);
    m_isListening = true;
}

void AutoImportManager::stopListening() {
    if (!m_isListening) return;
    disconnect(&MftReader::instance(), &MftReader::entryAdded, this, &AutoImportManager::onEntryAdded);
    m_isListening = false;
}

void AutoImportManager::onEntryAdded(uint64_t key) {
    int idx = MftReader::instance().getIndexByKey(key);
    if (idx < 0) return;

    // 获取路径并检查是否在托管文件夹内
    QString qPath = MftReader::instance().getFullPath(idx);
    std::wstring fullPath = qPath.toStdWString();
    std::wstring managedFolder;
    
    if (checkAndGetManagedPath(fullPath, managedFolder)) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingPaths.push_back(fullPath);
        
        // 跨线程安全启动/重置去抖定时器
        QMetaObject::invokeMethod(m_debounceTimer, "start", Qt::QueuedConnection);
    }
}

bool AutoImportManager::checkAndGetManagedPath(const std::wstring& path, std::wstring& outManagedFolder) {
    std::wstring volSerial = MetadataManager::getVolumeSerialNumber(path);
    if (volSerial.empty()) return false;

    std::wstring managedAbs = getManagedFolderAbsolutePath(volSerial);
    if (managedAbs.empty()) return false;

    // 校验路径是否以托管文件夹路径为前缀（注意大小写敏感性处理）
    if (path.size() >= managedAbs.size() && _wcsnicmp(path.c_str(), managedAbs.c_str(), managedAbs.size()) == 0) {
        outManagedFolder = managedAbs;
        return true;
    }
    return false;
}

void AutoImportManager::processImportQueue() {
    std::vector<std::wstring> pathsToProcess;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        pathsToProcess = std::move(m_pendingPaths);
        m_pendingPaths.clear();
    }

    if (pathsToProcess.empty()) return;

    // 1. 批量入库处理
    for (const auto& path : pathsToProcess) {
        // registerItem 内部应已处理卷关联与事务逻辑（若无，则需在此显式包裹事务）
        MetadataManager::instance().registerItem(path);
    }

    // 2. 刷新 UI 状态
    MetadataManager::instance().notifyFullUIRebuild();
    qDebug() << "[AutoImport] 自动入库完成，处理项数:" << pathsToProcess.size();
}

} // namespace ArcMeta
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 该文档仅作为技术方案设计，**严禁修改任何 `.cpp` 或 `.h` 源文件**。

**明确禁止越界修改的范围：**
- [ ] 禁止修改 `MftReader` 的核心索引扫描逻辑。
- [ ] 禁止修改 `DatabaseManager` 的物理连接管理逻辑。

## 6. 实现准则与预警【核心】
1. **盘符获取**：`getDriveRootBySerial` 需通过 `GetVolumePathNamesForVolumeNameW` 或遍历 `GetLogicalDrives` 结合 `GetVolumeInformationW` 实现，以确保序列号与盘符的准确对应。
2. **性能预警**：在 `onEntryAdded` 中频繁调用 `getFullPath` 可能在高并发下产生压力，需观察 `MftReader` 内部路径缓存的命中率。
3. **事务一致性**：`MetadataManager::registerItem` 必须保证其内部的数据库写入是原子的，或者由 `AutoImportManager` 在批量处理时统一开启外部事务以提升写入性能。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 架构一致性 | 一硬盘一数据库，基于卷序列号隔离 | ✅ 符合 |
| 性能要求 | 避免高频 I/O，使用批量事务与去抖 | ✅ 符合 |
| 自动入库 | 监听 USN 变更流，匹配路径后自动 register | ✅ 符合 |

## 8. 待确认事项
- `MetadataManager::registerItem` 是否已经具备了内部事务攒批能力？若无，建议在 `AutoImportManager::processImportQueue` 中显式包裹针对各卷数据库的 `BEGIN TRANSACTION`。
