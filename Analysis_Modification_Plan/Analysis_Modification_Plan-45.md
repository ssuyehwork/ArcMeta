# 架构分析与修改方案 - 全局 UI 统一化（原生静态对话框至 FramelessDialog 的深度迁移分析）

## 1. 现状分析与逻辑根因

### 1.1 问题描述
当前系统中部分界面在执行“重命名”、“新建组”或“输入密码”操作时，弹出的对话框带有 Windows 原生标题栏，与主程序无边框、扁平化的 UI 风格不统一。用户提供的截图 `image.png` 证实了这一现象。

### 1.2 逻辑根因
经过审计，发现以下代码位置直接使用了 Qt 原生的静态方法 `QInputDialog::getText()`：
- **TagManagerView.cpp**:
    - `新建标签组` 操作
    - `重命名组` 操作
    - `重命名标签` 操作
- **ContentPanel.cpp**:
    - `设置加密密码` 操作
    - `解除加密密码` 操作

这些静态方法会由操作系统直接创建带装饰的窗口，绕过了项目自定义的 `FramelessDialog` 架构，导致了视觉上的“跳戏”。

---

## 2. 解决方案架构设计

### 2.1 增强 FramelessInputDialog (架构升级)
目前项目已存在 `FramelessInputDialog` 类，但其功能较为单一，仅支持普通文本输入，不支持密码模式（Password Echo Mode）。为了满足 `ContentPanel` 的加密/解密密码输入需求，需要对其进行如下架构增强：

**技术方案 (src/ui/FramelessDialog.h):**
- **增加成员函数**：`void setEchoMode(QLineEdit::EchoMode mode);` 用于动态调整输入框的显示模式。
- **扩展构造函数**：建议将构造函数签名修改为支持初始模式设定，以减少调用方的代码量。

**预期效果**：通过 `dlg.setEchoMode(QLineEdit::Password)`，使 `FramelessInputDialog` 完美替代 `QInputDialog::getText(..., QLineEdit::Password, ...)`。

### 2.2 新增 FramelessColorPicker (功能补全)
由于 `CategoryPanel` 仍在使用 `QColorDialog::getColor`，这会导致颜色选择时跳出原生窗口。
- **架构建议**：新建 `FramelessColorPicker` 类，继承自 `FramelessDialog`。
- **UI 组件**：内部集成现有的 `ColorPicker` 小部件（src/ui/ColorPicker.h）。

### 2.3 替换逻辑
将所有 `QInputDialog::getText` 的调用替换为 `FramelessInputDialog` 的实例化调用。

#### A. 标签管理界面 (TagManagerView.cpp)
**原始代码：**
```cpp
QString name = QInputDialog::getText(this, "新建标签组", "标签组名称:", QLineEdit::Normal, "", &ok);
```
**建议方案：**
```cpp
FramelessInputDialog dlg("新建标签组", "标签组名称:", "", this);
if (dlg.exec() == QDialog::Accepted) {
    QString name = dlg.text();
    // ... 原有逻辑
}
```

#### B. 内容面板 (ContentPanel.cpp) - 加密模式
**原始代码：**
```cpp
QString pwd = QInputDialog::getText(this, "加密保护", "设置加密密码:", QLineEdit::Password, "", &ok);
```
**建议方案（配合增强后的类）：**
```cpp
FramelessInputDialog dlg("加密保护", "设置加密密码:", "", this);
dlg.setEchoMode(QLineEdit::Password); 
if (dlg.exec() == QDialog::Accepted) {
    QString pwd = dlg.text();
    // ... 原有逻辑
}
```

#### C. 分类面板 (CategoryPanel.cpp) - 颜色选择
**原始代码：**
```cpp
QColor color = QColorDialog::getColor(Qt::white, this, "选择分类颜色");
```
**建议方案：**
```cpp
FramelessColorPicker dlg("选择分类颜色", this);
dlg.setCurrentColor(originalColor);
if (dlg.exec() == QDialog::Accepted) {
    QColor color = dlg.selectedColor();
    // ... 原有逻辑
}
```

---

## 3. 规范一致性审计
- **圆角**：`FramelessInputDialog` 内部容器应保持 `6px` 圆角。
- **标题栏**：统一使用 `FramelessDialog` 自带的 34px 高度自定义标题栏。
- **交互**：保持 ESC 键退出及 Enter 键确认的逻辑。

## 4. 结论
通过将遗留的原生静态对话框替换为自有的无边框组件，可以实现全软件 UI 的 100% 视觉统一。
