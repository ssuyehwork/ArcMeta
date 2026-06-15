# Analysis and Modification Plan - QMessageBox 与 QFileDialog 自定义标准化方案

## 1. FramelessMessageBox 实施方案 (消息弹窗)

### 1.1 逻辑定位
旨在彻底替换项目中散落在各处的 `QMessageBox` 调用。通过在 `FramelessDialog` 命名空间下建立一套静态 API，封装已有的 `FramelessConfirmDialog`。

### 1.2 接口设计 (C++ 伪代码)
```cpp
class FramelessMessageBox {
public:
    enum Icon { Information, Warning, Critical, Question };
    
    static void information(QWidget* parent, const QString& title, const QString& text);
    static void warning(QWidget* parent, const QString& title, const QString& text);
    static bool question(QWidget* parent, const QString& title, const QString& text); // 返回是否点击确定
    static void critical(QWidget* parent, const QString& title, const QString& text);
};
```

### 1.3 视觉映射规范
- **Information**: 图标 `info` (#3498db)，仅展示“确定”按钮。
- **Warning**: 图标 `warning` (#f1c40f)，仅展示“确定”按钮。
- **Critical**: 图标 `error` (#e81123)，展示“确定”按钮。
- **Question**: 图标 `help` (#3498db)，展示“确定”与“取消”双按钮。

### 1.4 实施步骤
1. 在 `src/ui/FramelessDialog.h` 中声明 `FramelessMessageBox`。
2. 在 `FramelessDialog.cpp` 中实现静态方法，动态创建 `FramelessConfirmDialog` 实例并根据类型调整图标颜色。
3. 全局搜索并替换 `QMessageBox::` 调用。

---

## 2. FramelessFileDialog 实施方案 (自定义文件浏览器)

### 2.1 架构设计
新建 `src/ui/FramelessFileDialog.h/cpp`，继承自 `FramelessDialog`。

### 2.2 UI 布局树
- **Container**: `FramelessDialog` 基座。
- **ContentArea (VBoxLayout)**:
    - **TopRow (HBoxLayout)**: 复用 `AddressBar` 核心逻辑，提供路径跳转与面包屑。
    - **MainGrid (HBoxLayout)**:
        - **Sidebar (QListWidget)**: 快捷导航（此电脑、下载、桌面、常用硬盘分区）。
        - **FileView (QListView)**: 核心内容区。数据驱动采用 `QFileSystemModel`，配合自定义 Delegate 实现暗色模式风格。
    - **BottomRow (GridLayout)**:
        - 文件名输入框 (`QLineEdit`)。
        - 文件类型筛选器 (`QComboBox`)。
        - 动作按钮组（“打开/选择”、“取消”）。

### 2.3 关键接口对标 (API Compatibility)
提供与 `QFileDialog` 完全一致的静态方法，实现业务代码的“零成本”迁移：
```cpp
static QString getExistingDirectory(QWidget* parent, const QString& caption, const QString& dir);
static QString getOpenFileName(QWidget* parent, const QString& caption, const QString& dir, const QString& filter);
```

### 2.4 技术难点与处理
- **高性能加载**: 针对大型目录，配置 `QFileSystemModel` 的 `setReadOnly(true)` 及 `setNameFilterDisables(false)`。
- **视觉一致性**: 所有滚动条必须对标 `style.qss` 中的标准（宽 10px, 圆角 3px, 透明轨道）。

---

## 3. 执行规范与申报约束
- **杜绝脑补**: 以上方案严格限定于“替换原生窗口”。严禁在实现过程中顺手修改 `FramelessDialog` 的拖拽、置顶或其他基类逻辑。
- **保持非缩放**: `FramelessFileDialog` 初始尺寸建议设为 `800x600`，且不开启边缘缩放功能，维持现有的窗口特性。
- **申报备案**: 在后续编写代码时，若发现 `QFileSystemModel` 与项目现有的 MFT 逻辑存在性能冲突，必须在 `Declaration_Log.md` 中备案，不得擅自重写底层索引引擎。
