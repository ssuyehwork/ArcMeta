收到需求。为了保证符合单一职责原则（SRP）且不破坏现有的 `ToolTipOverlay` 纯悬浮提示组件，我们将新建一个专用于“可撤销操作反馈”的交互式 Toast 组件：**`UndoToastOverlay`**。

---

### 一、 新组件设计规范（对标目标截图）

1. **外观与布局**：
   * **背景**：深灰底色（`#252526`），1px 边框（`#3E3E42`），6px 圆角，无操作系统原生阴影。
   * **图标（左侧）**：绿色圆底打勾图标（`#2ECC71`，16x16px）。
   * **提示文案（中部）**：高亮白色文本（如 `成功重命名 26 个项目`）。
   * **“撤销”按钮**：下划线/高亮操作按钮（`#EEEEEE`，悬浮变为 ActiveOrange `#FF551C`）。
   * **分割线**：1px 垂直细线（`#444444`）。
   * **“×”关闭按钮**：图标/文本按钮（`#888888`，悬浮变白）。
2. **生命周期与交互**：
   * 默认展示 **5,000 毫秒（5秒）**，倒计时结束自动淡出关闭。
   * 点击“撤销”：立即触发绑定的撤销回调函数（如调用 `UndoManager::instance().undo()`），并瞬时关闭 Toast。
   * 点击“×”：停止倒计时并关闭 Toast。

---

### 二、 施工图纸与步骤（交付给执行者实施）

#### 步骤 1：新建头文件 `src/ui/UndoToastOverlay.h`

```cpp
#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QTimer>
#include <QPropertyAnimation>
#include <functional>

namespace ArcMeta {

/**
 * @brief 可撤销操作反馈 Toast 浮窗 (Snackbar 风格)
 */
class UndoToastOverlay : public QWidget {
    Q_OBJECT
public:
    static UndoToastOverlay* instance();

    /**
     * @brief 弹出操作成功与撤销提示
     * @param parent 挂载的父窗口 (通常为 MainWindow)
     * @param message 提示文案（如 "成功重命名 26 个项目"）
     * @param undoCallback 点击“撤销”时的回调处理函数
     * @param durationMs 显示持续时间（默认 5000ms）
     */
    void showToast(QWidget* parent, 
                   const QString& message, 
                   std::function<void()> undoCallback, 
                   int durationMs = 5000);

    void hideToast();

protected:
    explicit UndoToastOverlay(QWidget* parent = nullptr);
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* m_iconLabel = nullptr;
    QLabel* m_msgLabel = nullptr;
    QPushButton* m_btnUndo = nullptr;
    QWidget* m_separator = nullptr;
    QPushButton* m_btnClose = nullptr;

    QTimer m_autoHideTimer;
    QPropertyAnimation* m_fadeAnim = nullptr;
    std::function<void()> m_undoCallback = nullptr;
};

} // namespace ArcMeta
```

---

#### 步骤 2：新建实现文件 `src/ui/UndoToastOverlay.cpp`

