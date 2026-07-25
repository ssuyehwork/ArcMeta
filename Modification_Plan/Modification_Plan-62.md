# 彻底根除随机与设置颜色旧UI并在右键菜单上直接以悬停白圈色块展示同步存库 —— Modification_Plan-62.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
在目前的逻辑架构中，“设置颜色”和“随机颜色”在分类侧边栏右键菜单和盘符栏 FolderButton 右键菜单中采用独立的弹窗 (FramelessColorPicker) 或传统 QAction 菜单项。其配置流程复杂，不够直观。用户期望彻底根除这类老旧的逻辑代码，不可保留。而是直接将当前现有的标注色显示在右键菜单上，当鼠标悬停于色块上时，通过白圈突显色块。在选择色块后，要求将色值同时存入 `categories` 表的 `color` 字段与 `metadata` 表的 `color` 字段中。

## 2. 问题定位
- 现有的“随机颜色”和“设置颜色”在 `CategoryPanel` 及 `MainWindow` 中由独立的 `QAction` 驱动，并关联到 `onRandomColor` / `onSetColor` 及 `FramelessColorPicker` 弹窗。
- 在 `ContentPanel` 的右键菜单中，“设定颜色标签”子菜单是通过添加普通文字 Action 加上 12x12 圆形 Pixmap 作为 Icon 展现的。
- 目标逻辑及改动位置：
  1. 彻底根除 `CategoryPanel::onSetColor`、`CategoryPanel::onRandomColor`。
  2. 彻底根除 `MainWindow` 中 `onFolderButtonContextMenu` 处理 `actSetColor` / `actRandomColor` 的分支。
  3. 设计通用的、可在 QMenu 中展示的水平色块快捷选择控件 `ColorStripPicker`（基于 QWidget / QWidgetAction 机制），提供自绘圆形色块和悬停白圈突显功能。
  4. 设计色值统一写入方案，确保触发更新后，无论源自何种右键操作，皆能够将对应路径和绑定关系在 `categories` 表的 `color` 字段和 `metadata` 表的 `color` 字段（如果适用）同步保存更新。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 我期望将”随机颜色“和”设置颜色“相关逻辑代码底根除，不可保留。 (对应用户原话) | 彻底删除并清理 `CategoryPanel.cpp/h` 和 `MainWindow.cpp/h` 中所有“设置颜色”、“随机颜色”的旧 QAction 与旧事件槽函数，不残留。 | ✅ |
| 2    | 直接将当前现有的标注色直接显示在右键菜单上，鼠标悬停在某个色块上方时通过白圈突显色块，类似图中这样。 (对应用户原话) | 在 `CategoryPanel` 侧边栏分类右键菜单、`MainWindow` 中的 FolderButton 右键菜单、以及 `ContentPanel` 内容右键菜单中采用自定义 `ColorStripPicker` 控件。该控件水平一排圆润色块展示现有的 9 个标准标注色，支持在 `enterEvent`/`leaveEvent`/`mouseMoveEvent` 或 `paintEvent` 下，在 Hover 状态绘制宽度为 1.5px 至 2px 的白色高亮同心圈。 | ✅ |
| 3    | 选择某个色块后，将色值存入到categories 表的 color 字段中的同时存入到 metadata 表 of color 字段中。 (对应用户原话) | 选中色块时发射带颜色名信号，并在对应的控制器和响应逻辑（`CategoryPanel`、`ContentPanel`、`MainWindow`）中更新数据持久化。更新不仅作用于 `categories` 表对应项（调用 `CategoryRepo::update`），同时将其绑定物理路径在 `metadata` 表的 `color` 字段同步写入（调用 `MetadataManager::instance().setColor`）。两边同步，物理一致。 | ✅ |

## 4. 详细解决方案

### 4.1 水平标注色块快捷菜单项 `ColorStripPicker` 设计
为了在右键 QMenu 中直接显示水平排列的 9 个色块（包括“无色/取消”以及 8 种标注色），我们将构建一个轻量级的 QWidget，其继承自 `QWidget`：

```cpp
class ColorStripPicker : public QWidget {
    Q_OBJECT
public:
    explicit ColorStripPicker(const QString& currentColorHex, QWidget* parent = nullptr);
signals:
    void colorSelected(const QString& hexColor);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    struct ColorItem {
        QString hex;
        QColor color;
        QString name;
    };
    QList<ColorItem> m_items;
    int m_hoveredIndex = -1;
    QString m_selectedColor;
    int m_circleRadius = 10; // 半径 10，直径 20
    int m_spacing = 8;       // 间隔 8
};
```

