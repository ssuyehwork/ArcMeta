# 关闭按钮悬停色逻辑纠偏方案

## 1. 现状审计
在 `src/ui/MainWindow.cpp` 第 1042-1046 行的 `m_btnClose->setStyleSheet` 代码块中，存在错误的颜色脑补实现：

```cpp
m_btnClose->setStyleSheet(QString(
    "QPushButton { background-color: %1; border: none; border-radius: 4px; padding: 0; }"
    "QPushButton:hover { background-color: #F1707A; }"  // <--- 错误的硬编码
    "QPushButton:pressed { background-color: #A50000; }"
).arg(qssColor(ErrorRed)));
```

### 1.1 偏差分析
- **错误点**：`QPushButton:hover` 被设置为了 `#F1707A`（淡红色）。
- **用户需求**：关闭按钮无论在何种状态（普通、悬停）下，其背景色必须**始终保持 `#e81123` (ErrorRed)**。

---

## 2. 修改方案

### 2.1 物理对齐需求
- **操作**：将 `QPushButton:hover` 样式块中的背景色变量指向 `%1`，从而在 QSS 渲染时引用全局定义的 `ErrorRed`。

### 2.2 推荐修正代码

```cpp
m_btnClose->setStyleSheet(QString(
    "QPushButton { background-color: %1; border: none; border-radius: 4px; padding: 0; }"
    "QPushButton:hover { background-color: %1; }"  // 修正：悬停时也保持 #e81123
    "QPushButton:pressed { background-color: #A50000; }" // 按下态保持原有加深反馈
).arg(qssColor(ErrorRed)));
```

---

## 3. 预期效果
- **悬停反馈**：当鼠标移入关闭按钮时，背景色将不再发生“淡化”或“变粉”的现象，视觉上保持稳固的 `#e81123` 品牌红。
- **图标可见性**：白色图标 `#EEEEEE` 在 `#e81123` 的高对比度背景下将持续保持清晰，不会受悬停动作影响。
