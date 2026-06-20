# 磁盘栏“设定默认选项”功能适配分析 —— Analysis_Modification_Plan-70.md

## 1. 任务背景
用户希望在当前版本的主界面磁盘栏（Drive Bar）中，复刻旧版 `ScanDialog` 的“设定默认选项”功能。被设为默认的磁盘在视觉上应有“★”标识，且该状态需持久化存储。

## 2. 问题定位
- **模块**：`src/ui/MainWindow.cpp`
- **函数**：
    - `initUi()`：负责磁盘按钮的初始创建。
    - `onDriveContextMenu()`：负责磁盘按钮的右键菜单。
- **根因分析**：当前版本的磁盘栏仅实现了基础的激活（Active）与扫描逻辑，缺失了默认磁盘（Default）的配置读取、UI 表现及右键交互逻辑。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 盘符左侧显示了★，说明已经被设定了“设定默认选项” | 在 `updateDriveButtonStyles` 中根据配置为按钮文本添加 "★ " 前缀 | ✅ |
| 2    | 当前版本也应该有“设定默认选项” | 在 `onDriveContextMenu` 中增加“设为默认选项/取消默认选项”菜单项 | ✅ |
| 3    | 存储与持久化 | 使用 `AppConfig::instance().getValue("Drives/DefaultDrives")` 进行存储 | ✅ |

## 4. 详细解决方案

### 4.1 数据结构与初始化
在 `MainWindow.h` 中无需新增成员变量，直接利用 `AppConfig`。
在 `MainWindow.cpp` 的 `initUi` 中，初始化磁盘按钮后需调用刷新样式的私有方法。

### 4.2 新增私有方法 `updateDriveButtonStyles`
```cpp
void MainWindow::updateDriveButtonStyles() {
    QStringList defaultDrives = AppConfig::instance().getValue("Drives/DefaultDrives").toStringList();
    for (auto it = m_driveButtonMap.begin(); it != m_driveButtonMap.end(); ++it) {
        QString letter = it.key();
        QPushButton* btn = it.value();
        bool isDefault = defaultDrives.contains(letter);
        
        // 更新文本：★ + 盘符 (如 ★ C:)
        btn->setText(QString("%1%2").arg(isDefault ? "★ " : "").arg(letter));
        
        // 触发属性刷新（用于潜在的 QSS 样式联动）
        btn->setProperty("isDefault", isDefault);
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
    }
}
```

### 4.3 增强 `onDriveContextMenu`
在 `onDriveContextMenu` 中添加逻辑：
```cpp
void MainWindow::onDriveContextMenu(const QString& letter, const QPoint& pos) {
    QMenu menu(this);
    UiHelper::applyMenuStyle(&menu);
    
    // --- 新增：默认选项控制 ---
    QStringList defaultDrives = AppConfig::instance().getValue("Drives/DefaultDrives").toStringList();
    bool isDefault = defaultDrives.contains(letter);
    QAction* defaultAct = menu.addAction(isDefault ? "取消默认选项" : "设为默认选项");
    menu.addSeparator();
    // ------------------------

    QAction* setFolderAct = menu.addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "设置托管文件夹...");
    // ... 原有逻辑 ...

    QAction* chosen = menu.exec(m_driveButtonMap[letter]->mapToGlobal(pos));
    if (chosen == defaultAct) {
        if (isDefault) defaultDrives.removeAll(letter);
        else if (!defaultDrives.contains(letter)) defaultDrives.append(letter);
        
        AppConfig::instance().setValue("Drives/DefaultDrives", defaultDrives);
        AppConfig::instance().sync();
        updateDriveButtonStyles();
    } 
    // ... 原有 else if 处理 ...
}
```

### 4.4 逻辑链整合
1.  **修改 `initUi`**：在创建完所有 `QPushButton` 并添加到 `m_driveButtonMap` 后，调用一次 `updateDriveButtonStyles()`。
2.  **修改 `onDriveContextMenu`**：如 4.3 所示，实现配置的增删与 UI 同步。

## 5. 修改边界声明【红线】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/MainWindow.h` (声明新方法)
- [ ] 模块/文件：`src/ui/MainWindow.cpp` (实现逻辑)

**明确禁止越界修改的范围：**
- [ ] 禁止修改磁盘扫描底层 `MftReader` 逻辑.
- [ ] 禁止修改 `DatabaseManager` 的数据库挂载逻辑.

## 6. 实现准则与预警【核心】
1.  **头文件依赖**：确保 `MainWindow.cpp` 已包含 `#include "core/AppConfig.h"` 和 `#include <QStyle>`。
2.  **QSS 刷新**：必须执行 `unpolish/polish` 序列，否则 `setProperty` 变更不会触发样式重绘（若 QSS 中使用了 `[isDefault="true"]`）。
3.  **文本原子性**：设置文本时应保持旧版格式，即 `★ ` 字符后带一个空格。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求 | 本方案是否符合 |
|-------------|----------------------|----------------|
| UI 刷新防抖 | 暂无大批量操作，无需 Debounce | ✅ |
| 管理员权限 | 磁盘访问逻辑已存在，本方案仅修改 UI 表现，无权限冲突 | ✅ |
| AppConfig | 统一使用 `AppConfig::instance()` | ✅ |

## 8. 待确认事项
- 旧版 `ScanDialog` 中默认盘符似乎还带有磁盘卷标（Label），如 `★ G: (Data)`。当前 `MainWindow` 的磁盘按钮较小（固定宽度 60px），建议仅保留 `★ C:` 格式以防溢出。确认是否仅显示盘符？
