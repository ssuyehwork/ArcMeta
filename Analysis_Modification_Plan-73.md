# Analysis and Modification Plan - 73

## 1. 需求背景
用户要求彻底废除基于 SQLite 数据库的分类与元数据管理模式，转而全面采用二进制 `.scch` 格式。具体要求包括：
- 移除侧边栏分类标题上方的模式切换按钮（`m_btnSwitch`）。
- 移除所有与“DB 类数据库”相关的代码逻辑。
- 确保系统从现在起仅使用 `.scch` 格式。

## 2. 影响范围
- **UI 层**: `src/ui/CategoryPanel.cpp`, `src/ui/CategoryPanel.h`
- **持久层**: `src/db/CategoryRepo.cpp`, `src/db/CategoryRepo.h`
- **元数据管理**: `src/meta/MetadataManager.cpp`, `src/meta/MetadataManager.h`
- **配置**: `AppConfig` 中的 `Category/IsScchMode` 设置。

## 3. 修改方案

### 3.1 UI 层修改 (`CategoryPanel`)
1. **移除组件**: 从 `CategoryPanel.h` 中移除 `m_btnSwitch` 成员变量声明。
2. **清理 `initUi`**:
   - 移除 `m_btnSwitch` 的创建、样式设置、事件过滤器安装。
   - 移除 `m_btnSwitch` 的点击信号连接逻辑。
3. **清理构造函数**:
   - 移除从 `AppConfig` 读取 `IsScchMode` 的逻辑。
   - 强制调用 `CategoryRepo::setScchMode(true)`。
   - 强制调用 `MetadataManager::instance().initFromScchMode()`。
4. **清理事件过滤器**: 在 `eventFilter` 中移除对 `m_btnSwitch` 相关悬浮事件的处理逻辑。

### 3.2 持久层修改 (`CategoryRepo`)
1. **强制 SCCH 模式**:
   - 将 `m_isScchMode` 默认值设为 `true`。
   - 在所有公开接口（`add`, `update`, `remove`, `getAll` 等）中移除对 `m_isScchMode` 的判断，直接调用 `ScchCategoryEngine` 的实现。
   - 考虑移除 SQLite 相关的写入/读取代码（保留 `syncDatabaseAndScch` 可能用于迁移，或者根据彻底移除的要求将其清理）。
   - *注意*: 用户要求彻底移除 DB 逻辑，因此应删除接口内部的 SQLite 操作。

### 3.3 元数据管理修改 (`MetadataManager`)
1. **移除数据库初始化**:
   - 移除 `initFromDatabase` 的具体实现或将其设为私有/弃用。
   - 确保 `initFromScchMode` 是唯一的入口。
2. **清理 `persistAsync`**:
   - 移除 `!CategoryRepo::isScchMode()` 的判断块，不再同步到 `ItemRepo` / `FolderRepo` 的 SQLite 表。

### 3.4 清理冗余代码
1. 检查 `SyncEngine` 和 `CategoryRepo::syncDatabaseAndScch`，如果不再需要与数据库同步，则进行相应简化。

## 4. 验证计划
1. **编译验证**: 确保移除相关代码后项目能正常编译。
2. **UI 验证**: 检查侧边栏，确认切换按钮已消失，UI 布局保持正常。
3. **功能验证**: 
   - 创建、重命名、删除分类，检查 `arcmeta_categories.scch` 是否正常更新。
   - 修改元数据（评分、颜色、标签），检查 `metadata.scch` 是否正常更新。
   - 重启程序，确保数据从 `.scch` 正确加载。
