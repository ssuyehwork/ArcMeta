# 异步高性能恢复文件夹登记进度环 —— implementation_plan.md

## 1. 目标与背景

由于之前的同步深度递归扫描严重影响了内容面板的载入性能，上一代优化中彻底移除了文件夹已登记进度（`registrationProgress`）的计算与赋值逻辑。这导致 `ItemRecord::registrationProgress` 始终保持其初始默认值 `-1.0`，从而阻断了绘制蓝色百分比圆圈的路径，使其长久失效。

为了重新恢复这套进度环绘制，且确保大文件夹加载体验流畅（不出现卡死或界面空白），本方案提供一套 **“异步单线程队列扫描 + 局部增量刷新 + QDirIterator 高效迭代”** 的惰性加载策略。

---

## 2. 方案详细设计

### 2.1 物理防御：单线程池队列

计算文件夹的递归已登记文件比例涉及磁盘 I/O。为防止并发多个文件夹同时进行深度递归扫描时造成机械硬盘等存储设备的寻道磁头冲突、恶化整体 I/O 吞吐率，本方案将：
- 在 `ContentPanel` 内部初始化一个专属的 `QThreadPool m_progressThreadPool`，并强行将其 `maxThreadCount` 设为 `1`。
- 这样，所有的文件夹进度计算任务将以排队的形式串行执行，完全避免磁盘抢占。

### 2.2 计算逻辑升级：QDirIterator 代替手写递归

手写 `QDir::entryInfoList` 递归会因为实例化过多 `QFileInfoList` 导致大量的堆内存分配，且在深层目录下面临爆栈的隐患。
我们使用底层的 `QDirIterator it(..., QDirIterator::Subdirectories)`，在子线程中以极高速度进行子条目流式枚举，比常规递归速度快数倍，极度轻量。

---

## 3. 拟做修改的文件

### 3.1 [MODIFY] [ModelContract.h](file:///g:/C++/ArcMeta/ArcMeta/src/core/ModelContract.h)
确认 `RegistrationProgressRole` 是否已经定义（先前已验证，已存在且为 `Qt::UserRole + 205`，本文件无需修改，保持原样）。

### 3.2 [MODIFY] [ContentPanel.h](file:///g:/C++/ArcMeta/ArcMeta/src/ui/ContentPanel.h)

1. 在 `FerrexVirtualDbModel` 类定义中，暴露更新进度的公共接口：
   ```cpp
   /**
    * @brief 2026-06-27 高性能增量刷新：主线程更新特定路径文件夹的登记进度并触发局部刷新
    */
   void updateRegistrationProgress(const QString& path, double progress);
   ```

2. 在 `ContentPanel` 类定义中，加入如下私有成员变量及辅助函数：
   ```cpp
   // 头文件引用
   #include <QThreadPool>
   #include <QMutex>

   // 在 ContentPanel 的 private 区域
   QThreadPool m_progressThreadPool;       // 进度计算专用线程池
   QSet<QString> m_activeCalculations;     // 当前正在计算的路径集合
   QMutex m_calcMutex;                     // 用于保护 m_activeCalculations 的互斥锁

   void startAsyncProgressCalculation();   // 启动异步计算任务
   double calculateFolderProgressInternal(const QString& folderPath); // 底层扫描计算
   ```

### 3.3 [MODIFY] [ContentPanel.cpp](file:///g:/C++/ArcMeta/ArcMeta/src/ui/ContentPanel.cpp)

#### 1. 初始化与反初始化

在 `ContentPanel::ContentPanel` 构造函数中初始化线程池：
```cpp
m_progressThreadPool.setMaxThreadCount(1); // 物理防御：串行 I/O 任务
```

在 `ContentPanel::~ContentPanel` 析构函数中等待线程安全结束：
```cpp
m_progressThreadPool.waitForDone();
```