#### 4.1.1 核心自绘与悬停白圈效果
在 `paintEvent` 中，我们首先填充深色菜单背景色（例如 `#1F1F1F` 或遵循考古样式），接着水平画出 9 个圆形色块：
- 圆直径 20px。
- 如果鼠标悬停在某个色块上方（即 `m_hoveredIndex == i`），我们使用白色画笔，在圆形色块的外边缘（略带 2-3px 的间距 padding）绘制一个亮白色的同心外圆圈，从而实现完美的白圈突显。

```cpp
void ColorStripPicker::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 背景填充 (与暗色菜单对齐)
    painter.fillRect(rect(), QColor("#1e1e1e"));

    int startX = 12; // 起始左边距
    int y = rect().height() / 2;

    for (int i = 0; i < m_items.size(); ++i) {
        int cx = startX + i * (20 + m_spacing) + 10;

        // 1. 绘制色块本身
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_items[i].color);
        painter.drawEllipse(QPoint(cx, y), 10, 10);

        // 2. 悬停状态：绘制突出亮白圈
        if (i == m_hoveredIndex) {
            painter.setBrush(Qt::NoBrush);
            // 亮白画笔，宽度 1.5 像素
            QPen pen(QColor("#FFFFFF"), 1.8);
            painter.setPen(pen);
            // 稍大一圈，半径设为 12 像素以包裹里面的色块
            painter.drawEllipse(QPoint(cx, y), 12, 12);
        }
    }
}
```

- 通过重写 `mouseMoveEvent` 来检测当前鼠标位于哪一个色块圆盘中，并实时更新 `m_hoveredIndex` 与触发 `update()` 重绘，同时可以使用 `QToolTip` 在色块上方提示对应颜色名。

### 4.2 清理并重构 `CategoryPanel` 侧边右键菜单
1. 彻底删除 `CategoryPanel::onSetColor` 和 `CategoryPanel::onRandomColor` 的声明及实现。
2. 在 `CategoryPanel::showContextMenu` 对应连接处，删除“设置颜色”与“随机颜色”的 Action 构建。
3. 增加全新的 `ColorStripPicker` 选择逻辑。使用 `QWidgetAction` 加载：

```cpp
// 彻底移除 actSetColor, actRandomColor 后：
QMenu* colorMenu = menu.addMenu(UiHelper::getIcon("palette", WarningOrange, 18), "分类颜色标");
UiHelper::applyMenuStyle(colorMenu);

QWidgetAction* pickerAction = new QWidgetAction(colorMenu);
ColorStripPicker* pickerWidget = new ColorStripPicker(colorStr, colorMenu);
pickerAction->setDefaultWidget(pickerWidget);
colorMenu->addAction(pickerAction);

connect(pickerWidget, &ColorStripPicker::colorSelected, this, [this, id, colorMenu]() {
    // 1. 更新 categories 表中的 color 字段
    auto all = CategoryRepo::getAll();
    for (auto& cat : all) {
        if (cat.id == id) {
            cat.color = hexColor.toUpper().toStdWString();
            CategoryRepo::update(cat);
            // 2. 如果关联了物理路径，同步更新 metadata 表中的 color 字段
            if (!cat.physicalPath.empty()) {
                MetadataManager::instance().setColor(cat.physicalPath, cat.color, true);
            }
            break;
        }
    }
    m_categoryModel->refresh();
    colorMenu->close();
});
```

### 4.3 清理并重构 `MainWindow` FolderButton 菜单
1. 彻底删除 `MainWindow::onFolderButtonContextMenu` 中，关于 `actSetColor` 与 `actRandomColor` 的逻辑处理。
2. 将菜单中的“设置颜色”和“随机颜色”入口删除，用全新 `ColorStripPicker` 实现色块直观展示：

```cpp
QMenu* colorMenu = menu.addMenu(UiHelper::getIcon("palette", WarningOrange, 18), "设定分类色");
UiHelper::applyMenuStyle(colorMenu);

QWidgetAction* pickerAction = new QWidgetAction(colorMenu);
ColorStripPicker* pickerWidget = new ColorStripPicker(colorStr, colorMenu);
pickerAction->setDefaultWidget(pickerWidget);
colorMenu->addAction(pickerAction);

connect(pickerWidget, &ColorStripPicker::colorSelected, this, [btn, path, colorMenu]() {
    AppConfig::instance().setValue(QString("DriveBar/FolderColor_%1").arg(path), hexColor.toUpper());
    AppConfig::instance().sync();

    // 如果此物理路径在托管库内已被入库标记为分类，同时也应同步存入 categories 表
    std::wstring normPath = MetadataManager::normalizePath(path.toStdWString());
    MetadataManager::instance().setColor(normPath, hexColor.toUpper().toStdWString(), true);

    btn->update();
    colorMenu->close();
});
```

### 4.4 清理并重构 `ContentPanel` 右键菜单
1. 在 `ContentPanel::onCustomContextMenuRequested` 内部，移除传统的 `ActionColorTag` 普通菜单列表添加。
2. 修改为 `QWidgetAction` 引入 `ColorStripPicker`：