```cpp
#include "UndoToastOverlay.h"
#include "UiHelper.h"
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QScreen>

namespace ArcMeta {

UndoToastOverlay* UndoToastOverlay::instance() {
    static UndoToastOverlay* inst = new UndoToastOverlay(nullptr);
    return inst;
}

UndoToastOverlay::UndoToastOverlay(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(10);

    // 1. 成功绿勾图标
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(18, 18);
    m_iconLabel->setPixmap(UiHelper::getIcon("check_circle_filled", QColor("#2ECC71")).pixmap(18, 18));
    layout->addWidget(m_iconLabel);

    // 2. 消息文案
    m_msgLabel = new QLabel(this);
    m_msgLabel->setStyleSheet("color: #EEEEEE; font-size: 12px; font-family: 'Segoe UI', 'Microsoft YaHei';");
    layout->addWidget(m_msgLabel);

    // 3. 撤销按钮
    m_btnUndo = new QPushButton("撤销", this);
    m_btnUndo->setCursor(Qt::PointingHandCursor);
    m_btnUndo->setStyleSheet(
        "QPushButton { color: #FFFFFF; font-weight: bold; font-size: 12px; border: none; background: transparent; text-decoration: underline; }"
        "QPushButton:hover { color: #FF551C; }"
    );
    layout->addWidget(m_btnUndo);

    // 4. 垂直分割线
    m_separator = new QWidget(this);
    m_separator->setFixedSize(1, 14);
    m_separator->setStyleSheet("background-color: #4E4E52;");
    layout->addWidget(m_separator);

    // 5. 关闭按钮
    m_btnClose = new QPushButton("×", this);
    m_btnClose->setFixedSize(16, 16);
    m_btnClose->setCursor(Qt::PointingHandCursor);
    m_btnClose->setStyleSheet(
        "QPushButton { color: #888888; font-size: 14px; font-weight: bold; border: none; background: transparent; }"
        "QPushButton:hover { color: #FFFFFF; }"
    );
    layout->addWidget(m_btnClose);

    // 定时器与动画
    m_autoHideTimer.setSingleShot(true);
    connect(&m_autoHideTimer, &QTimer::timeout, this, &UndoToastOverlay::hideToast);

    m_fadeAnim = new QPropertyAnimation(this, "windowOpacity", this);
    m_fadeAnim->setDuration(200);

    // 按钮事件绑定
    connect(m_btnUndo, &QPushButton::clicked, this, [this]() {
        if (m_undoCallback) {
            m_undoCallback();
        }
        hideToast();
    });

    connect(m_btnClose, &QPushButton::clicked, this, &UndoToastOverlay::hideToast);

    hide();
}

void UndoToastOverlay::showToast(QWidget* parent, const QString& message, std::function<void()> undoCallback, int durationMs) {
    m_undoCallback = undoCallback;
    m_msgLabel->setText(message);
    m_btnUndo->setVisible(m_undoCallback != nullptr);
    m_separator->setVisible(m_undoCallback != nullptr);

    adjustSize();

    // 计算定位：位于 Screen/Parent 底部居中（距离底边 40px）
    QPoint targetPos;
    if (parent) {
        QRect parentGeom = parent->geometry();
        QPoint parentGlobal = parent->mapToGlobal(QPoint(0, 0));
        int x = parentGlobal.x() + (parentGeom.width() - width()) / 2;
        int y = parentGlobal.y() + parentGeom.height() - height() - 40;
        targetPos = QPoint(x, y);
    } else {
        QScreen* screen = QGuiApplication::primaryScreen();
        QRect screenGeom = screen->geometry();
        int x = screenGeom.x() + (screenGeom.width() - width()) / 2;
        int y = screenGeom.y() + screenGeom.height() - height() - 60;
        targetPos = QPoint(x, y);
    }

    move(targetPos);

    // 淡入显示
    m_fadeAnim->stop();
    setWindowOpacity(0.0);
    show();
    raise();

    m_fadeAnim->setStartValue(0.0);
    m_fadeAnim->setEndValue(1.0);
    m_fadeAnim->start();

    m_autoHideTimer.start(durationMs);
}

void UndoToastOverlay::hideToast() {
    m_autoHideTimer.stop();
    m_fadeAnim->stop();
    m_fadeAnim->setStartValue(windowOpacity());
    m_fadeAnim->setEndValue(0.0);
    
    disconnect(m_fadeAnim, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_fadeAnim, &QPropertyAnimation::finished, this, [this]() {
        hide();
        setWindowOpacity(1.0);
    });
    m_fadeAnim->start();
}

void UndoToastOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRectF rect(0.5, 0.5, width() - 1, height() - 1);
    p.setPen(QPen(QColor("#3E3E42"), 1));
    p.setBrush(QColor("#252526"));
    p.drawRoundedRect(rect, 6, 6);
}

} // namespace ArcMeta
```

---

#### 步骤 3：在 `CMakeLists.txt` 中添加源文件
在 `CMakeLists.txt` 的 `set(SOURCES ...)` 列表中加入：
```cmake
src/ui/UndoToastOverlay.h
src/ui/UndoToastOverlay.cpp
```

---

#### 步骤 4：在业务触发处（如批量重命名成功后）调用

当 `BatchRenameDialog` 批量重命名成功并关闭后，调用：

```cpp
UndoToastOverlay::instance()->showToast(
    mainWindowPtr, 
    QString("成功重命名 %1 个项目").arg(successCount),
    []() {
        // 点击“撤销”触发 UndoManager 撤销上一条指令
        UndoManager::instance().undo();
    },
    5000 // 5 秒后自动消失
);
```

---

图纸已完整生成，请授权执行者新建文件并编译接入。

应用场景：
重命名
批量重命名
拖拽分类
删除
添加至收藏
归类到...