# 架构分析与修改方案 - FramelessDialog 标题栏按钮按需定制（移除冗余控件）

## 1. 现状剖析
当前的 `FramelessDialog` 基类在构造函数中采用硬编码方式创建并显示了四个标题栏按钮：
- **m_pinBtn** (置顶)
- **m_minBtn** (最小化)
- **m_maxBtn** (最大化)
- **m_closeBtn** (关闭)

对于如 `FramelessInputDialog`（即用户截图中的“重命名”对话框）这类轻量级交互窗口，置顶和缩放功能属于冗余交互，不仅占据视觉空间，也容易引发误操作。

## 2. 解决方案建议（已达成共识）

### 2.1 按钮权限按需开放原则
不应在基类中彻底删除这些按钮（因为批量重命名等大型窗口可能仍需使用），而是应通过架构调整，允许子类根据业务场景决定按钮的可见性。

### 2.2 技术实现路径（src/ui/FramelessDialog.cpp/h）

#### 方案 A：子类主动隐藏（侵入性最低）
在 `FramelessInputDialog` 的构造函数中，显式隐藏不需要的按钮：
```cpp
// 在 FramelessInputDialog 构造函数末尾添加
m_pinBtn->hide();
m_minBtn->hide();
m_maxBtn->hide();
```
*注：由于布局管理器会自动收缩空间，隐藏后的按钮不会留下空白间隙。*

#### 方案 B：基类提供配置接口（推荐，更专业）
在 `FramelessDialog` 基类中增加一个枚举标识符或配置函数：
```cpp
// 头文件声明
enum DialogButton { Pin = 1, Min = 2, Max = 4, Close = 8, All = 15 };
void setVisibleButtons(int flags);

// 实现逻辑
void FramelessDialog::setVisibleButtons(int flags) {
    m_pinBtn->setVisible(flags & Pin);
    m_minBtn->setVisible(flags & Min);
    m_maxBtn->setVisible(flags & Max);
    m_closeBtn->setVisible(flags & Close);
}
```

## 3. 规范要求
- **重命名/输入框窗口**：必须调用 `setVisibleButtons(Close)`，仅保留关闭按钮。
- **确认/提示框窗口**：同上，仅保留关闭按钮。
- **批量重命名/大窗口**：保留 `All`（默认值）。

## 4. 结论
通过对 `FramelessDialog` 标题栏按钮的可见性控制，可以显著提升小型弹出窗口的视觉纯净度，使 UI 层次感更加分明。