```cpp
QMenu* colorMenu = menu.addMenu("设定颜色标签");
UiHelper::applyMenuStyle(colorMenu);
colorMenu->setIcon(UiHelper::getIcon("palette", QColor("#EEEEEE")));

QWidgetAction* pickerAction = new QWidgetAction(colorMenu);
ColorStripPicker* pickerWidget = new ColorStripPicker(currentColorStr, colorMenu);
pickerAction->setDefaultWidget(pickerWidget);
colorMenu->addAction(pickerAction);

connect(pickerWidget, &ColorStripPicker::colorSelected, this, [this, view, colorMenu](const QString& hexColor) {
    auto indexes = view->selectionModel()->selectedIndexes();
    for (const auto& idx : indexes) {
        if (idx.column() == 0) {
            QString itemPath = idx.data(PathRole).toString();
            // 1. 同步写入到 metadata 表的 color 字段
            m_proxyModel->setData(idx, hexColor, ColorRole);

            // 2. 如果它是文件夹并且被绑定为了 categories 分类，则同时存入 categories 表的 color 字段
            std::wstring normPath = MetadataManager::normalizePath(itemPath.toStdWString());
            CategoryRepo::updateCategoryColorByPath(normPath, hexColor.toUpper().toStdWString());

            // 3. 重新渲染图标以保持视觉同步
            QIcon coloredIcon;
            QString ext = QFileInfo(itemPath).suffix().toLower();
            if (UiHelper::isGraphicsFile(ext)) {
                QImage img = UiHelper::getShellThumbnail(itemPath, this->m_zoomLevel);
                if (!img.isNull()) coloredIcon = QIcon(QPixmap::fromImage(img));
            }
            if (coloredIcon.isNull()) {
                coloredIcon = UiHelper::getFileIcon(itemPath, this->m_zoomLevel);
            }
            m_proxyModel->setData(idx, coloredIcon, Qt::DecorationRole);
        }
    }
    colorMenu->close();
});
```

## 5. 修改边界声明【范围】

**本次方案涉及范围：**
- [ ] 模块/文件：`src/ui/CategoryPanel.h` (清理无用旧响应方法)
- [ ] 模块/文件：`src/ui/CategoryPanel.cpp` (清除旧逻辑，右键菜单换用 ColorStripPicker)
- [ ] 模块/文件：`src/ui/MainWindow.h` (移除旧成员声明)
- [ ] 模块/文件：`src/ui/MainWindow.cpp` (清理 FolderButton 菜单，改为色块选择)
- [ ] 模块/文件：`src/ui/ContentPanel.cpp` (移除“设定颜色标签”下普通多子项选择，改为色块水平选择，加固写入逻辑)
- [ ] 模块/文件：`src/ui/ColorPicker.h` (头文件末尾/或新增定义 `ColorStripPicker` 快捷色块栏组件)
- [ ] 模块/文件：`src/ui/ColorPicker.cpp` (实现 `ColorStripPicker` 快捷色块栏及白圈 Hover 逻辑)

**明确禁止越界修改的范围：**
- [ ] 模块/文件：`src/core/BasicCommands.h` —— 不修改
- [ ] 模块/文件：`src/meta/DatabaseManager.cpp` —— 不修改底层 Schema
- [ ] 模块/文件：`src/mft/MftReader.cpp` —— 不修改物理扫描层

## 6. 实现准则与预警【核心】
1. **依赖对齐**：在 `CategoryPanel.cpp`、`MainWindow.cpp` 与 `ContentPanel.cpp` 中需要 `#include "ColorPicker.h"` 从而调用 `ColorStripPicker`。
2. **存库一致性**：更新色值时，统一大写格式（如 `#E24B4A` 或空串），并在调用 `CategoryRepo` 和 `MetadataManager` 进行同步更新时确保互锁正常，不引发无限重入。
3. **QSS 作用域限制**：在色块菜单中绘制选中与悬停高亮，由于直接使用 Painter 绘制，不依赖 CSS 以免被宿主菜单污染。

## 7. Memories.md 合规检查

| 组件 / 模式 | Memories.md 规范要求（写具体内容，不写引用） | 本方案是否符合 |
|-------------|----------------------|----------------|
| 双向同步模式 | 普通未入库项目不受修改颜色影响，更改颜色只对入库绑定有效的文件夹分类有效。 | ✅ 符合。代码内已做 `physicalPath.empty()` 判定保护，符合规范要求。 |
| 清除按钮原生组件 | 必须使用 Qt 原生 setClearButtonEnabled(true)，本方案无清除按钮相关新增。 | ✅ 符合。本方案未新增包含输入框的清除组件，完全符合。 |

## 8. 待确认事项（可选）
（无）
