# 实施方案 - USN 实时感知与解析触发逻辑调试

## 1. 需求背景
用户反馈在托管库内新增/删减项目时，未观察到解析逻辑触发。需要通过在关键逻辑路径插入 `QMessageBox` 来确认：
1. 系统是否感知到了 USN 信号。
2. 信号是否通过了托管库路径过滤逻辑（`isUnderManagedLibrary`）。
3. 是否最终进入了元数据解析流水线（`registerItem`）。

## 2. 逻辑埋点方案

### 2.1 AutoImportManager::onEntryAdded (信号感知层)
- **位置**：在 `onEntryAdded` 异步任务开始处。
- **目的**：确认 `MftReader` 是否发出了信号，以及 `AutoImportManager` 是否接收到。
- **信息**：显示感知到的文件路径。

### 2.2 AutoImportManager::onEntryAdded (过滤决策层)
- **位置**：在 `isUnderManagedLibrary(key)` 返回 `true` 之后。
- **目的**：确认路径过滤逻辑是否正确识别了托管库。
- **信息**：显示“已通过托管库过滤”。

### 2.3 MetadataManager::registerItem (业务解析层)
- **位置**：在 `registerItem` 函数入口。
- **目的**：确认去抖动（Debounce）之后，解析任务是否真正开始执行。
- **信息**：显示“开始执行解析流水线”。

## 3. 技术实现细节
- **线程安全**：由于上述逻辑均运行在异步线程池（`QtConcurrent`），必须使用 `QMetaObject::invokeMethod` 切换到主线程显示 `QMessageBox`。
- **头文件依赖**：需在 `AutoImportManager.cpp` 和 `MetadataManager.cpp` 中包含 `<QMessageBox>`。

## 4. 风险控制
- 避免在循环内触发过多弹窗导致界面假死（仅用于小规模调试）。
- 确保不破坏现有的 `isInternalOperating` 锁逻辑和事务逻辑。
