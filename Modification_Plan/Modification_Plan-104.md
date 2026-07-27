# 监控文件夹失效后盘符及残留清退自愈方案 —— Modification_Plan-104.md

> 状态：已批准，执行中

## 1. 任务背景
在监控系统的物理对账过程中，当用户通过第三方软件（如资源管理器、其他文件工具）把已加入自动监控的自定义文件夹移走、删除或重命名时，该路径事实上已经失效。

然而，现有架构中存在关键的联动断层，缺乏物理存在性的自愈感知。这使得：
1. 底层已失效的监控无法安全解除。
2. 盘符栏按钮（FolderButton）在启动、刷新或检测到变动时，依然顽固地停留在 UI 上，无法被自动销毁。
3. 数据库和左侧“我的分类”中自动生成的 1:1 镜像分类树中留留下大量失效、残留的僵尸元数据记录，破坏了系统的整洁。

本重构方案将对 `MainWindow.cpp` 的加载校验、信号响应点、以及 `NativeFolderWatcher` 变动信号范围进行联动式安全加固。

## 2. 问题定位
造成本物理脱节现象的核心根源如下：
1. **盘符栏加载缺乏物理磁盘校验**：在 `MainWindow::updateCustomFolderButtons()` 中，代码在读取配置 `DriveBar/CustomMonitoredFolders` 时，只是盲目循环并将字符串组装成 FolderButton 去显示，中间完全没有任何 `QDir::exists()` 物理校验，造成外部失效的文件夹无法在界面刷新或启动时被排除剔除。
2. **监控事件未向主界面抛出自愈通知**：当外部移走目录时，`NativeFolderWatcher::handleNotification` 在 `Action == FILE_ACTION_REMOVED` 中，只针对包含 `ArcMeta.Library_` 的托管库文件夹向外发送 `managedFolderRemoved`，完全遗漏了用户手动添加的自定义导入目录，这使得 `MainWindow` 无法实时收到并响应来自监视器的销毁信号。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 自动解绑监控 | 硬盘失效或删除时，实时调用 `NativeFolderWatcher::instance().removeWatch()` 销毁监控。 | ✅ |
| 2    | 自动清理 UI | 当磁盘失效或收到移除通知时，物理销毁 `DriveBar` 上的失效 FolderButton，并清理 AppConfig 的配置列表。 | ✅ |
| 3    | 自动数据库除账与侧边栏 1:1 镜像分类树销毁 | 联动调用 `MetadataManager::instance().removeMetadataSync()` 清除数据库下全部子项元数据，同时在 Category 存储中删除对应的分类树节点。 | ✅ |
| 4    | 实时物理自愈，不残留废弃数据 | 结合启动检查（`updateCustomFolderButtons`）与事件即时通信（`managedFolderRemoved`），提供双向闭环。 | ✅ |

## 4. 详细解决方案

### 4.1 加强 `MainWindow::updateCustomFolderButtons` 物理自愈检验（解决缺陷 1）
- **引入物理存在性校验**：
  在循环加载 `customFolders` 时，使用 `QDir(finalPath).exists()` 实时侦测：
  ```cpp
  // 🚨 核心自愈逻辑：如果在物理磁盘上文件夹已经不存在（被移走或删除），自动触发数据清洗与自动解绑！
  if (!QDir(finalPath).exists()) {
      qDebug() << "[DriveBar] 检测到监控文件夹在硬盘上已失效，自动清退:" << finalPath;
      
      // 1. 从 NativeFolderWatcher 监控中注销此路径
      NativeFolderWatcher::instance().removeWatch(normPath);

      // 2. 彻底清洗数据库元数据与侧边栏镜像分类
      MetadataManager::instance().removeMetadataSync(normPath);

      // 3. 清除相关图标与颜色配置
      AppConfig::instance().setValue(QString("DriveBar/FolderColor_%1").arg(path), QVariant());
      AppConfig::instance().setValue(QString("DriveBar/FolderIcon_%1").arg(path), QVariant());
      AppConfig::instance().setValue(QString("DriveBar/FolderColor_%1").arg(finalPath), QVariant());
      AppConfig::instance().setValue(QString("DriveBar/FolderIcon_%1").arg(finalPath), QVariant());
      
      hasInvalid = true;
      continue; // 跳过，不进行 UI 按钮绘制
  }
  ```
- **配置回写与同步**：
  如果在启动或刷新中发现了失效路径，将过滤后真实的 `validFolders` 一并回写到 `DriveBar/CustomMonitoredFolders` 配置项中，并调用 `AppConfig::instance().sync()` 安全同步落盘，确保配置的绝对纯净。

### 4.2 扩展 `NativeFolderWatcher` 物理删除信号范围（解决缺陷 2）
- **解除过激的 `ArcMeta.Library_` 字符串拦截**：
  在 `NativeFolderWatcher.cpp` 的 `handleNotification` 中，当收到 `FILE_ACTION_REMOVED` 时，不再将其狭隘地限制在默认托管库文件夹范围内。
