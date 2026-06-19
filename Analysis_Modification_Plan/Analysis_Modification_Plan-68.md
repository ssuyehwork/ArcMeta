# 盘符管理栏与多通道数据库挂载设计 —— Analysis_Modification_Plan-68.md

## 1. 任务背景
在“一硬盘一数据库”的去中心化架构下，随着用户外接磁盘的增多，系统需要提供直观的盘符挂载界面以支持多库并存与按需加载。用户期望在主窗口标题栏下方新增可展开/折叠的“盘符管理栏”，支持右键设定该盘对应的“托管文件夹”，并规范化数据库挂载到内存的三种通道触发流程，同时实现侧边栏计数的实时更新。

## 2. 问题定位
- **现有机制局限性**：
  1. 目前主窗口 `MainWindow` 在自定义布局中没有预留盘符展示容器。
  2. 数据库加载（`DatabaseManager::instance().getMemoryDb`）缺乏上层多通道调度接口。
  3. 缺乏盘符级别的右键上下文菜单，无法绑定各盘专属的托管文件夹相对路径配置。
- **关联文件**：
  - `src/ui/MainWindow.h` / `src/ui/MainWindow.cpp`
  - `src/meta/DatabaseManager.h`
  - `src/meta/MetadataManager.h`
  - `src/core/AutoImportManager.h` / `src/core/AutoImportManager.cpp`

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 标题栏新增按钮控制折叠 | `MainWindow` 标题栏新增 `m_btnToggleDrives` 按钮 | ✅ |
| 2    | 盘符按钮右键设置托管文件夹 | 盘符按钮绑定右键菜单，通过 `AppConfig` 存储相对路径 | ✅ |
| 3    | 方式 1：启动自动加载 | 初始化时读取 `Drives/ActiveDrives` 并挂载 | ✅ |
| 4    | 方式 2：按需手动加载 | 盘符按钮点击时触发数据库挂载与索引更新 | ✅ |
| 5    | 方式 3：托管文件夹变动加载 | `AutoImportManager` 变动时静默加载数据库 | ✅ |
| 6    | 挂载后侧边栏计数改变 | 调用 `notifyCategoryCountChanged` 触发刷新 | ✅ |

## 4. 详细解决方案

### 4.1 UI 布局重构设计 (`MainWindow`)
在主布局 `centralC` 的垂直主布局 `mainL` 中插入盘符管理栏 `m_driveBarWidget`：
```cpp
mainL->addWidget(m_titleBarWidget);  // 1. 标题栏
mainL->addWidget(m_driveBarWidget);  // 2. 盘符管理栏 (45px, 默认隐藏)
mainL->addWidget(m_navBarWidget);    // 3. 导航栏
mainL->addWidget(bodyWrapper, 1);    // 4. 核心拆分容器
mainL->addWidget(statusBar);          // 5. 状态栏
```

### 4.2 磁盘探测与初始化
使用 `GetLogicalDrives` 结合 `GetVolumeInformationW` 探测所有 NTFS 分区。通过 `QPushButton` 实现 Checkable 状态，按钮外观遵循扁平化规范。

### 4.3 数据库多通道挂载逻辑

#### 通道 1：启动自动加载
读取 `AppConfig` 中 `Drives/ActiveDrives` 列表。
对于列表中的每个盘符，通过卷序列号调用 `DatabaseManager::getMemoryDb` 预加载。

#### 通道 2：按需手动加载
用户点击盘符按钮时：
- **勾选**：调用 `getMemoryDb` 挂载，并更新 `MftReader` 的活动盘符过滤掩码。
- **取消勾选**：卸载元数据名称索引 (`unloadVolumeNameCache`)，更新 `MftReader` 掩码。注意：需确保至少保留一个活动盘符。

#### 通道 3：托管文件夹变动自动加载
由 `AutoImportManager` 在入库处理前静默执行 `getMemoryDb(volSerial)`，确保即便该盘未在 UI 上激活，其变更也能被正确记入数据库。

### 4.4 托管文件夹设置
盘符按钮右键弹出“设置托管文件夹...”，选择目录后，系统自动计算其相对于根目录的相对路径，并以 `ManagedFolder/Volume_<卷序列号>` 为键存入配置。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- `src/ui/MainWindow.h/cpp`：布局调整与按钮逻辑。
- `src/core/AutoImportManager.h/cpp`：引入自动监听与静默挂载。

**明确禁止越界修改的范围：**
- 禁止修改 `DatabaseManager` 的底层连接池逻辑。
- 禁止修改 `CategoryPanel` 内部的绘制代码。

## 6. 实现准则与预警【核心】
1. **安全性**：卸载盘符前必须先更新 `MftReader` 掩码，防止多线程检索失效路径。
2. **容错性**：托管文件夹路径必须经过 `normalizePath` 处理，并校验盘符所属权。
3. **性能**：大量磁盘扫描应放在异步线程处理（如 `refreshDriveList`）。

## 7. Memories.md 合规检查
符合 UI 固定高度（34px）、清理按钮启用（setClearButtonEnabled）及范围感知搜索（updateActiveDrives 联动）等规范。
