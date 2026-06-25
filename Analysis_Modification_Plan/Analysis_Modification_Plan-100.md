# 托管文件夹监听与入库逻辑补完（开箱即用版） —— Analysis_Modification_Plan-100.md

## 1. 任务背景
针对 Plan-99 实施中反馈的编译错误（未定义类型、缺少头文件、成员变量未声明等），本方案提供高精度的补足指引，确保所有 UI 状态切换与后台监听逻辑在现有工程中能直接编译运行。

## 2. 问题定位
- **声明缺失**：`MainWindow.h` 缺少 `DriveButton` 的前置声明及 `m_activeDrives` 等成员变量。
- **作用域模糊**：`DriveButton` 的状态枚举 `State` 未明确作用域，导致 `Active/Running` 等标识符无法识别。
- **头文件遗漏**：`AutoImportManager` 缺少 `<unordered_set>` 标准库支持。
- **API 调用失误**：`_wcsnicmp` 漏掉第三参数；`QObject::connect` 因类型不完整导致模板推导失败。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 必须做到开箱即用 | 补全所有头文件包含、前置声明及成员变量定义 | ✅ |
| 2    | 状态B — 已激活：USN Journal 开始监听 | 实现 `m_activeDrives` 管理及 `MftReader` 联动 | ✅ |
| 3    | 仅监听...ArcMeta.Library 文件夹内部变化 | 修正 `libraryPrefix` 定义及 `_wcsnicmp` 调用参数 | ✅ |
| 4    | 暂停图标：使用现有 SvgIcons.h 中的暂停相关图标 | 明确使用 `SvgIcons::icons["pause"]` 绘制 | ✅ |

## 4. 详细解决方案

### 4.1 UI 基础声明补完 (`src/ui/MainWindow.h`)
必须在 `namespace ArcMeta` 内添加前置声明，并补全私有成员变量：
```cpp
// 在 MainWindow 声明前添加
class DriveButton; 

class MainWindow : public QMainWindow {
    // ...
private:
    QStringList m_activeDrives; // 对应用户原话：“已激活盘符列表，如 "C:,G:"”
    std::unordered_map<QString, bool> m_drivePausedMap; // 任务暂停状态追踪
    // ... 修改 m_driveButtonMap 的类型定义
    QMap<QString, DriveButton*> m_driveButtonMap; 
};
```

### 4.2 状态枚举与绘制补完 (`src/ui/DriveButton.h/cpp`)
1. **枚举定义**：
```cpp
class DriveButton : public QPushButton {
public:
    enum State { Inactive, Active, Running, Paused }; // 定义互斥状态机
    void setState(State state);
    State state() const { return m_currentState; }
private:
    State m_currentState = Inactive;
};
```
2. **绘制逻辑（修正作用域）**：
在 `paintEvent` 中，使用 `if (m_currentState == Running)` 而非直接使用 `Running`。对于暂停图标，加载 `SvgIcons::icons["pause"]`。

### 4.3 监听联动逻辑修正 (`src/core/AutoImportManager.cpp`)
1. **包含头文件**：必须包含 `#include <unordered_set>`。
2. **路径判定修正**：
```cpp
void AutoImportManager::onEntryUpdated(uint64_t key) {
    // ... 获取路径 fullPath
    std::wstring libraryPrefix = driveLetter.toStdWString() + L"\\ArcMeta.Library\\";
    // 修正：_wcsnicmp 必须包含长度参数
    if (_wcsnicmp(fullPath.c_str(), libraryPrefix.c_str(), libraryPrefix.length()) == 0) {
        // 执行入库逻辑
    }
}
```

### 4.4 MainWindow 状态流转槽函数修正
```cpp
void MainWindow::onDriveButtonClicked(const QString& letter, bool checked) {
    DriveButton* btn = m_driveButtonMap.value(letter);
    if (!btn) return;

    DriveButton::State currentState = btn->state(); // 补全局部变量定义

    if (currentState == DriveButton::Inactive) { // 补全类名作用域
        btn->setState(DriveButton::Active);
        // ...
    }
    // ... 后续逻辑同理
}
```

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/MainWindow.h/cpp` (新增成员变量与声明)
- [ ] 模块/文件：`src/ui/DriveButton.h/cpp` (新增枚举与状态绘制)
- [ ] 模块/文件：`src/core/AutoImportManager.h/cpp` (修正 API 参数与头文件)

**明确禁止越界修改的范围：**
- [ ] 禁止在非 `ArcMeta` 命名空间下定义全局变量。
- [ ] 禁止修改现有 QSS 中未提及的颜色变量。

## 6. 实现准则与预警【核心】
1. **前置声明预警**：在 `MainWindow.h` 中使用 `DriveButton*` 之前必须有 `class DriveButton;`，且 `MainWindow.cpp` 顶部必须 `#include "DriveButton.h"`，否则 `dynamic_cast` 和成员函数调用将触发“未定义类型”错误。
2. **标准库包含**：由于使用了 `std::unordered_map` 和 `std::unordered_set`，必须确保相应头文件在 `.h` 或 `.cpp` 中就绪。
3. **API 参数核对**：`_wcsnicmp` 的第三个参数应使用 `libraryPrefix.length()`，确保判定范围精确到托管文件夹。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| 状态机互斥性 | 四种状态必须互斥 | ✅ 符合 |
| 旋转动画复用 | 复用 refresh 图标 | ✅ 符合 |
| QSS 禁用态 | 必须包含 :disabled 状态 | ✅ 符合 |

## 8. 待确认事项
- `m_activeDrives` 的持久化写入时机：建议在每次 `onDriveButtonClicked` 导致状态 A/B 切换后立即调用 `AppConfig::instance().setValue(...)`，以防异常崩溃丢失状态。
