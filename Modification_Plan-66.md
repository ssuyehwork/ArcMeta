# Modification Plan - 标注色块选择器集成与全模式断行防混排方案

本方案旨在：
1. 彻底清除冗余的“随机颜色”和“设置颜色”旧逻辑代码，并直接在右键菜单中提供一个横向圆形色块选择器；
2. 贯彻 **“任何模式都不允许混排”** 的钢律，捍卫文件夹与普通文件的严格断行隔离规则，消除当前版本中 `GridMode` 视图下文件与文件夹同行并排的恶劣混排 Bug。

---

## 1. 拟修改与清理的文件清单

- **`src/ui/CategoryPanel.h` / `src/ui/CategoryPanel.cpp`**:
  - 彻底删除 `onSetColor()` 与 `onRandomColor()` 声明与定义。
  - 删除右键菜单中的 `“设置颜色”` 与 `“随机颜色”` 菜单项。
  - 引入并在右键菜单中嵌入新建的 `ColorStripPicker` 行动作。
- **`src/ui/MainWindow.cpp`**:
  - 在 `onFolderButtonContextMenu`（文件夹按钮右键菜单）中，删除旧的 `actSetColor`、`actRandomColor` 动作逻辑，彻底移除 `FramelessColorPicker` 在此处的实例化。
  - 引入并嵌入 `ColorStripPicker` 行动作，选择颜色后同步更新多表字段。
- **`src/ui/JustifiedView.cpp`**:
  - 针对 `GridMode` 网格视图重构排版循环，注入类型突变断行判定，彻底消除文件和文件夹同行并排。
  - 强化并保留 `JustifiedMode` 自适应视图下的物理强制分离换行机制。
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

### 2.2 彻底捍卫“任何模式都不允许混排”硬核折行机制

#### 2.2.1 `GridMode`（网格视图）彻底杜绝同行混排
当前版本的网格模式排版没有任何类型判断，导致文件进入文件夹同行。我们通过在填充该行之前进行**类型变动强行截断断行**：

```cpp
    if (m_layoutMode == GridMode) {
        // GridMode 网格等宽等高排布
        int itemWidth = m_targetRowHeight + cardPadding;
        int itemHeight = m_targetRowHeight + extraHeight;

        int maxNumInRow = (containerWidth + spacing) / (itemWidth + spacing);
        if (maxNumInRow <= 0) maxNumInRow = 1;

        int standardSpacing = spacing;
        if (maxNumInRow > 1) {
            standardSpacing = (containerWidth - (maxNumInRow * itemWidth)) / (maxNumInRow - 1);
        }

        int i = 0;
        while (i < count) {
            int rowStart = i;
            // 1. 获取当前行首项的类型 (是文件夹/分类还是普通文件)
            QModelIndex firstIdx = model()->index(rowStart, 0);
            QString firstType = model()->data(firstIdx, TypeRole).toString();
            bool isFirstDir = (firstType == "folder" || firstType == "category");

            // 2. 遍历本行允许容纳的项，一旦检测到后续项类型突变，强行截断断行，另起一行！
            int numInRow = 0;
            while (numInRow < maxNumInRow && (rowStart + numInRow) < count) {
                int nextIdx = rowStart + numInRow;
                QModelIndex idx = model()->index(nextIdx, 0);
                QString nextType = model()->data(idx, TypeRole).toString();
                bool isNextDir = (nextType == "folder" || nextType == "category");

                if (isNextDir != isFirstDir) {
                    break; // 类型改变，立即截断，本行填充到此为止
                }
                numInRow++;
            }

            int currentX = margin;
            for (int j = 0; j < numInRow; ++j) {
                int itemIdx = rowStart + j;
                m_geometries[itemIdx] = { QRect(currentX, currentY, itemWidth, itemHeight), itemIdx };
                currentX += itemWidth + standardSpacing;
            }
            currentY += itemHeight + spacing;
            i += numInRow; // 推进下一行
        }
    }
```

#### 2.2.2 `JustifiedMode`（自适应视图）保留物理隔离换行
保留并加固原本在 `JustifiedMode` 下工作的物理隔离换行判断，确保自适应模式下也没有任何混排漏网之鱼：
```cpp
            bool forceBreak = false;
            while (i < count) {
                QModelIndex idx = model()->index(i, 0);
                double ar = model()->data(idx, m_aspectRatioRole).toDouble();
                if (ar <= 0) ar = 1.0;
                
                // 物理分离逻辑：如果当前项是文件，但行首是文件夹（或反之），强制换行
                QString type = model()->data(idx, TypeRole).toString();
                bool isCurrentDir = (type == "folder" || type == "category");
                
                if (i > rowStart) {
                    QModelIndex prevIdx = model()->index(i - 1, 0);
                    QString prevType = model()->data(prevIdx, TypeRole).toString();
                    bool isPrevDir = (prevType == "folder" || prevType == "category");
                    
                    if (isCurrentDir != isPrevDir) {
                        forceBreak = true;
                        break;
                    }
                }
                ...
```

---

## 3. 多数据库（多表）同步写入逻辑

### 3.1 分类树（`CategoryPanel`）右键选色：
1. 更新 `categories` 表：
   - 根据选中的分类 `id` 查找 `Category` 数据，设置 `cat.color = colorHex.toStdWString()`。
   - 调用 `CategoryRepo::update(cat)` 将更改写入 `categories` 表。
2. 同步更新 `metadata` 表：
   - 检查分类是否具有 `physicalPath`，若有则调用 `MetadataManager::instance().setColor(cat.physicalPath, colorHex.toStdWString(), true)` 同步写入。
3. 自动同步和 UI 刷新：
   - 调用 `m_categoryModel->refresh()`。
   - 自动关闭菜单。

### 3.2 盘符/自定义文件夹（`FolderButton`）右键选色：
1. 更新 `metadata` 表：
   - 调用 `MetadataManager::instance().setColor(folderPath, colorHex.toStdWString(), true)`。
2. 同步更新 `categories` 表：
   - 调用 `CategoryRepo::updateCategoryColorByPath(folderPath, colorHex.toStdWString())`，确保侧边栏同名物理关联分类的颜色绝对同步。
3. 更新 AppConfig 与本地缓存：
   - `AppConfig::instance().setValue(QString("DriveBar/FolderColor_%1").arg(folderPath), colorHex)`。
   - 触发按钮重绘 `btn->update()` 并关闭右键 QMenu。

---

## 4. 彻底清除死代码列表

- `CategoryPanel::onSetColor` 及定义。
- `CategoryPanel::onRandomColor` 及定义。
- 移除 `CategoryPanel` 菜单构建处的旧 Palette 设定与 Random 设定。
- 移除 `MainWindow` 菜单构建处的旧 `actSetColor`、`actRandomColor` 和 `FramelessColorPicker`。

---

## 5. 验证与回归测试

1. 右击侧边栏任意分类，横向圆形色块选择条完美嵌入，无冗余文字。
2. 鼠标悬浮在圆形色块上，能被耀眼的白色高亮圆圈精准包裹。
3. 点击色块后，右键菜单完美关闭， categories 数据库和 metadata 数据库中的对应的 color 字段同步落盘更改，界面刷新。
4. 打开任何模式（自适应视图、网格视图），**文件夹永久在最上方，文件永久在最下方，且绝对、没有任何一个文件能混到文件夹组那一行去并排**！
