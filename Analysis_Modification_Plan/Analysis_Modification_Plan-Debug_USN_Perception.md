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

## 3. 技术实施细节 (Git Merge Diff 预览)

### 3.1 AutoImportManager.cpp 埋点
```cpp
// 引入头文件
#include <QMessageBox>
#include <QApplication>

// onEntryAdded 逻辑增强
void AutoImportManager::onEntryAdded(uint64_t key) {
    (void)QtConcurrent::run([this, key]() {
        // [调试埋点 A] 确认信号到达
        QMetaObject::invokeMethod(qApp, [key]() {
            QMessageBox::information(nullptr, "Debug [A]", QString("USN 感知到物理变动\nKey: %1").arg(key));
        });

        if (!isUnderManagedLibrary(key)) return;

        // [调试埋点 B] 确认通过过滤
        QMetaObject::invokeMethod(qApp, [key]() {
            QMessageBox::information(nullptr, "Debug [B]", QString("过滤通过：该项属于托管库范围"));
        });
        // ...
    });
}
```

### 3.2 MetadataManager.cpp 埋点
```cpp
// 引入头文件
#include <QMessageBox>

// registerItem 逻辑增强
void MetadataManager::registerItem(const std::wstring& path, bool authorized) {
    // ... 指纹校验后 ...
    // [调试埋点 C] 确认解析点火
    QMetaObject::invokeMethod(QCoreApplication::instance(), [nPath]() {
        QMessageBox::information(nullptr, "Debug [C]", QString("解析流水线点火\n路径: %1").arg(QString::fromStdWString(nPath)));
    });
    // ...
}
```

## 4. 风险控制
- **线程安全**：通过 `QMetaObject::invokeMethod` 确保 UI 操作回到 GUI 主线程，防止跨线程调用导致的段错误。
- **性能影响**：`QMessageBox` 是模态阻塞的，会暂停当前逻辑。建议仅在测试单体文件增删时开启。
- **不脑补原则**：本方案仅作为辅助诊断，不修改任何核心业务状态位。
