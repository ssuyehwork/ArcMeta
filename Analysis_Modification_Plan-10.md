# 全应用关闭按钮样式统一方案 (Analysis_Modification_Plan-10)

## 1. 现状调研
目前系统中存在四种典型的关闭按钮样式实现，分布在不同模块：
- **MainWindow.cpp (L1054)**: 硬编码红色背景，悬停仍为红色。
- **MetaPanel.cpp (L327)**: 面板头部关闭按钮，硬编码红色背景，悬停为亮红色。
- **MetaPanel.cpp (L171)**: 标签块 (TagPill) 关闭按钮，已符合 `rgba(255, 255, 255, 0.1)` 规范。
- **FramelessDialog.cpp (L116)**: 对话框关闭按钮，独立样式定义。

## 2. 目标规范
根据用户要求，所有界面的关闭按钮悬停状态必须统一为：
- **悬停 (Hover)**: `background: rgba(255, 255, 255, 0.1);`
- **基础 (Normal)**: `background: transparent;` (对于主窗口等原本红色的，需降权为透明或保持背景色但统一悬停感)。

## 3. 详细修改路径

### 3.1 MainWindow 调整
- **位置**: `src/ui/MainWindow.cpp` 函数 `setupCustomTitleBarButtons`。
- **改动**: 将 `m_btnClose` 的样式表从硬编码红色背景改为：
  ```cpp
  m_btnClose->setStyleSheet(QString(
      "QPushButton { background-color: transparent; border: none; border-radius: 4px; padding: 0; }"
      "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); }"
      "QPushButton:pressed { background-color: rgba(255, 255, 255, 0.2); }"
  ));
  ```

### 3.2 MetaPanel 调整
- **位置**: `src/ui/MetaPanel.cpp` 函数 `initUi`。
- **改动**: 找到 `closeBtn` (面板头部)，同步修改其 QSS 定义。

### 3.3 FramelessDialog 调整
- **位置**: `src/ui/FramelessDialog.cpp` 函数 `initUi`。
- **改动**: 统一替换 `m_closeBtn` 的样式表。

## 4. 推荐的全局优化方案
为了避免后续再次出现样式不统一，建议在 `UiHelper` 中增加一个静态方法：
```cpp
void UiHelper::applyCloseButtonStyle(QPushButton* btn) {
    btn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background: rgba(255, 255, 255, 0.2); }"
    );
}
```
然后在各处调用 `UiHelper::applyCloseButtonStyle(btn)`。

## 5. 结论
通过将原本分散在各处的“红色警示”样式降权为统一的“半透明悬浮”样式，可以显著提升视觉的一致性与呼吸感，符合工业级 UI 的打磨要求。
