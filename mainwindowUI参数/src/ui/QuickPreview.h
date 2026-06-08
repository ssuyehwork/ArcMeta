#ifndef QUICKPREVIEW_H
#define QUICKPREVIEW_H

#include <QWidget>
#include "StringUtils.h"

#include <QTextEdit>
#include <QVariant>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QTextDocument>
#include <QTextCursor>
#include <QSettings>
#include "ToolTipOverlay.h"
#include <QCursor>
#include <QFrame>
#include <QShortcut>
#include <QAction>
#include <QScreen>
#include <QGuiApplication>
#include <QLineEdit>
#include <QSplitter>
#include "IconHelper.h"
#include "../core/ShortcutManager.h"
#include "../core/DatabaseManager.h"
#include <QMimeData>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

class QuickPreview : public QWidget {
    Q_OBJECT
public:
    static QuickPreview* instance() {
        static QuickPreview* inst = nullptr;
        if (!inst) {
            inst = new QuickPreview();
        }
        return inst;
    }

    QWidget* caller() const { return m_focusBackWidget; }

signals:
    void editRequested(int noteId);
    void prevRequested();
    void nextRequested();
    void historyNavigationRequested(int noteId);

private:
    explicit QuickPreview(QWidget* parent = nullptr) : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint) {
        setObjectName("QuickPreview");
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::StrongFocus);
        
        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(12, 12, 12, 12);

        m_container = new QFrame();
        m_container->setObjectName("previewContainer");
        m_container->setStyleSheet(
            "QFrame#previewContainer { background-color: #1e1e1e; border: 1px solid #444; border-radius: 8px; }"
            "QFrame#previewTitleBar { background-color: #1e1e1e; border-top-left-radius: 7px; border-top-right-radius: 7px; border-bottom: 1px solid #333; }"
            "QTextEdit { border-bottom-left-radius: 7px; border-bottom-right-radius: 7px; background: transparent; border: none; color: #ddd; padding: 0px; }" 
            "QScrollBar:vertical { width: 6px; background: transparent; margin: 0px; }"
            "QScrollBar::handle:vertical { background: #444; border-radius: 3px; min-height: 20px; }"
            "QScrollBar::handle:vertical:hover { background: #555; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
            "QPushButton { border: none; border-radius: 4px; background: transparent; padding: 4px; }"
            "QPushButton:hover { background-color: #3e3e3e; }"
            "QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }"
            "QPushButton#btnClose:hover { background-color: #E81123; }"
        );
        
        auto* containerLayout = new QVBoxLayout(m_container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        m_titleBar = new QFrame();
        m_titleBar->setObjectName("previewTitleBar");
        m_titleBar->setFixedHeight(36);
        m_titleBar->setAttribute(Qt::WA_StyledBackground);
        auto* titleLayout = new QHBoxLayout(m_titleBar);
        titleLayout->setContentsMargins(10, 0, 5, 0);
        titleLayout->setSpacing(5);

        m_titleLabel = new QLabel("预览");
        m_titleLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold;");
        titleLayout->addWidget(m_titleLabel);

        m_searchEdit = new QLineEdit();
        m_searchEdit->setFocusPolicy(Qt::StrongFocus);
        m_searchEdit->setPlaceholderText("查找内容...");
        m_searchEdit->setFixedWidth(250);
        
        QAction* searchAction = new QAction(this);
        searchAction->setIcon(IconHelper::getIcon("search", "#888888"));
        m_searchEdit->addAction(searchAction, QLineEdit::LeadingPosition);
        
        m_searchEdit->setStyleSheet(
            "QLineEdit {"
            "  background-color: #2d2d2d; color: #eee; border: 1px solid #555; border-radius: 6px;"
            "  padding: 2px 10px; font-size: 12px;"
            "}"
            "QLineEdit:focus {"
            "  background-color: #383838; border-color: #007acc; color: #fff;"
            "}"
            "QLineEdit::placeholder { color: #666; }"
        );
        titleLayout->addSpacing(20);
        titleLayout->addWidget(m_searchEdit);

        m_searchCountLabel = new QLabel("0 / 0");
        m_searchCountLabel->setStyleSheet("color: #007acc; font-size: 11px; font-weight: bold; margin-left: 5px;");
        titleLayout->addWidget(m_searchCountLabel);

        titleLayout->addStretch();

        auto createBtn = [this](const QString& icon, const QString& tooltip, const QString& objName = "", const QString& color = "#aaaaaa") {
            QPushButton* btn = new QPushButton();
            btn->setIcon(IconHelper::getIcon(icon, color));
            btn->setIconSize(QSize(16, 16));
            btn->setFixedSize(32, 32);
            btn->setProperty("tooltipText", tooltip);
            if (!objName.isEmpty()) btn->setObjectName(objName);
            btn->installEventFilter(this);
            return btn;
        };

        m_btnBack = createBtn("nav_first", "后退", "btnBack");
        m_btnBack->setFocusPolicy(Qt::NoFocus);
        m_btnForward = createBtn("nav_last", "前进", "btnForward");
        m_btnForward->setFocusPolicy(Qt::NoFocus);

        QPushButton* btnPrev = createBtn("nav_prev", "上一个", "btnPrev");
        btnPrev->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnNext = createBtn("nav_next", "下一个", "btnNext");
        btnNext->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnCopy = createBtn("copy", "复制内容", "btnCopy");
        btnCopy->setFocusPolicy(Qt::NoFocus);
        
        // [MODIFIED] 按照用户要求：放入回收站按钮使用红色图标 #e74c3c
        QPushButton* btnTrash = createBtn("trash", "放入回收站", "btnTrash", "#e74c3c");
        btnTrash->setFocusPolicy(Qt::NoFocus);
        
        QPushButton* btnEdit = createBtn("edit", "编辑内容", "btnEdit");
        btnEdit->setFocusPolicy(Qt::NoFocus);
        
        m_btnPin = createBtn("pin_tilted", "置顶显示", "btnPin");
        m_btnPin->setCheckable(true);
        m_btnPin->setFocusPolicy(Qt::NoFocus);
        
        QSettings settings("RapidNotes", "WindowStates");
        m_isPinned = settings.value("QuickPreview/StayOnTop", false).toBool();
        if (m_isPinned) {
            m_btnPin->setChecked(true);
            m_btnPin->setIcon(IconHelper::getIcon("pin_vertical", "#FF551C"));
            setWindowFlag(Qt::WindowStaysOnTopHint, true);
        }

        QPushButton* btnMin = createBtn("minimize", "最小化", "btnMin");
        btnMin->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnMax = createBtn("maximize", "最大化", "btnMax");
        btnMax->setFocusPolicy(Qt::NoFocus);
        QPushButton* btnClose = createBtn("close", "关闭预览", "btnClose");
        btnClose->setFocusPolicy(Qt::NoFocus);

        connect(m_btnBack, &QPushButton::clicked, this, &QuickPreview::navigateBack);
        connect(m_btnForward, &QPushButton::clicked, this, &QuickPreview::navigateForward);
        connect(btnPrev, &QPushButton::clicked, this, &QuickPreview::prevRequested);
        connect(btnNext, &QPushButton::clicked, this, &QuickPreview::nextRequested);
        connect(btnCopy, &QPushButton::clicked, this, &QuickPreview::copyFullContent);
        connect(btnTrash, &QPushButton::clicked, this, &QuickPreview::deleteCurrentNote);
        connect(m_btnPin, &QPushButton::toggled, [this](bool checked) {
            m_isPinned = checked;
#ifdef Q_OS_WIN
            HWND hwnd = (HWND)winId();
            SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
            setWindowFlag(Qt::WindowStaysOnTopHint, m_isPinned);
            show();
#endif
            m_btnPin->setIcon(IconHelper::getIcon(m_isPinned ? "pin_vertical" : "pin_tilted", m_isPinned ? "#FF551C" : "#aaaaaa"));
            QSettings settings("RapidNotes", "WindowStates");
            settings.setValue("QuickPreview/StayOnTop", m_isPinned);
        });

        connect(btnEdit, &QPushButton::clicked, [this]() {
            emit editRequested(m_currentNoteId);
        });
        connect(btnMin, &QPushButton::clicked, this, &QuickPreview::showMinimized);
        connect(btnMax, &QPushButton::clicked, [this]() {
            if (isMaximized()) showNormal();
            else showMaximized();
        });
        connect(btnClose, &QPushButton::clicked, this, &QuickPreview::hide);

        titleLayout->addWidget(m_btnBack);
        titleLayout->addWidget(m_btnForward);
        titleLayout->addSpacing(5);
        titleLayout->addWidget(btnPrev);
        titleLayout->addWidget(btnNext);
        titleLayout->addSpacing(5);
        titleLayout->addWidget(btnCopy);
        titleLayout->addSpacing(15);
        
        // [MODIFIED] 按照用户最新要求，重排标题栏按钮物理顺序。
        // 从右往左依次为：关闭、最大化、最小化、置顶、编辑、回收站
        // 在 QHBoxLayout 中，后添加的在右侧，故按以下逆序添加：
        titleLayout->addWidget(btnTrash);
        titleLayout->addWidget(btnEdit);
        titleLayout->addWidget(m_btnPin);
        titleLayout->addWidget(btnMin);
        titleLayout->addWidget(btnMax);
        titleLayout->addWidget(btnClose);

        containerLayout->addWidget(m_titleBar);

        connect(m_searchEdit, &QLineEdit::textChanged, this, &QuickPreview::performSearch);
        connect(m_searchEdit, &QLineEdit::returnPressed, this, &QuickPreview::findNext);
        m_searchEdit->installEventFilter(this);

        m_textEdit = new QTextEdit();
        m_textEdit->document()->setDocumentMargin(12);
        m_textEdit->setReadOnly(true);
        m_textEdit->setFocusPolicy(Qt::StrongFocus);
        QFont previewFont = m_textEdit->font();
        previewFont.setPointSize(12);
        m_textEdit->setFont(previewFont);
        m_textEdit->installEventFilter(this); 
        if (m_textEdit->viewport()) {
            m_textEdit->viewport()->installEventFilter(this);
        }

        m_metaPanel = new QWidget();
        m_metaPanel->setObjectName("previewMetaPanel");
        m_metaPanel->setMinimumWidth(0);
        m_metaPanel->setMaximumWidth(230); 
        m_metaPanel->setStyleSheet(
            "QWidget#previewMetaPanel { background-color: #161616; border-left: 1px solid #333; border-bottom-right-radius: 7px; }"
            "QLabel { color: #ccc; font-size: 12px; background: transparent; border: none; }"
        );
        auto* metaLayout = new QVBoxLayout(m_metaPanel);
        metaLayout->setContentsMargins(12, 14, 12, 14);
        metaLayout->setSpacing(0);

        auto addMetaRow = [&](const QString& iconName, const QString& labelText, QLabel*& valueLabel) {
            auto* row = new QWidget();
            row->setStyleSheet("background: transparent;");
            auto* rowLayout = new QVBoxLayout(row);
            rowLayout->setContentsMargins(0, 8, 0, 8);
            rowLayout->setSpacing(3);

            auto* header = new QHBoxLayout();
            header->setSpacing(5);
            auto* iconLbl = new QLabel();
            iconLbl->setPixmap(IconHelper::getIcon(iconName, "#666", 12).pixmap(12, 12));
            auto* keyLbl = new QLabel(labelText);
            keyLbl->setStyleSheet("color: #666; font-size: 11px;");
            header->addWidget(iconLbl);
            header->addWidget(keyLbl);
            header->addStretch();

            valueLabel = new QLabel("-");
            valueLabel->setWordWrap(true);
            valueLabel->setStyleSheet("color: #ddd; font-size: 12px; font-weight: bold;");

            auto* sepLine = new QFrame();
            sepLine->setFrameShape(QFrame::HLine);
            sepLine->setStyleSheet("background: #2a2a2a; max-height: 1px; border: none;");

            rowLayout->addLayout(header);
            rowLayout->addWidget(valueLabel);
            rowLayout->addWidget(sepLine);
            metaLayout->addWidget(row);
        };

        addMetaRow("text",       "标题",   m_metaTitle);
        addMetaRow("branch",     "分类",   m_metaCategory);
        addMetaRow("tag",        "标签",   m_metaTags);
        addMetaRow("star",       "评级",   m_metaRating);
        addMetaRow("pin_vertical", "状态",   m_metaStatus);
        addMetaRow("calendar",   "创建于", m_metaCreated);
        addMetaRow("edit",       "更新于", m_metaUpdated);

        // 备注单独处理
        m_metaRemarkRow = new QWidget();
        auto* remarkRow = m_metaRemarkRow;
        remarkRow->setStyleSheet("background: transparent;");
        auto* remarkRowLayout = new QVBoxLayout(remarkRow);
        remarkRowLayout->setContentsMargins(0, 8, 0, 8);
        remarkRowLayout->setSpacing(3);
        auto* remarkHeaderLayout = new QHBoxLayout();
        remarkHeaderLayout->setSpacing(5);
        auto* riIcon = new QLabel();
        riIcon->setPixmap(IconHelper::getIcon("edit", "#666", 12).pixmap(12, 12));
        auto* riKey = new QLabel("备注");
        riKey->setStyleSheet("color: #666; font-size: 11px;");
        remarkHeaderLayout->addWidget(riIcon);
        remarkHeaderLayout->addWidget(riKey);
        remarkHeaderLayout->addStretch();
        m_metaRemark = new QLabel("-");
        m_metaRemark->setWordWrap(true);
        m_metaRemark->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_metaRemark->setStyleSheet("color: #4fc3f7; font-size: 12px; font-style: italic;");
        remarkRowLayout->addLayout(remarkHeaderLayout);
        remarkRowLayout->addWidget(m_metaRemark);
        metaLayout->addWidget(remarkRow);

        m_metaTitle->parentWidget()->installEventFilter(this);
        m_metaCategory->parentWidget()->installEventFilter(this);
        m_metaTags->parentWidget()->installEventFilter(this);
        m_metaRating->parentWidget()->installEventFilter(this);
        m_metaStatus->parentWidget()->installEventFilter(this);
        m_metaCreated->parentWidget()->installEventFilter(this);
        m_metaUpdated->parentWidget()->installEventFilter(this);
        m_metaRemarkRow->installEventFilter(this);
        metaLayout->addStretch();

        auto* contentSplitter = new QSplitter(Qt::Horizontal);
        contentSplitter->setHandleWidth(0);
        contentSplitter->addWidget(m_textEdit);
        contentSplitter->addWidget(m_metaPanel);
        contentSplitter->setStretchFactor(0, 1);
        contentSplitter->setStretchFactor(1, 0);
        contentSplitter->setSizes({870, 230}); 
        contentSplitter->setStyleSheet("QSplitter { background: transparent; }");
        
        containerLayout->addWidget(contentSplitter);
        
        mainLayout->addWidget(m_container);
        
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 120));
        shadow->setOffset(0, 4);
        m_container->setGraphicsEffect(shadow);
        
        resize(1100, 720);

        setupShortcuts();
        updateShortcuts(); // 初始同步快捷键提示
        connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &QuickPreview::updateShortcuts);
    }

public:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            // [USER_REQUEST] 预览窗口内按下空格键应关闭预览，与外部开启逻辑形成闭环
            if (keyEvent->key() == Qt::Key_Space) {
                hide();
                return true;
            }

            // [CRITICAL] 物理拦截 Ctrl+C：锁定局部复制逻辑。
            // 解决 QTextEdit 默认将富文本源码存入剪贴板的“傻逼逻辑”。
            if (keyEvent->key() == Qt::Key_C && (keyEvent->modifiers() & Qt::ControlModifier)) {
                copyFullContent();
                return true;
            }
        }
        if (event->type() == QEvent::ToolTip) {
            auto* helpEvent = static_cast<QHelpEvent*>(event);
            auto* widget = qobject_cast<QWidget*>(watched);
            if (widget && !widget->property("tooltipText").toString().isEmpty()) {
                // 2026-03-xx 按照用户要求，按钮/组件 ToolTip 持续时间设为 2 秒 (2000ms)
                ToolTipOverlay::instance()->showText(helpEvent->globalPos(), widget->property("tooltipText").toString(), 2000);
            }
            // [CRITICAL] 物理级拦截：无论是否有 tooltipText，只要是 ToolTip 事件都必须截断，严禁原生回退
            return true;
        }
        if (event->type() == QEvent::Wheel) {
            if (watched == m_textEdit || (m_textEdit && watched == m_textEdit->viewport())) {
                auto* wheelEvent = static_cast<QWheelEvent*>(event);
                if (wheelEvent->modifiers() & Qt::ControlModifier) {
                    if (wheelEvent->angleDelta().y() > 0) m_textEdit->zoomIn(1);
                    else m_textEdit->zoomOut(1);
                    double factor = m_textEdit->font().pointSizeF() / 12.0;
                    QString html = StringUtils::generateNotePreviewHtml(m_currentTitle, m_pureContent, m_currentType, m_currentData, factor);
                    m_textEdit->setHtml(html);
                    return true;
                }
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void showPreview(const QVariantMap& note, const QPoint& pos, const QString& catName = "", QWidget* caller = nullptr) {
        if (caller) m_focusBackWidget = caller;
        
        int noteId = note.value("id").toInt();
        m_currentNoteId = noteId;
        m_currentTitle = note.value("title").toString();
        m_currentType = note.value("item_type").toString();
        m_currentData = note.value("data_blob").toByteArray();
        m_pureContent = note.value("content").toString();

        if (m_searchEdit) m_searchEdit->clear();
        addToHistory(noteId);
        m_titleLabel->setText(catName.isEmpty() ? "预览" : QString("预览 - %1").arg(catName));
        
        QString html = StringUtils::generateNotePreviewHtml(m_currentTitle, m_pureContent, m_currentType, m_currentData);
        m_textEdit->setHtml(html);

        if (m_metaTitle)    m_metaTitle->setText(m_currentTitle.isEmpty() ? "-" : m_currentTitle);
        if (m_metaCategory) m_metaCategory->setText(catName.isEmpty() ? "未分类" : catName);
        if (m_metaTags)     m_metaTags->setText(note.value("tags").toString().trimmed().isEmpty() ? "无" : note.value("tags").toString().trimmed());
        if (m_metaRating) {
            int r = note.value("rating").toInt();
            m_metaRating->setText(r > 0 ? QString("★").repeated(r) : "无");
            m_metaRating->setStyleSheet(r > 0 ? "color: #FFD700; font-size: 14px; font-weight: bold;" : "color: #555; font-size: 12px; font-weight: normal;");
        }
        if (m_metaStatus) {
            QStringList s;
            if (note.value("is_pinned").toInt())   s << "📌 置顶";
            if (note.value("is_favorite").toInt()) s << "🔖 收藏";
            // 2026-03-xx 按照用户要求：彻底移除笔记级锁定标识显示
            m_metaStatus->setText(s.isEmpty() ? "未置顶" : s.join("  "));
        }
        if (m_metaCreated) m_metaCreated->setText(note.value("created_at").toString().left(16).replace("T", " "));
        if (m_metaUpdated) m_metaUpdated->setText(note.value("updated_at").toString().left(16).replace("T", " "));
        if (m_metaRemark) {
            QString remark = note.value("remark").toString().trimmed();
            m_metaRemark->setText(remark.isEmpty() ? "-" : remark);
            m_metaRemarkRow->setProperty("tooltipText", remark.isEmpty() ? "暂无备注" : "[!] 备注: " + remark);
        }

        QPoint adjustedPos = pos;
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        bool wasHidden = !isVisible();

        if (wasHidden && screen) {
            QRect screenGeom = screen->availableGeometry();
            adjustedPos = screenGeom.center() - QRect(0, 0, width(), height()).center();
        }

        move(adjustedPos);
        show();
        if (wasHidden) setFocus();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && m_titleBar->rect().contains(m_titleBar->mapFrom(this, event->pos()))) {
            m_dragging = true;
            m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_dragging && event->buttons() & Qt::LeftButton) {
            move(event->globalPosition().toPoint() - m_dragPos);
            event->accept();
        }
    }
    void mouseReleaseEvent(QMouseEvent* event) override { m_dragging = false; }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (m_titleBar->rect().contains(m_titleBar->mapFrom(this, event->pos()))) {
            if (isMaximized()) showNormal();
            else showMaximized();
        }
    }

    void setupShortcuts() {
        auto add = [&](const QString& id, std::function<void()> func) {
            auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func, Qt::WidgetWithChildrenShortcut);
            sc->setProperty("id", id);
            m_shortcuts.append(sc);
        };
        add("pv_prev", [this](){ emit prevRequested(); });
        add("pv_next", [this](){ emit nextRequested(); });
        add("pv_back", [this](){ navigateBack(); });
        add("pv_forward", [this](){ navigateForward(); });
        add("pv_edit", [this](){ emit editRequested(m_currentNoteId); });
        add("pv_delete", [this](){ deleteCurrentNote(); });
        add("pv_copy", [this](){ copyFullContent(); });
        add("pv_close", [this](){ hide(); });
        add("pv_search", [this](){ toggleSearch(true); });
    }

    void updateShortcuts() {
        for (auto* sc : m_shortcuts) {
            sc->setKey(ShortcutManager::instance().getShortcut(sc->property("id").toString()));
        }
        auto getScHint = [](const QString& id) -> QString {
            QKeySequence seq = ShortcutManager::instance().getShortcut(id);
            return seq.isEmpty() ? "" : QString(" （%1）").arg(seq.toString(QKeySequence::NativeText).replace("+", " + "));
        };
        auto updateBtnTip = [&](const QString& objName, const QString& baseTip, const QString& scId) {
            QPushButton* btn = findChild<QPushButton*>(objName);
            if (btn) btn->setProperty("tooltipText", baseTip + (scId.isEmpty() ? "" : getScHint(scId)));
        };
        updateBtnTip("btnBack", "后退", "pv_back");
        updateBtnTip("btnForward", "前进", "pv_forward");
        updateBtnTip("btnPrev", "上一个项目", "pv_prev");
        updateBtnTip("btnNext", "下一个项目", "pv_next");
        updateBtnTip("btnCopy", "复制", "pv_copy");
        updateBtnTip("btnTrash", "放入回收站", "pv_delete");
        updateBtnTip("btnEdit", "编辑项目", "pv_edit");
        updateBtnTip("btnClose", "关闭预览", "pv_close");
    }

    void navigateBack() {
        if (m_historyIndex > 0) {
            m_historyIndex--;
            m_isNavigatingHistory = true;
            emit historyNavigationRequested(m_history.at(m_historyIndex));
            m_isNavigatingHistory = false;
            updateHistoryButtons();
        }
    }
    void navigateForward() {
        if (m_historyIndex < m_history.size() - 1) {
            m_historyIndex++;
            m_isNavigatingHistory = true;
            emit historyNavigationRequested(m_history.at(m_historyIndex));
            m_isNavigatingHistory = false;
            updateHistoryButtons();
        }
    }

    void deleteCurrentNote() {
        if (m_currentNoteId <= 0) return;
        if (DatabaseManager::instance().softDeleteNotes({m_currentNoteId})) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>已移至回收站</b>", 700);
            // 2026-03-xx 按照用户要求：删除后不关闭窗口，而是跳转到下一条
            emit nextRequested();
        }
    }

    void toggleSearch(bool show) {
        if (show) { m_searchEdit->setFocus(); m_searchEdit->selectAll(); }
        else { m_searchEdit->clear(); m_textEdit->setExtraSelections({}); m_textEdit->setFocus(); }
    }

    void performSearch(const QString& text) {
        if (text.isEmpty()) { m_textEdit->setExtraSelections({}); return; }
        QList<QTextEdit::ExtraSelection> selections;
        QTextCursor originalCursor = m_textEdit->textCursor();
        m_textEdit->moveCursor(QTextCursor::Start);
        while (m_textEdit->find(text)) {
            QTextEdit::ExtraSelection s;
            s.format.setBackground(QColor(255, 255, 0, 100));
            s.cursor = m_textEdit->textCursor();
            selections.append(s);
            if (selections.size() > 1000) break;
        }
        m_textEdit->setExtraSelections(selections);
        m_textEdit->setTextCursor(originalCursor);
        updateSearchCount();
    }
    void findNext() { if (!m_textEdit->find(m_searchEdit->text())) { m_textEdit->moveCursor(QTextCursor::Start); m_textEdit->find(m_searchEdit->text()); } updateSearchCount(); }
    void updateSearchCount() {
        int total = m_textEdit->extraSelections().size();
        if (m_searchCountLabel) m_searchCountLabel->setText(QString("匹配: %1").arg(total));
    }

    void copyFullContent() {
        // [MODIFIED] 2026-03-xx 优化复制逻辑：优先复制选中的局部内容，否则复制全部
        QString textToCopy;
        if (m_textEdit->textCursor().hasSelection()) {
            textToCopy = m_textEdit->textCursor().selectedText();
            // QTextCursor::selectedText() 会将 Unicode 段落分隔符替换为 U+2029，需转回普通换行
            textToCopy.replace(QChar(u'\u2029'), QChar('\n'));
        } else {
            textToCopy = m_pureContent.isEmpty() ? m_textEdit->toPlainText() : m_pureContent;
        }

        if (!textToCopy.isEmpty()) {
            // [CRITICAL] 使用 StringUtils::copyNoteToClipboard 统一处理，确保不会复制到 HTML 源码标签
            StringUtils::copyNoteToClipboard(textToCopy);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>已复制到剪贴板</b>", 700);
        }
    }

    void addToHistory(int id) {
        if (m_isNavigatingHistory || (!m_history.isEmpty() && m_history.last() == id)) return;
        m_history.append(id);
        m_historyIndex = m_history.size() - 1;
        updateHistoryButtons();
    }
    void updateHistoryButtons() {
        if (m_btnBack) m_btnBack->setEnabled(m_historyIndex > 0);
        if (m_btnForward) m_btnForward->setEnabled(m_historyIndex < m_history.size() - 1);
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) { hide(); event->accept(); return; }
        if (event->key() == Qt::Key_Delete) { deleteCurrentNote(); event->accept(); return; }
        QWidget::keyPressEvent(event);
    }

    void hideEvent(QHideEvent* event) override {
        if (m_focusBackWidget) { m_focusBackWidget->activateWindow(); m_focusBackWidget->setFocus(); }
        QWidget::hideEvent(event);
    }

private:
    QFrame* m_container;
    QList<QShortcut*> m_shortcuts;
    QWidget* m_titleBar;
    QLabel* m_titleLabel;
    QLineEdit* m_searchEdit = nullptr;
    QLabel* m_searchCountLabel = nullptr;
    QTextEdit* m_textEdit;
    QString m_pureContent, m_currentTitle, m_currentType;
    QByteArray m_currentData;
    int m_currentNoteId = -1;
    bool m_dragging = false, m_isPinned = false, m_isNavigatingHistory = false;
    QPushButton *m_btnPin = nullptr, *m_btnBack = nullptr, *m_btnForward = nullptr;
    QPoint m_dragPos;
    QWidget* m_focusBackWidget = nullptr;
    QList<int> m_history;
    int m_historyIndex = -1;
    QWidget *m_metaPanel = nullptr, *m_metaRemarkRow = nullptr;
    QLabel *m_metaTitle = nullptr, *m_metaCategory = nullptr, *m_metaTags = nullptr, *m_metaRating = nullptr, *m_metaStatus = nullptr, *m_metaCreated = nullptr, *m_metaUpdated = nullptr, *m_metaRemark = nullptr;
};

#endif // QUICKPREVIEW_H
