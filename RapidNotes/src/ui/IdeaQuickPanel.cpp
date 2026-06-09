#include "IdeaQuickPanel.h"
#include "../core/DatabaseManager.h"
#include "../core/ClipboardMonitor.h"
#include "StringUtils.h"
#include "IconHelper.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QClipboard>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QListWidgetItem>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// ──────────────────────────────────────────────
//  常量
// ──────────────────────────────────────────────
static constexpr int kPanelWidth      = 400;   // 浮窗固定宽度（px）
static constexpr int kMaxVisibleItems = 8;     // 不出滚动条时最多显示几行
static constexpr int kItemHeight      = 58;    // 每行高度（px）
static constexpr int kPageSize        = 20;    // 最多加载条数
static constexpr int kSearchDelay     = 180;   // 搜索防抖延迟（ms）

// ──────────────────────────────────────────────
//  单例
// ──────────────────────────────────────────────
IdeaQuickPanel* IdeaQuickPanel::s_instance = nullptr;

IdeaQuickPanel* IdeaQuickPanel::instance() {
    if (!s_instance) {
        s_instance = new IdeaQuickPanel();
    }
    return s_instance;
}

// ──────────────────────────────────────────────
//  构造
// ──────────────────────────────────────────────
IdeaQuickPanel::IdeaQuickPanel(QWidget* parent)
    : QWidget(parent,
              Qt::Tool                  // Tool 窗口：不抢主窗口激活状态
              | Qt::FramelessWindowHint
              | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFixedWidth(kPanelWidth);

    // ── 外层容器（用于 shadow 留边）──────────────────
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(12, 12, 12, 12);   // shadow 留白
    outerLayout->setSpacing(0);

    auto* card = new QWidget();
    card->setObjectName("iqpCard");
    outerLayout->addWidget(card);

    // ── 阴影 ──────────────────────────────────────
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(24);
    shadow->setColor(QColor(0, 0, 0, 140));
    shadow->setOffset(0, 6);
    card->setGraphicsEffect(shadow);

    // ── 卡片内布局 ─────────────────────────────────
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    cardLayout->setSpacing(8);

    // ── 全局样式 ───────────────────────────────────
    card->setStyleSheet(R"(
        QWidget#iqpCard {
            background-color: #1E1E1E;
            border-radius: 10px;
            border: 1px solid #383838;
        }

        QLineEdit#iqpSearch {
            background-color: #252526;
            border: 1px solid #3A3A3A;
            border-radius: 6px;
            padding: 5px 10px 5px 32px;
            color: #E8E8E8;
            font-size: 13px;
        }
        QLineEdit#iqpSearch:focus {
            border-color: #4a90e2;
            background-color: #2A2A2A;
        }
        QLineEdit#iqpSearch::placeholder {
            color: #555;
        }

        QListWidget#iqpList {
            background: transparent;
            border: none;
            outline: none;
            color: #CCC;
        }
        QListWidget#iqpList::item {
            border-bottom: 1px solid #252525;
            padding: 4px 6px;
            border-radius: 5px;
        }
        QListWidget#iqpList::item:hover {
            background-color: #2A2D30;
        }
        QListWidget#iqpList::item:selected {
            background-color: #094771;
            color: #FFFFFF;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 5px;
            margin: 4px 0;
        }
        QScrollBar::handle:vertical {
            background: #444;
            border-radius: 2px;
            min-height: 24px;
        }
        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical,
        QScrollBar::sub-page:vertical { background: none; }
    )");

    // ── Header 行 ─────────────────────────────────
    {
        auto* headerRow = new QHBoxLayout();
        headerRow->setContentsMargins(4, 0, 4, 0);

        auto* iconLabel = new QLabel("⚡");
        iconLabel->setStyleSheet(
            "color: #3A90FF; font-size: 14px; background: transparent; border: none;");

        auto* titleLabel = new QLabel("灵感快取");
        titleLabel->setStyleSheet(
            "color: #3A90FF; font-size: 13px; font-weight: bold; "
            "background: transparent; border: none;");

        m_countLabel = new QLabel();
        m_countLabel->setStyleSheet(
            "color: #555; font-size: 11px; background: transparent; border: none;");
        m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        headerRow->addWidget(iconLabel);
        headerRow->addSpacing(4);
        headerRow->addWidget(titleLabel);
        headerRow->addStretch();
        headerRow->addWidget(m_countLabel);
        cardLayout->addLayout(headerRow);
    }

    // ── 搜索框 ─────────────────────────────────────
    {
        m_searchEdit = new QLineEdit();
        m_searchEdit->setObjectName("iqpSearch");
        m_searchEdit->setFixedHeight(34);
        m_searchEdit->setPlaceholderText("过滤...  ↑↓ 导航  Enter 粘贴  Esc 关闭");
        m_searchEdit->setClearButtonEnabled(true);
        m_searchEdit->installEventFilter(this);    // 截获上下键
        cardLayout->addWidget(m_searchEdit);
    }

    // ── 列表 ───────────────────────────────────────
    {
        m_listWidget = new QListWidget();
        m_listWidget->setObjectName("iqpList");
        m_listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_listWidget->setUniformItemSizes(true);
        m_listWidget->setSpacing(1);

        // 最多显示 kMaxVisibleItems 行，超出后出现滚动条
        m_listWidget->setFixedHeight(kMaxVisibleItems * kItemHeight + 8);

        cardLayout->addWidget(m_listWidget);
    }

    // ── 底部提示行 ─────────────────────────────────
    {
        auto* hintLabel = new QLabel("按 更新时间 排序");
        hintLabel->setStyleSheet(
            "color: #3A3A3A; font-size: 10px; background: transparent; border: none;");
        hintLabel->setAlignment(Qt::AlignCenter);
        cardLayout->addWidget(hintLabel);
    }

    // ── 搜索防抖定时器 ──────────────────────────────
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(kSearchDelay);

    // ── 信号连接 ───────────────────────────────────
    connect(m_searchTimer, &QTimer::timeout, this, [this]() {
        loadNotes(m_searchEdit->text().trimmed());
    });

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        m_searchTimer->start();
    });

    connect(m_searchEdit, &QLineEdit::returnPressed, this, [this]() {
        // 搜索框回车：如果没有选中行则默认第一行
        if (m_listWidget->currentRow() < 0 && m_listWidget->count() > 0) {
            m_listWidget->setCurrentRow(0);
        }
        activateCurrentItem();
    });

    connect(m_listWidget, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        activateCurrentItem();
    });

    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        activateCurrentItem();
    });
}

