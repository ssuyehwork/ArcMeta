# 分类与文件夹为空时显示“没有可显示的项目”占位提示 —— Modification_Plan-29.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
本方案承接自 Modification_Plan-27.md 与 Modification_Plan-28.md。
当前版本在侧边栏分类为空或者双击进入某个空物理文件夹时，右侧内容面板只显示一片全黑背景，没有任何友好提示。根据用户最新期望，需要在为空情况下，在内容面板正中央优雅地绘制“没有可显示的项目”文字占位提示。

## 2. 问题定位
1. **网格/自适应视图 (JustifiedView) 空状态绘制缺失**：当 `m_geometries` 为空（即 model 行数为 0）时，`JustifiedView::paintEvent` 仅仅绘制了背景，没有绘制任何提示文本。
2. **列表视图 (DropTreeView) 空状态绘制缺失**：列表模式采用 `DropTreeView`，没有在为空时自主画出提示。由于其也作为侧边栏的分类树，所以我们必须以极其解耦的方式引入一个 `m_emptyHint` 参数属性进行按需绘制。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 分类 / 文件夹为空情况下，内容面板里显示“没有可显示的项目”（对应用户原话） | 在 JustifiedView::paintEvent 与 DropTreeView::paintEvent 中，检测到行数为 0 且配置了占位提示时，在视口正中央通过温和灰（#888888）抗锯齿绘制“没有可显示的项目”提示文字。 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换。

### 4.1 修改 `src/ui/JustifiedView.cpp`
在 `paintEvent` 顶部检测到为空时，立即在中心绘制文字，避免执行后续冗余计算。

```diff
<<<<<<< SEARCH
void JustifiedView::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    // 2026-06-xx 物理修复：在开启 TranslucentBackground 时手动填充坚实背景，防止透明穿透
    painter.fillRect(viewport()->rect(), QColor("#1E1E1E"));
    
    painter.save();
    painter.translate(0, -verticalScrollBar()->value());
=======
void JustifiedView::paintEvent(QPaintEvent*) {
    QPainter painter(viewport());
    // 2026-06-xx 物理修复：在开启 TranslucentBackground 时手动填充坚实背景，防止透明穿透
    painter.fillRect(viewport()->rect(), QColor("#1E1E1E"));

    if (m_geometries.empty()) {
        painter.save();
        painter.setPen(QColor("#888888"));
        painter.setFont(QFont("Microsoft YaHei", 12));
        painter.drawText(viewport()->rect(), Qt::AlignCenter, "没有可显示的项目");
        painter.restore();
        return;
    }
    
    painter.save();
    painter.translate(0, -verticalScrollBar()->value());
>>>>>>> REPLACE
```

### 4.2 修改 `src/ui/DropTreeView.h`
声明 `setEmptyHint` 设置占位符文本接口，并重写 `paintEvent`。

```diff
<<<<<<< SEARCH
class DropTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit DropTreeView(QWidget* parent = nullptr);

    /**
     * @brief 物理辅助：暴露内部 rowHeight 接口以支持外部布局高度计算
     */
    int rowHeight(const QModelIndex& index) const { return QTreeView::rowHeight(index); }

signals:
=======
class DropTreeView : public QTreeView {
    Q_OBJECT
public:
    explicit DropTreeView(QWidget* parent = nullptr);

    /**
     * @brief 物理辅助：暴露内部 rowHeight 接口以支持外部布局高度计算
     */
    int rowHeight(const QModelIndex& index) const { return QTreeView::rowHeight(index); }

    /**
     * @brief 设置空状态时的占位文本提示
     */
    void setEmptyHint(const QString& hint) { m_emptyHint = hint; }

signals:
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

    void keyboardSearch(const QString& search) override;
=======
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void startDrag(Qt::DropActions supportedActions) override;

    void keyboardSearch(const QString& search) override;
    void paintEvent(QPaintEvent* event) override;
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
private:
    // 2026-06-xx 物理辅助：拖拽悬停自动展开
    QTimer* m_autoExpandTimer = nullptr;
    QModelIndex m_hoverIndex;
};
=======
private:
    // 2026-06-xx 物理辅助：拖拽悬停自动展开
    QTimer* m_autoExpandTimer = nullptr;
    QModelIndex m_hoverIndex;
    QString m_emptyHint;
};
>>>>>>> REPLACE
```

### 4.3 修改 `src/ui/DropTreeView.cpp`
引入 `<QPainter>` 并重写实现 `paintEvent`。

```diff
<<<<<<< SEARCH
#include "DropTreeView.h"
#include "CategoryModel.h"
#include "ContentPanel.h"
#include <QDrag>
=======
#include "DropTreeView.h"
#include "CategoryModel.h"
#include "ContentPanel.h"
#include <QDrag>
#include <QPainter>
>>>>>>> REPLACE
```

```diff
<<<<<<< SEARCH
void DropTreeView::keyboardSearch(const QString& search) {
    Q_UNUSED(search);
}

} // namespace ArcMeta
=======
void DropTreeView::keyboardSearch(const QString& search) {
    Q_UNUSED(search);
}

void DropTreeView::paintEvent(QPaintEvent* event) {
    QTreeView::paintEvent(event);
    if (!m_emptyHint.isEmpty() && model() && model()->rowCount() == 0) {
        QPainter painter(viewport());
        painter.save();
        painter.setPen(QColor("#888888"));
        painter.setFont(QFont("Microsoft YaHei", 12));
        painter.drawText(viewport()->rect(), Qt::AlignCenter, m_emptyHint);
        painter.restore();
    }
}

} // namespace ArcMeta
>>>>>>> REPLACE
```

### 4.4 修改 `src/ui/ContentPanel.cpp`
在 `ContentPanel` 初始化列表视图时，显式调用接口注入“没有可显示的项目”文本。

```diff
<<<<<<< SEARCH
    m_treeView->setRootIsDecorated(false); 
     
    // 列表视图开启 m_drawMiniCards = true，以启用 Column 0 “最左侧微卡片圆角预览”和底部分割线贯通绘制
    m_treeView->setItemDelegate(new TreeItemDelegate(this, true, true)); 
=======
    m_treeView->setRootIsDecorated(false); 
     
    // 在列表视图中显式指定空项时的文字占位提醒
    m_treeView->setEmptyHint("没有可显示的项目");

    // 列表视图开启 m_drawMiniCards = true，以启用 Column 0 “最左侧微卡片圆角预览”和底部分割线贯通绘制
    m_treeView->setItemDelegate(new TreeItemDelegate(this, true, true)); 
>>>>>>> REPLACE
```

## 5. 修改边界声明【范围】
- [x] 修改 `src/ui/JustifiedView.cpp`
- [x] 修改 `src/ui/DropTreeView.h`
- [x] 修改 `src/ui/DropTreeView.cpp`
- [x] 修改 `src/ui/ContentPanel.cpp`

## 6. 实现准则与预警【核心】
通过 `m_emptyHint` 的设置判断使侧边栏使用的 `DropTreeView` 不受污染，做到架构层完全解耦。

## 7. Memories.md 合规检查
（无）

## 8. 待确认事项
（无）