- **信号通用化抛出**：
  只要是被移除的目录，均向外抛出 `managedFolderRemoved` 通告。这样，不论是官方托管路径、还是自定义导入的磁盘路径被移动或删除，外层界面及关联业务层都能完整收到通告通知并即时自愈。

### 4.3 修正 `MainWindow::initDriveBar` 信号监听与自动注销联动（解决缺陷 2）
- **扩展信号监听逻辑**：
  在 `initDriveBar()` 响应 `managedFolderRemoved` 的 lambda 中，先拉取 `DriveBar/CustomMonitoredFolders` 配置列表。
- **自定义路径自动清退逻辑**：
  ```cpp
  // 连接文件监视信号，当发现第三方删除/移走监控文件夹时自动清退 UI 与底层数据库
  connect(&NativeFolderWatcher::instance(), &NativeFolderWatcher::managedFolderRemoved, this, [this](const std::wstring& path) {
      QString qPath = QString::fromStdWString(path);
      std::wstring normPath = MetadataManager::normalizePath(path);
      QString finalPath = QString::fromStdWString(normPath);

      QStringList customFolders = AppConfig::instance().getValue("DriveBar/CustomMonitoredFolders").toStringList();

      // 如果被移动/删除的是自定义监控文件夹，自动触发注销逻辑
      if (customFolders.contains(finalPath) || customFolders.contains(qPath)) {
          removeCustomMonitoredFolder(qPath);
      } else {
          // 默认托管库清退逻辑
          QFileInfo info(qPath);
          QString letter = info.absolutePath().left(2).toUpper(); // "D:"
          if (m_driveButtons.contains(letter)) {
              m_driveButtons[letter]->setState(DriveButton::Inactive);
          }
          MetadataManager::instance().removeMetadataSync(path);
      }
  });
  ```
- 当用户在外部直接移走、重命名该自定义文件夹时，`removeCustomMonitoredFolder(qPath)` 被安全触发，其内部不仅会安全清理 UI 并移除 Native 监控，而且会调用 `MetadataManager::instance().removeMetadataSync`，彻底清理数据库里原路径下的全部僵尸元数据、并清洗在左侧生成的 1:1 分类树定义，绝不残留。

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/MainWindow.cpp` （重构 `updateCustomFolderButtons` 的校验加载逻辑，以及 `initDriveBar` 的监视器信号接收 lambda 事务，完美对齐物理自愈流程）
- [ ] 模块/文件：`src/core/NativeFolderWatcher.cpp` （优化 `handleNotification` 在 `Action == FILE_ACTION_REMOVED` 时仅发送 `ArcMeta.Library_` 的空限制，使其向外通知通用路径移除）

**明确禁止越界修改的范围：**
- [ ] 外部 `FolderButton` 的自定义绘制底层逻辑——不修改
- [ ] `MetadataManager` 与 `CategoryRepo` 在清理数据时的核心磁盘删除实现——不修改

## 6. 实现准则与预警【核心】
1. **依赖头文件**：`MainWindow.cpp` 已拥有 `<QFileInfo>`、`<QDir>` 以及 `MetadataManager.h` 依赖，重构时需直接调用 `normalizePath` 进行精准路径统一，防止因双斜杠或大小写未对齐引发自愈失效。
2. **持久化与重绘机制**：清退逻辑必须以“数据 -> UI”单向同步的形式运转。即通过修改 AppConfig 及元数据镜像后，主动调用 `updateCustomFolderButtons()` 进行销毁重绘，禁止在不更新 AppConfig 或不解绑监控的情况下手动从 layout 里面 `delete` 控件，防止下次重启时状态发生反弹。
3. **1:1 镜像分类树自动清除**：由于镜像分类是通过其绑定的物理路径与 `categories` 关联的，调用 `removeMetadataSync` 还会安全联动删除其子节点和空节点，所以无需在 UI 层面再写一套手动的分类层级销毁代码，利用底层对账机制完成 100% 自动安全落盘与同步，保持开箱即用。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 路径标准化  | 路径拼合和处理一律使用标准化规范，避免在后续对账或路径比对时发生大小写或斜杠不一致问题。 | ✅ 方案中使用 `MetadataManager::normalizePath` 进行转换，完全统一为原生标准化 wstring，避免在包含、匹配判断时由于非标路径导致校验错误。 |
| 多线程安全与 UI 线程重载 | UI 界面的创建与销毁、控件重建必须在主线程中执行，严禁从异步工作线程直接操作。 | ✅ 方案中的 QDir::exists 检测和按钮 `deleteLater()` 重建操作完全部署在 `MainWindow` 主线程的 UI 渲染流程和 QMetaObject::invokeMethod 异步回传中，确保极其安全的多线程生命期体验。 |

## 8. 待确认事项（可选）
- 暂无