IdeaQuickPanel::~IdeaQuickPanel() {
    s_instance = nullptr;
}

// ──────────────────────────────────────────────
//  popup — 对外入口
// ──────────────────────────────────────────────
void IdeaQuickPanel::popup(const QPoint& globalPos) {
    // 1. 如果已经可见，第二次按 Alt+A → 关闭（toggle）
    if (isVisible()) {
        hide();
        return;
    }

    // 2. 捕获当前前台窗口，用于粘贴目标
#ifdef Q_OS_WIN
    m_prevHwnd = GetForegroundWindow();
    if (m_prevHwnd) {
        m_prevThreadId = GetWindowThreadProcessId(m_prevHwnd, nullptr);
        GUITHREADINFO gti;
        gti.cbSize = sizeof(GUITHREADINFO);
        m_prevFocusHwnd = GetGUIThreadInfo(m_prevThreadId, &gti) ? gti.hwndFocus : nullptr;
    }
#endif

    // 3. 重置搜索框并加载数据
    m_searchEdit->blockSignals(true);
    m_searchEdit->clear();
    m_searchEdit->blockSignals(false);
    loadNotes();

    // 4. 定位并显示
    adjustPosition(globalPos);
    show();
    raise();
    activateWindow();
    m_searchEdit->setFocus();

    // 5. 默认选中第一行
    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

// ──────────────────────────────────────────────
//  loadNotes — 查询数据库并填充列表
// ──────────────────────────────────────────────
void IdeaQuickPanel::loadNotes(const QString& keyword) {
    // ── 查询 ──
    // 使用 filterType = "recently_updated"（需确认 DatabaseManager 支持此类型）
    // 若不支持，可改为 "all" 并在 DatabaseManager 侧添加 ORDER BY updated_at DESC 分支。
    //
    // 当前已知可用的查询接口：
    //   searchNotes(keyword, filterType, filterValue, page, pageSize, criteria)
    //
    // 【⚠ 接入说明】：
    //   请在 DatabaseManager::searchNotes() 内部对 filterType == "recently_updated"
    //   的情况追加 ORDER BY n.updated_at DESC，其余逻辑不变。

    QVariantMap criteria;
    auto notes = DatabaseManager::instance().searchNotes(
        keyword,
        "recently_updated",  // ← 新增的 filterType，见上方说明
        QVariant(-1),
        1,
        kPageSize,
        criteria
    );

    m_notes = notes;
    m_listWidget->clear();

    for (int i = 0; i < m_notes.size(); ++i) {
        const auto& note = m_notes.at(i);

        const QString title   = note.value("title").toString();
        const QString content = StringUtils::htmlToPlainText(
                                    note.value("content").toString()).trimmed();
        const QString type    = note.value("item_type").toString();
        const QString timeStr = note.value("updated_at").toString();

        // ── 时间友好显示 ──
        QString timeDisplay;
        {
            QDateTime dt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
            if (dt.isValid()) {
                qint64 secs = dt.secsTo(QDateTime::currentDateTime());
                if      (secs < 60)    timeDisplay = "刚刚";
                else if (secs < 3600)  timeDisplay = QString("%1 分钟前").arg(secs / 60);
                else if (secs < 86400) timeDisplay = QString("%1 小时前").arg(secs / 3600);
                else                   timeDisplay = dt.toString("MM-dd  HH:mm");
            }
        }

        // ── 类型图标前缀 ──
        QString typeTag;
        if      (type == "image")        typeTag = "[图] ";
        else if (type == "local_file")   typeTag = "[文件] ";
        else if (type == "local_folder") typeTag = "[目录] ";
        else if (type == "link")         typeTag = "[链接] ";

        // ── 正文预览（不超过 70 字，换行转空格）──
        QString preview = content.left(70).replace('\n', ' ');
        if (content.length() > 70) preview += "…";

        // ── 组合显示文本（两行：标题 + 预览 & 时间）──
        //  行1: [类型前缀] 标题
        //  行2: 预览内容 · 时间
        QString line1 = typeTag + (title.isEmpty() ? "（无标题）" : title);
        QString line2;
        if (!preview.isEmpty() && preview.trimmed() != title.trimmed()) {
            line2 = preview;
            if (!timeDisplay.isEmpty()) line2 += "   · " + timeDisplay;
        } else {
            line2 = timeDisplay;
        }

        auto* item = new QListWidgetItem(m_listWidget);
        item->setData(Qt::UserRole,        note.value("id").toInt());
        item->setData(Qt::UserRole + 1,    i);             // index for m_notes

        // 用富文本方式拼接（QListWidget 默认不支持 HTML，改用 userData 驱动 delegate）
        // 此处先用 "\n" 简单双行，视觉已足够清晰
        item->setText(line1 + "\n" + line2);
        item->setSizeHint(QSize(kPanelWidth - 24, kItemHeight));

        // 收藏 / 置顶 标记颜色
        if (note.value("is_pinned").toInt())   item->setForeground(QColor("#3A90FF"));
        if (note.value("is_favorite").toInt()) item->setForeground(QColor("#F5A623"));
    }

    // ── 更新条数标签 ──
    if (m_notes.isEmpty()) {
        m_countLabel->setText(keyword.isEmpty() ? "暂无数据" : "无匹配");
    } else {
        m_countLabel->setText(QString("%1 条").arg(m_notes.size()));
    }

    if (m_listWidget->count() > 0) {
        m_listWidget->setCurrentRow(0);
    }
}

// ──────────────────────────────────────────────
//  activateCurrentItem — 复制 + 粘贴
// ──────────────────────────────────────────────
void IdeaQuickPanel::activateCurrentItem() {
    QListWidgetItem* item = m_listWidget->currentItem();
    if (!item) return;

    int noteId = item->data(Qt::UserRole).toInt();
    if (noteId <= 0) return;

    // 先隐藏面板（避免遮挡粘贴目标）
    hide();

    // 取完整笔记数据
    QVariantMap note = DatabaseManager::instance().getNoteById(noteId);
    if (note.isEmpty()) return;

    DatabaseManager::instance().recordAccess(noteId);

    const QString   itemType = note.value("item_type").toString();
    const QString   content  = note.value("content").toString();
    const QByteArray blob    = note.value("data_blob").toByteArray();

    // ── 写入剪贴板 ──
    if (itemType == "image") {
        QImage img;
        img.loadFromData(blob);
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setImage(img);
    } else {
        StringUtils::copyNoteToClipboard(content);
    }

    // ── 还原目标窗口焦点 + 自动粘贴（Windows）──
#ifdef Q_OS_WIN
    HWND target = m_prevHwnd;
    HWND focus  = m_prevFocusHwnd;
    DWORD tid   = m_prevThreadId;

    if (target && IsWindow(target)) {
        // 延迟 200ms，等待面板完全隐藏后再还原焦点并粘贴
        QTimer::singleShot(200, [target, focus, tid]() {
            DWORD currThread = GetCurrentThreadId();
            bool attached = (tid != 0 && tid != currThread)
                            ? AttachThreadInput(currThread, tid, TRUE)
                            : false;

            if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
            SetForegroundWindow(target);
            if (focus && IsWindow(focus)) SetFocus(focus);

            // 释放可能残留的修饰键，防止 Alt 键卡住
            INPUT rel[4]{};
            BYTE mods[] = { VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT };
            for (int i = 0; i < 4; ++i) {
                rel[i].type        = INPUT_KEYBOARD;
                rel[i].ki.wVk      = mods[i];
                rel[i].ki.dwFlags  = KEYEVENTF_KEYUP;
            }
            SendInput(4, rel, sizeof(INPUT));

            // 发送 Ctrl+V
            INPUT inputs[4]{};
            inputs[0].type       = INPUT_KEYBOARD;
            inputs[0].ki.wVk     = VK_LCONTROL;
            inputs[0].ki.wScan   = MapVirtualKey(VK_LCONTROL, MAPVK_VK_TO_VSC);

            inputs[1].type       = INPUT_KEYBOARD;
            inputs[1].ki.wVk     = 'V';
            inputs[1].ki.wScan   = MapVirtualKey('V', MAPVK_VK_TO_VSC);

            inputs[2]            = inputs[1];
            inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

            inputs[3]            = inputs[0];
            inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

            SendInput(4, inputs, sizeof(INPUT));

            if (attached) AttachThreadInput(currThread, tid, FALSE);
        });
    }
#endif
}

// ──────────────────────────────────────────────
//  adjustPosition — 防越屏定位
// ──────────────────────────────────────────────
void IdeaQuickPanel::adjustPosition(const QPoint& preferredPos) {
    QScreen* screen = QGuiApplication::screenAt(preferredPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect avail = screen->availableGeometry();

    // 先 adjustSize 确保 sizeHint 准确
    adjustSize();
    const QSize sz = size();

    // 默认在光标右下方偏移 12px
    int x = preferredPos.x() + 12;
    int y = preferredPos.y() + 12;

    // 右侧越屏 → 改为光标左侧
    if (x + sz.width() > avail.right() - 4)
        x = preferredPos.x() - sz.width() - 12;

    // 下方越屏 → 改为光标上方
    if (y + sz.height() > avail.bottom() - 4)
        y = preferredPos.y() - sz.height() - 12;

    // 最终夹在可用区域内
    x = qBound(avail.left() + 4, x, avail.right()  - sz.width()  - 4);
    y = qBound(avail.top()  + 4, y, avail.bottom() - sz.height() - 4);

    move(x, y);
}

// ──────────────────────────────────────────────
//  event — 失去焦点自动关闭
// ──────────────────────────────────────────────
bool IdeaQuickPanel::event(QEvent* e) {
    if (e->type() == QEvent::WindowDeactivate) {
        // 短暂延迟，避免点击列表时因焦点瞬移而意外关闭
        QTimer::singleShot(80, this, [this]() {
            // 再次确认焦点确实已离开本窗口及其子控件
            QWidget* fw = QApplication::focusWidget();
            if (fw && (fw == this || isAncestorOf(fw))) return;
            hide();
        });
    }
    return QWidget::event(e);
}

// ──────────────────────────────────────────────
//  keyPressEvent — 面板级快捷键
// ──────────────────────────────────────────────
void IdeaQuickPanel::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {

    case Qt::Key_Escape:
        hide();
        break;

    case Qt::Key_Up: {
        int row = m_listWidget->currentRow();
        if (row > 0) {
            m_listWidget->setCurrentRow(row - 1);
            m_listWidget->scrollToItem(m_listWidget->currentItem());
        }
        break;
    }

    case Qt::Key_Down: {
        int row = m_listWidget->currentRow();
        if (row < m_listWidget->count() - 1) {
            m_listWidget->setCurrentRow(row + 1);
            m_listWidget->scrollToItem(m_listWidget->currentItem());
        }
        break;
    }

    case Qt::Key_Return:
    case Qt::Key_Enter:
        activateCurrentItem();
        break;

    default:
        // 其他字符键：自动转发给搜索框，方便用户直接打字
        if (!e->text().isEmpty() && !(e->modifiers() & Qt::ControlModifier)) {
            m_searchEdit->setFocus();
            QApplication::sendEvent(m_searchEdit, e);
            return;
        }
        QWidget::keyPressEvent(e);
    }
}

// ──────────────────────────────────────────────
//  eventFilter — 搜索框内 ↑↓ 转发给列表
// ──────────────────────────────────────────────
bool IdeaQuickPanel::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Up || ke->key() == Qt::Key_Down) {
            // 把事件转发给面板自身的 keyPressEvent
            QApplication::sendEvent(this, ke);
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}
