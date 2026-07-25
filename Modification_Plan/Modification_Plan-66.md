# Modification Plan - 标注色块选择器集成与旧颜色逻辑根除方案

本方案严格遵守用户指定的任务边界，致力于：
1. 彻底根除并清理“随机颜色”和“设置颜色”相关的旧逻辑代码；
2. 在分类树（`CategoryPanel`）及自定义盘符文件夹按钮（`FolderButton`）的右键菜单上，直接渲染横向圆形标注色选择器。鼠标 Hover 时呈现同心白色圆圈高亮；
3. 选择色块后，关闭右键菜单，并将色值同时写入数据库 `categories` 表的 `color` 字段以及 `metadata` 表的 `color` 字段，实现数据的多表同步。

---

## 1. 拟修改与清理的文件清单

- **`src/ui/CategoryPanel.h` / `src/ui/CategoryPanel.cpp`**:
  - 彻底删除旧函数 `onSetColor()` 与 `onRandomColor()` 的声明与定义。
  - 清理分类树右键菜单中的 `“设置颜色”` 与 `“随机颜色”` 菜单项。
  - 引入并在右键菜单中嵌入新建的 `ColorStripPicker` 行动作。
- **`src/ui/MainWindow.cpp`**:
  - 在 `onFolderButtonContextMenu`（自定义文件夹按钮右键菜单）中，彻底删除旧的 `actSetColor`、`actRandomColor` 动作逻辑，彻底移除调色板 `FramelessColorPicker` 在此处的实例化。
  - 引入并嵌入 `ColorStripPicker` 行动作，选择颜色后同步更新 `categories` 表与 `metadata` 表。
- **`src/ui/ColorStripPicker.h` / `src/ui/ColorStripPicker.cpp`（新增）**:
  - 实现横向色块选择控件。每个色块支持鼠标 hover 时的白色高亮圆圈，并在选中时关闭右键菜单、触发信号。

---

## 2. 核心技术设计

### 2.1 新建 `ColorStripPicker` 控件与 Hover 高亮白圈
在 `src/ui/` 目录下新增自定义 QWidget `ColorStripPicker`。该控件包含 9 个圆形色块，并整体通过 `QWidgetAction` 包装入 QMenu。

```cpp
namespace ArcMeta {

// 单个圆圈色块
class ColorStripBlock : public QWidget {
    Q_OBJECT
public:
    ColorStripBlock(const QString& colorHex, const QColor& color, QWidget* parent = nullptr)
        : QWidget(parent), m_colorHex(colorHex), m_color(color), m_hovered(false) {
        setFixedSize(28, 28);
        setAttribute(Qt::WA_Hover);
    }

signals:
    void clicked(const QString& colorHex);

protected:
    void enterEvent(QEnterEvent*) override { m_hovered = true; update(); }
    void leaveEvent(QEvent*) override { m_hovered = false; update(); }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked(m_colorHex);
        }
    }
    
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        
        // 1. 鼠标悬停时，绘制同心白色圆圈高亮
        if (m_hovered) {
            painter.setPen(QPen(Qt::white, 1.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
        }
        
        // 2. 绘制色块实体
        QRect colorRect = rect().adjusted(4, 4, -4, -4);
        if (m_colorHex.isEmpty()) {
            // 无颜色色块（带红斜线）
            painter.setPen(QPen(QColor("#888780"), 1.2));
            painter.setBrush(QColor("#252526"));
            painter.drawEllipse(colorRect);
            painter.setPen(QPen(QColor("#E24B4A"), 1.5));
            painter.drawLine(colorRect.topLeft() + QPoint(3, 3), colorRect.bottomRight() - QPoint(3, 3));
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_color);
            painter.drawEllipse(colorRect);
        }
    }

private:
    QString m_colorHex;
    QColor m_color;
    bool m_hovered;
};

// 横向排布条
class ColorStripPicker : public QWidget {
    Q_OBJECT
public:
    explicit ColorStripPicker(QWidget* parent = nullptr) : QWidget(parent) {
        QHBoxLayout* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 4, 8, 4);
        layout->setSpacing(6);
        
        struct ColorItem { QString hex; QColor preview; };
        QList<ColorItem> items = {
            {"", QColor("#888780")},
            {"#E24B4A", QColor("#E24B4A")},
            {"#EF9F27", QColor("#EF9F27")},
            {"#FECF0E", QColor("#FECF0E")},
            {"#639922", QColor("#639922")},
            {"#1D9E75", QColor("#1D9E75")},
            {"#378ADD", QColor("#378ADD")},
            {"#7F77DD", QColor("#7F77DD")},
            {"#5F5E5A", QColor("#5F5E5A")}
        };
        
        for (const auto& ci : items) {
            auto* block = new ColorStripBlock(ci.hex, ci.preview, this);
            layout->addWidget(block);
            connect(block, &ColorStripBlock::clicked, this, &ColorStripPicker::colorSelected);
        }
    }

signals:
    void colorSelected(const QString& colorHex);
};

} // namespace ArcMeta
```

### 2.2 数据的多表同步更新与菜单嵌入

#### 2.2.1 分类树（`CategoryPanel`）右键菜单应用：
1. 选中颜色后，通过 `CategoryRepo` 将 `colorHex` 写入对应分类的 `color` 字段并落库。
2. 同步检查分类是否具有物理路径 `physicalPath`，若有则调用 `MetadataManager::instance().setColor(cat.physicalPath, colorHex.toStdWString(), true)` 同步写入 `metadata` 表。
3. 刷新视图 `m_categoryModel->refresh()`。

#### 2.2.2 盘符/自定义文件夹（`FolderButton`）右键菜单应用：
1. 调用 `MetadataManager::instance().setColor(folderPath, colorHex.toStdWString(), true)` 写入元数据表。
2. 同时调用 `CategoryRepo::updateCategoryColorByPath(folderPath, colorHex.toStdWString())`，将对应的分类数据库表（`categories`）中的对应字段同步写入。
3. 更新 AppConfig 本地颜色配置并重绘按钮。

---

## 3. 彻底清除旧代码列表（绝不残留）

- `CategoryPanel::onSetColor` 及定义。
- `CategoryPanel::onRandomColor` 及定义。
- 移除 `CategoryPanel` 菜单构建处的旧“设置颜色”与“随机颜色”动作。
- 移除 `MainWindow` 菜单构建处的旧 `actSetColor`、`actRandomColor` 动作，完全不再引用、实例化 `FramelessColorPicker`。

---

## 4. 验证与回归测试

1. 右击侧边栏任意分类，横向圆形色块选择条完美嵌入，无冗余文字。
2. 鼠标悬浮在圆形色块上，能被耀眼的白色高亮圆圈精准包裹。
3. 点击色块后，右键菜单完美关闭， `categories` 数据库和 `metadata` 数据库中的对应的 `color` 字段同步落盘更改，界面刷新。