#### 2. 添加模型更新接口实现
```cpp
void FerrexVirtualDbModel::updateRegistrationProgress(const QString& path, double progress) {
    auto it = m_pathToIndex.find(path);
    if (it != m_pathToIndex.end()) {
        int row = it->second;
        m_allRecords[row].registrationProgress = progress;
        // 触发局部重绘
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {RegistrationProgressRole});
    }
}
```

#### 3. 异步任务触发与计算实现
```cpp
#include <QDirIterator>

void ContentPanel::startAsyncProgressCalculation() {
    int reqId = m_loadRequestId.load();
    std::vector<QString> dirsToCalculate;

    // 主线程只读遍历
    for (const auto& r : m_model->allRecords()) {
        // 仅处理真实的文件夹，且之前没有计算过进度（初始值为 -1.0）
        if (r.isDir && !r.isCategory && r.registrationProgress < -0.5) {
            dirsToCalculate.push_back(r.path);
        }
    }

    if (dirsToCalculate.empty()) return;

    for (const auto& dirPath : dirsToCalculate) {
        {
            QMutexLocker lock(&m_calcMutex);
            if (m_activeCalculations.contains(dirPath)) continue;
            m_activeCalculations.insert(dirPath);
        }

        // 使用 QThreadPool 异步提交
        m_progressThreadPool.start([this, dirPath, reqId]() {
            double progress = calculateFolderProgressInternal(dirPath);

            // 回调主线程
            QMetaObject::invokeMethod(this, [this, dirPath, progress, reqId]() {
                // 检查请求 ID 是否一致，防止由于用户切换了文件夹导致过期回调错误写入
                if (reqId == m_loadRequestId.load()) {
                    m_model->updateRegistrationProgress(dirPath, progress);
                }
                
                QMutexLocker lock(&m_calcMutex);
                m_activeCalculations.remove(dirPath);
            }, Qt::QueuedConnection);
        });
    }
}

double ContentPanel::calculateFolderProgressInternal(const QString& folderPath) {
    long long totalCount = 0;
    long long managedCount = 0;

    // 高效底层的扫描器：仅枚举文件与文件夹，使用 Subdirectories 递归
    QDirIterator it(folderPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString childPath = it.next();
        totalCount++;

        // 访问元数据读锁缓存
        if (MetadataManager::instance().getMeta(childPath.toStdWString()).hasUserOperations()) {
            managedCount++;
        }
    }

    return (totalCount == 0) ? 0.0 : (double)managedCount / totalCount;
}
```

#### 4. 挂载触发点
在所有模型加载完毕并设置记录后，触发异步任务：
- 在 `loadDirectory` 完成的 lambda 内：
  ```cpp
  // 约 L2378 行，设置完模型记录后
  m_model->setRecords(allItems);
  m_proxyModel->sort(0);
  startAsyncProgressCalculation(); // 触发异步进度扫描
  ```
- 在 `loadPaths` 完成的 lambda 内：
  ```cpp
  // 约 L2689 行
  m_model->setRecords(newRecords);
  startAsyncProgressCalculation(); // 触发异步进度扫描
  ```
- 在 `loadCategory` 完毕后：
  ```cpp
  // 约 L2578 行
  m_model->setRecords(allRecords);
  m_proxyModel->sort(0);
  startAsyncProgressCalculation(); // 触发异步进度扫描
  ```

---

## 4. 验证计划

1. **编译确认**：检查项目在 CMake 环境下无任何新增文件编译错误。
2. **性能与流畅度验证**：
   - 打开包含超过 1,000 个文件夹的目录（例如大分区的根目录），验证界面渲染依旧秒级完成，无任何卡顿。
   - 验证右上角的进度弧线会在后台计算完毕后自然“浮现”在每个文件夹卡片上。
   - 悬停在进度弧上，验证 ToolTip 是否能正确显式显示 `登记进度: XX%`。
3. **数据一致性验证**：
   - 在文件夹间切换，验证前一个文件夹的计算队列在切走时被抛弃，新文件夹的计算逻辑准确执行，进度条不出现混乱。
