#include "NoteEditWindow.h"
#include "StringUtils.h"
#include "../core/ShortcutManager.h"
#include "AdvancedTagSelector.h"
#include "TitleEditorDialog.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "../core/DatabaseManager.h"
#include "IconHelper.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QGridLayout>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QWindow>
#include <QMouseEvent>
#include <QShortcut>
#include <QKeySequence>
#include <QApplication>
#include <QScreen>
#include <QTextListFormat>
#include <QCompleter>
#include <QStringListModel>
#include <QDialog>
#include <QKeyEvent>
#include <QPointer>
#include <QThreadPool>


NoteEditWindow::NoteEditWindow(int noteId, QWidget* parent) 
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), m_noteId(noteId) 
{
    setObjectName("NoteEditWindow");
    setWindowTitle(m_noteId > 0 ? "编辑笔记" : "记录灵感");
    setAttribute(Qt::WA_TranslucentBackground); 
    setAttribute(Qt::WA_DeleteOnClose); // [FIX] 确保关闭后释放内存
    // 增加窗口物理尺寸以容纳外围阴影，防止 UpdateLayeredWindowIndirect 参数错误
    resize(980, 680); 
    initUI();

#ifdef Q_OS_WIN
    StringUtils::applyTaskbarMinimizeStyle((void*)winId());
#endif

    setupShortcuts();
    connect(&ShortcutManager::instance(), &ShortcutManager::shortcutsChanged, this, &NoteEditWindow::updateShortcuts);
    // 2026-04-xx 按照宪法初始化规范：显式同步一次快捷键提示
    updateShortcuts();
    
    if (m_noteId > 0) {
        loadNoteData(m_noteId);
    }
}

void NoteEditWindow::setDefaultCategory(int catId) {
    m_catId = catId;
}

void NoteEditWindow::setInitialData(const QString& title, const QString& content, const QStringList& tags) {
    // 2026-04-xx 按照用户要求：支持合并创建数据，初始化 UI 各组件
    if (m_titleEdit) m_titleEdit->setPlainText(title);
    if (m_contentEdit) m_contentEdit->setInitialContent(content);
    if (m_tagEdit) m_tagEdit->setText(tags.join(", "));
}

void NoteEditWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
#ifdef Q_OS_WIN
    if (m_isStayOnTop) {
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
#else
    Qt::WindowFlags f = windowFlags();
    if (m_isStayOnTop) f |= Qt::WindowStaysOnTopHint;
    else f &= ~Qt::WindowStaysOnTopHint;
    if (windowFlags() != f) {
        setWindowFlags(f);
        show();
    }
#endif
}

void NoteEditWindow::paintEvent(QPaintEvent* event) {
    // 由于使用了 mainContainer 承载背景和圆角，窗口本身只需保持透明
    Q_UNUSED(event);
}

void NoteEditWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (event->pos().y() < 40) {
            m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }
}

void NoteEditWindow::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        if (!m_dragPos.isNull()) {
            move(event->globalPosition().toPoint() - m_dragPos);
            event->accept();
        }
    }
}

void NoteEditWindow::mouseReleaseEvent(QMouseEvent* event) {
    m_dragPos = QPoint();
}

bool NoteEditWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        } else if (auto* textEdit = qobject_cast<QTextEdit*>(watched)) {
            // [NEW] 多行编辑框逻辑：首行按 ↑ 跳至开头，末行按 ↓ 跳至末尾
            if (keyEvent->key() == Qt::Key_Up) {
                QTextCursor cursor = textEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Up);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::Start);
                    textEdit->setTextCursor(cursor);
                    return true;
                }
            } else if (keyEvent->key() == Qt::Key_Down) {
                QTextCursor cursor = textEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Down);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::End);
                    textEdit->setTextCursor(cursor);
                    return true;
                }
            }
        }
    }

    if (watched->property("isCloseBtn").toBool()) {
        QPushButton* btn = qobject_cast<QPushButton*>(watched);
        if (btn) {
            if (event->type() == QEvent::Enter) {
                btn->setIcon(IconHelper::getIcon("close", "#ffffff", 20));
            } else if (event->type() == QEvent::Leave) {
                btn->setIcon(IconHelper::getIcon("close", "#aaaaaa", 20));
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void NoteEditWindow::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->pos().y() < 40) {
        toggleMaximize();
    }
}

void NoteEditWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 两段式退出逻辑：
        // 1. 如果标题框、标签框或内容框处于焦点，则仅清除焦点/返回界面。
        // 2. 如果当前无编辑组件获焦，则关闭窗口。
        QWidget* focus = focusWidget();
        if (focus && (focus == m_contentEdit || focus->parentWidget() == m_contentEdit)) {
            focus->clearFocus();
            event->accept();
            return;
        }
        if (focus && (focus == m_tagEdit || focus == m_titleEdit || focus == m_remarkEdit)) {
            focus->clearFocus();
            event->accept();
            return;
        }
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void NoteEditWindow::initUI() {
    auto* windowLayout = new QVBoxLayout(this);
    windowLayout->setObjectName("WindowLayout");
    windowLayout->setContentsMargins(15, 15, 15, 15); // 留出阴影空间
    windowLayout->setSpacing(0);

    // 主容器：承载圆角、背景和阴影
    auto* mainContainer = new QWidget();
    mainContainer->setObjectName("MainContainer");
    // 2026-04-xx 按照用户要求，修改容器为顶部直角，仅保留底部圆角
    mainContainer->setStyleSheet("QWidget#MainContainer { background-color: #1E1E1E; border-top-left-radius: 0px; border-top-right-radius: 0px; border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }");
    windowLayout->addWidget(mainContainer);

    auto* outerLayout = new QVBoxLayout(mainContainer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // 自定义标题栏
    m_titleBar = new QWidget();
    m_titleBar->setFixedHeight(32); 
    // 2026-04-xx 按照用户要求，将标题栏修改成直角
    m_titleBar->setStyleSheet("background-color: #252526; border-radius: 0px; border-bottom: 1px solid #333;");
    auto* tbLayout = new QHBoxLayout(m_titleBar);
    // 2026-04-xx 按照用户要求，将左右两侧的按钮/图标与边缘保持 5 像素间距
    tbLayout->setContentsMargins(5, 0, 5, 0); 
    tbLayout->setSpacing(5); // 修复：按钮间距同步设为 5 像素

    QLabel* titleIcon = new QLabel();
    titleIcon->setPixmap(IconHelper::getIcon("edit", "#4FACFE", 18).pixmap(18, 18));
    tbLayout->addWidget(titleIcon);

    m_winTitleLabel = new QLabel(m_noteId > 0 ? "编辑笔记" : "记录灵感");
    m_winTitleLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 13px; margin-left: 5px;");
    tbLayout->addWidget(m_winTitleLabel);
    tbLayout->addStretch();

    // 2026-03-xx 按照用户要求：统一控制按钮样式为 24x24px，圆角 4px，图标 18px
    QString ctrlBtnStyle = "QPushButton { background: transparent; border: none; border-radius: 4px; padding: 0px; } "
                           "QPushButton:hover { background-color: #3e3e42; }";
    
    QPushButton* btnMin = new QPushButton();
    btnMin->setIcon(IconHelper::getIcon("minimize", "#aaaaaa", 18));
    btnMin->setIconSize(QSize(18, 18));
    btnMin->setFixedSize(24, 24);
    btnMin->setStyleSheet(ctrlBtnStyle);
    connect(btnMin, &QPushButton::clicked, this, &QWidget::showMinimized);
    
    m_maxBtn = new QPushButton();
    m_maxBtn->setIcon(IconHelper::getIcon("maximize", "#aaaaaa", 18));
    m_maxBtn->setIconSize(QSize(18, 18));
    m_maxBtn->setFixedSize(24, 24);
    m_maxBtn->setStyleSheet(ctrlBtnStyle);
    connect(m_maxBtn, &QPushButton::clicked, this, &NoteEditWindow::toggleMaximize);

    m_btnStayOnTop = new QPushButton();
    m_btnStayOnTop->setIcon(IconHelper::getIcon("pin_tilted", "#aaaaaa", 18));
    m_btnStayOnTop->setIconSize(QSize(18, 18));
    m_btnStayOnTop->setFixedSize(24, 24);
    m_btnStayOnTop->setCheckable(true);
    // 2026-03-xx 按照用户要求，修改置顶按钮样式：置顶后背景为浅灰色。
    m_btnStayOnTop->setStyleSheet(ctrlBtnStyle + " QPushButton:checked { background-color: rgba(255, 255, 255, 0.1); }");

    // 加载记忆状态
    QSettings settings("RapidNotes", "WindowStates");
    m_isStayOnTop = settings.value("NoteEditWindow/StayOnTop", false).toBool();
    if (m_isStayOnTop) {
        m_btnStayOnTop->setChecked(true);
        m_btnStayOnTop->setIcon(IconHelper::getIcon("pin_vertical", "#FF551C", 18));
    }

    connect(m_btnStayOnTop, &QPushButton::toggled, this, &NoteEditWindow::toggleStayOnTop);
    
    QPushButton* btnClose = new QPushButton();
    // 2026-04-xx 按照用户要求：关闭按钮常驻红底白字
    btnClose->setIcon(IconHelper::getIcon("close", "#FFFFFF", 18));
    btnClose->setIconSize(QSize(18, 18));
    btnClose->setFixedSize(24, 24);
    // 2026-04-xx 按照用户要求：关闭按钮悬停锁定红色系
    btnClose->setStyleSheet("QPushButton { background: #E81123; border: none; border-radius: 4px; padding: 0px; } QPushButton:hover { background-color: #D71520; }");
    connect(btnClose, &QPushButton::clicked, this, &QWidget::close);

    tbLayout->addWidget(m_btnStayOnTop);
    tbLayout->addWidget(btnMin);
    tbLayout->addWidget(m_maxBtn);
    tbLayout->addWidget(btnClose);
    outerLayout->addWidget(m_titleBar);

    // 主内容区使用 Splitter
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setStyleSheet("QSplitter::handle { background-color: #252526; width: 2px; } QSplitter::handle:hover { background-color: #4FACFE; }");

    // 左侧面板
    QWidget* leftContainer = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(15, 15, 15, 15);
    setupLeftPanel(leftLayout);

    // 右侧面板
    QWidget* rightContainer = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->setContentsMargins(10, 15, 15, 15);
    setupRightPanel(rightLayout);

    m_splitter->addWidget(leftContainer);
    m_splitter->addWidget(rightContainer);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({300, 650});

    outerLayout->addWidget(m_splitter);

    // 阴影应用在内部容器上，确保不超出窗口边界
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(15);
    shadow->setColor(QColor(0, 0, 0, 180));
    shadow->setOffset(0, 2);
    mainContainer->setGraphicsEffect(shadow);
}

void NoteEditWindow::setupLeftPanel(QVBoxLayout* layout) {
    QString labelStyle = "color: #888; font-size: 12px; font-weight: bold; margin-bottom: 4px;";
    QString inputStyle = "QLineEdit, QComboBox { background: #252526; border: 1px solid #333; border-radius: 4px; padding: 8px; color: #eee; font-size: 13px; } QLineEdit:focus, QComboBox:focus { border: 1px solid #4FACFE; }";

    QLabel* lblTitle = new QLabel("标题");
    lblTitle->setStyleSheet(labelStyle);
    m_titleEdit = new QTextEdit();
    m_titleEdit->setPlaceholderText("请输入灵感标题...");
    m_titleEdit->setStyleSheet(
        "QTextEdit {"
        "  background: #252526;"
        "  border: 1px solid #333;"
        "  border-radius: 4px;"
        "  padding: 6px 8px;"
        "  color: #eee;"
        "  font-size: 13px;"
        "}"
        "QTextEdit:focus { border: 1px solid #4FACFE; }"
        "QScrollBar:vertical { width: 5px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );
    m_titleEdit->setFixedHeight(100);
    m_titleEdit->installEventFilter(this);
    layout->addWidget(lblTitle);
    layout->addWidget(m_titleEdit);

    // [NEW] 备注输入框（标题下方）
    QLabel* lblRemark = new QLabel("备注");
    lblRemark->setStyleSheet(labelStyle);
    m_remarkEdit = new QTextEdit();
    m_remarkEdit->setPlaceholderText("添加备注说明...");
    m_remarkEdit->setFixedHeight(100);
    m_remarkEdit->installEventFilter(this);
    m_remarkEdit->setStyleSheet(
        "QTextEdit {"
        "  background: #252526;"
        "  border: 1px solid #333;"
        "  border-radius: 4px;"
        "  padding: 6px 8px;"
        "  color: #eee;"
        "  font-size: 12px;"
        "}"
        "QTextEdit:focus { border: 1px solid #4FACFE; }"
        "QScrollBar:vertical { width: 5px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 2px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
    );
    layout->addWidget(lblRemark);
    layout->addWidget(m_remarkEdit);

    layout->addSpacing(12); // 标签区上方留出间距，视觉上向下偏移
    QLabel* lblTags = new QLabel("标签");
    lblTags->setStyleSheet(labelStyle);
    m_tagEdit = new ClickableLineEdit();
    m_tagEdit->setPlaceholderText("使用逗号分隔，如: 工作, 待办 (双击显示历史)");
    m_tagEdit->setStyleSheet(inputStyle);
    connect(m_tagEdit, &ClickableLineEdit::doubleClicked, this, &NoteEditWindow::openTagSelector);
    
    // 智能补全标签
    QStringList allTags = DatabaseManager::instance().getAllTags();
    QCompleter* completer = new QCompleter(allTags, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_tagEdit->setCompleter(completer);

    layout->addWidget(lblTags);
    layout->addWidget(m_tagEdit);
    m_tagEdit->installEventFilter(this);

    QLabel* lblColor = new QLabel("标记颜色");
    lblColor->setStyleSheet(labelStyle);
    layout->addWidget(lblColor);

    QWidget* colorGrid = new QWidget();
    QGridLayout* grid = new QGridLayout(colorGrid);
    grid->setContentsMargins(0, 10, 0, 10);
    
    m_colorGroup = new QButtonGroup(this);
    QStringList colors = {"#FF9800", "#444444", "#2196F3", "#4CAF50", "#F44336", "#9C27B0"};
    for(int i=0; i<colors.size(); ++i) {
        QPushButton* btn = createColorBtn(colors[i], i);
        grid->addWidget(btn, i/3, i%3);
        m_colorGroup->addButton(btn, i);
    }
    if(m_colorGroup->button(0)) m_colorGroup->button(0)->setChecked(true);
    
    layout->addWidget(colorGrid);
    
    m_defaultColorCheck = new QCheckBox("设为默认颜色");
    m_defaultColorCheck->setStyleSheet("QCheckBox { color: #858585; font-size: 12px; margin-top: 5px; }");
    layout->addWidget(m_defaultColorCheck);

    layout->addStretch(); 

    QPushButton* saveBtn = new QPushButton();
    saveBtn->setIcon(IconHelper::getIcon("save", "#ffffff"));
    saveBtn->setText("  保存 (Ctrl+S)");
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setFixedHeight(50);
    // 2026-03-xx 按照用户要求，统一保存按钮圆角为 4px
    saveBtn->setStyleSheet("QPushButton { background-color: #4FACFE; color: white; border: none; border-radius: 4px; font-weight: bold; font-size: 13px; } QPushButton:hover { background-color: #357abd; }");
    connect(saveBtn, &QPushButton::clicked, this, &NoteEditWindow::saveNote);
    layout->addWidget(saveBtn);
}

QPushButton* NoteEditWindow::createColorBtn(const QString& color, int id) {
    QPushButton* btn = new QPushButton();
    btn->setCheckable(true);
    btn->setFixedSize(30, 30);
    btn->setProperty("color", color);
    btn->setStyleSheet(QString(
        "QPushButton { background-color: %1; border-radius: 15px; border: 2px solid transparent; }"
        "QPushButton:checked { border: 2px solid white; }"
    ).arg(color));
    return btn;
}

void NoteEditWindow::setupRightPanel(QVBoxLayout* layout) {
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(1);

    QLabel* titleLabel = new QLabel("详细内容");
    titleLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();

    QHBoxLayout* toolBar = new QHBoxLayout();
    toolBar->setContentsMargins(0, 0, 0, 0);
    toolBar->setSpacing(0); // 彻底消除按钮间距，实现紧凑布局

    // 2026-03-xx 按照用户要求：标准化工具栏样式，对齐 QuickWindow 规格 (24x24, 4px)
    QString btnStyle = "QPushButton { background: transparent; border: none; border-radius: 4px; padding: 0px; } "
                       "QPushButton:hover { background-color: #3e3e42; } "
                       "QPushButton:checked { background-color: rgba(255, 255, 255, 0.2); }";
    
    auto addTool = [&](const QString& iconName, const QString& tip, std::function<void()> callback) {
        QPushButton* btn = new QPushButton();
        btn->setIcon(IconHelper::getIcon(iconName, "#aaaaaa", 18)); // 统一 18px 图标
        btn->setIconSize(QSize(18, 18));
        btn->setProperty("tooltipText", tip); btn->installEventFilter(this);
        btn->setFixedSize(24, 24); // 统一 24x24 尺寸
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(btnStyle);
        connect(btn, &QPushButton::clicked, callback);
        toolBar->addWidget(btn);
        return btn;
    };

    // 2026-04-xx 按照宪法规范：修正 ToolTip 为“半角空格引导+全角括号”
    addTool("undo", "撤销 （Ctrl + Z）", [this](){ m_contentEdit->undo(); });
    addTool("redo", "重做 （Ctrl + Y）", [this](){ m_contentEdit->redo(); });
    
    QFrame* sep1 = new QFrame();
    sep1->setFixedWidth(1);
    sep1->setFixedHeight(16);
    sep1->setStyleSheet("background-color: #333; margin-left: 2px; margin-right: 2px;");
    toolBar->addWidget(sep1);

    addTool("list_ul", "无序列表", [this](){ m_contentEdit->toggleList(false); });
    addTool("list_ol", "有序列表", [this](){ m_contentEdit->toggleList(true); });
    

    addTool("edit_clear", "清除格式", [this](){ m_contentEdit->clearFormatting(); });
    
    QFrame* sep2 = new QFrame();
    sep2->setFixedWidth(1);
    sep2->setFixedHeight(16);
    sep2->setStyleSheet("background-color: #333; margin-left: 2px; margin-right: 2px;");
    toolBar->addWidget(sep2);

    // 高亮颜色
    QStringList hColors = {"#c0392b", "#f1c40f", "#27ae60", "#2980b9"};
    for (const auto& color : hColors) {
        QPushButton* hBtn = new QPushButton();
        hBtn->setFixedSize(20, 20);
        hBtn->setStyleSheet(QString("QPushButton { background-color: %1; border: 1px solid rgba(0,0,0,0.2); border-radius: 4px; } "
                                    "QPushButton:hover { border-color: white; }").arg(color));
        hBtn->setCursor(Qt::PointingHandCursor);
        connect(hBtn, &QPushButton::clicked, [this, color](){ m_contentEdit->highlightSelection(QColor(color)); });
        toolBar->addWidget(hBtn);
    }

    // 清除高亮按钮
    QPushButton* btnNoColor = new QPushButton();
    btnNoColor->setIcon(IconHelper::getIcon("no_color", "#aaaaaa", 14));
    btnNoColor->setFixedSize(24, 24);
    btnNoColor->setProperty("tooltipText", "清除高亮"); btnNoColor->installEventFilter(this);
    btnNoColor->setStyleSheet("QPushButton { background: transparent; border: 1px solid #444; border-radius: 4px; margin-left: 4px; } "
                              "QPushButton:hover { background-color: rgba(255, 255, 255, 0.1); border-color: #888; }");
    btnNoColor->setCursor(Qt::PointingHandCursor);
    connect(btnNoColor, &QPushButton::clicked, [this](){ m_contentEdit->highlightSelection(Qt::transparent); });
    toolBar->addWidget(btnNoColor);
    
    headerLayout->addLayout(toolBar);
    layout->addLayout(headerLayout);

    // 搜索栏 (默认隐藏)
    m_searchBar = new QWidget();
    m_searchBar->setVisible(false);
    m_searchBar->setStyleSheet("background-color: #252526; border-radius: 6px; padding: 2px;");
    auto* sbLayout = new QHBoxLayout(m_searchBar);
    sbLayout->setContentsMargins(5, 2, 5, 2);
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("查找内容...");
    m_searchEdit->setStyleSheet("border: none; background: transparent; color: #fff;");
    connect(m_searchEdit, &QLineEdit::returnPressed, [this](){ m_contentEdit->findText(m_searchEdit->text()); });
    
    QPushButton* btnPrev = new QPushButton();
    btnPrev->setIcon(IconHelper::getIcon("nav_prev", "#ccc"));
    btnPrev->setFixedSize(24, 24);
    btnPrev->setStyleSheet("background: transparent; border: none;");
    connect(btnPrev, &QPushButton::clicked, [this](){ m_contentEdit->findText(m_searchEdit->text(), true); });
    
    QPushButton* btnNext = new QPushButton();
    btnNext->setIcon(IconHelper::getIcon("nav_next", "#ccc"));
    btnNext->setFixedSize(24, 24);
    btnNext->setStyleSheet("background: transparent; border: none;");
    connect(btnNext, &QPushButton::clicked, [this](){ m_contentEdit->findText(m_searchEdit->text(), false); });
    
    QPushButton* btnCls = new QPushButton();
    btnCls->setIcon(IconHelper::getIcon("close", "#ccc"));
    btnCls->setFixedSize(24, 24);
    btnCls->setStyleSheet("background: transparent; border: none;");
    connect(btnCls, &QPushButton::clicked, [this](){ m_searchBar->hide(); });

    sbLayout->addWidget(m_searchEdit);
    m_searchEdit->installEventFilter(this);
    sbLayout->addWidget(btnPrev);
    sbLayout->addWidget(btnNext);
    sbLayout->addWidget(btnCls);
    layout->addWidget(m_searchBar);

    layout->addSpacing(5);
    m_contentEdit = new Editor(); 
    m_contentEdit->setPlaceholderText("在这里记录详细内容（支持 Markdown 和粘贴图片）...");
    m_contentEdit->togglePreview(false); // 强制进入编辑模式
    layout->addWidget(m_contentEdit);
}

void NoteEditWindow::setupShortcuts() {
    auto add = [&](const QString& id, std::function<void()> func) {
        auto* sc = new QShortcut(ShortcutManager::instance().getShortcut(id), this, func);
        sc->setProperty("id", id);
        m_shortcutObjs.append(sc);
    };

    add("ed_save", [this](){ saveNote(); });
    add("ed_close", [this](){ close(); });
    add("ed_search", [this](){ toggleSearchBar(); });
}

void NoteEditWindow::updateShortcuts() {
    for (auto* sc : m_shortcutObjs) {
        QString id = sc->property("id").toString();
        sc->setKey(ShortcutManager::instance().getShortcut(id));
    }
}

void NoteEditWindow::toggleStayOnTop() {
    m_isStayOnTop = m_btnStayOnTop->isChecked();
    // 2026-03-xx 按照用户要求，修改置顶按钮样式：置顶后图标变为橙色。
    m_btnStayOnTop->setIcon(IconHelper::getIcon(m_isStayOnTop ? "pin_vertical" : "pin_tilted", m_isStayOnTop ? "#FF551C" : "#aaaaaa", 20));

    QSettings settings("RapidNotes", "WindowStates");
    settings.setValue("NoteEditWindow/StayOnTop", m_isStayOnTop);

    if (isVisible()) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, m_isStayOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
        Qt::WindowFlags f = windowFlags();
        if (m_isStayOnTop) f |= Qt::WindowStaysOnTopHint;
        else f &= ~Qt::WindowStaysOnTopHint;
        setWindowFlags(f);
        show();
#endif
    }
}

void NoteEditWindow::toggleMaximize() {
    auto* windowLayout = findChild<QVBoxLayout*>("WindowLayout");
    auto* mainContainer = findChild<QWidget*>("MainContainer");

    if (m_isMaximized) {
        showNormal();
        if (windowLayout) windowLayout->setContentsMargins(15, 15, 15, 15);
        // 2026-04-xx 按照用户要求，恢复后容器顶部仍保持直角，仅底部圆角
        if (mainContainer) mainContainer->setStyleSheet("QWidget#MainContainer { background-color: #1E1E1E; border-top-left-radius: 0px; border-top-right-radius: 0px; border-bottom-left-radius: 12px; border-bottom-right-radius: 12px; }");
        m_maxBtn->setIcon(IconHelper::getIcon("maximize", "#aaaaaa", 18));
        // 2026-04-xx 按照用户要求，恢复后标题栏仍保持直角
        m_titleBar->setStyleSheet("background-color: #252526; border-radius: 0px; border-bottom: 1px solid #333;");
    } else {
        m_normalGeometry = geometry();
        showMaximized();
        if (windowLayout) windowLayout->setContentsMargins(0, 0, 0, 0);
        if (mainContainer) mainContainer->setStyleSheet("QWidget#MainContainer { background-color: #1E1E1E; border-radius: 0px; }");
        // 2026-04-xx 按照用户要求，将 svg 图标 “restore” 替换成 “restore_window”
        m_maxBtn->setIcon(IconHelper::getIcon("restore_window", "#aaaaaa", 18));
        m_titleBar->setStyleSheet("background-color: #252526; border-radius: 0px; border-bottom: 1px solid #333;");
    }
    m_isMaximized = !m_isMaximized;
    update();
}

void NoteEditWindow::saveNote() {
    QString title = m_titleEdit->toPlainText().replace('\n', ' ').trimmed();
    if(title.isEmpty()) title = "未命名灵感";

    // 2026-03-xx 按照用户最高要求：优化保存策略，防止属性被破坏。
    // 优先采用优化后的内容获取方式（根据是否含富文本决定 HTML 或 纯文本）
    QString content = m_contentEdit->getOptimizedContent();

    QString tagsStr = m_tagEdit->text();
    QStringList tagsList = tagsStr.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for (QString& t : tagsList) t = t.trimmed();

    int catId = m_catId;
    QString color = m_colorGroup->checkedButton() ? m_colorGroup->checkedButton()->property("color").toString() : "";
    QString remark = m_remarkEdit ? m_remarkEdit->toPlainText().trimmed() : "";

    // [OPTIMIZATION] 将核心写入逻辑放入后台线程，防止超大 HTML/Base64 图片导致 UI 线程阻塞
    int noteId = m_noteId;
    
    // [CRITICAL] 2026-03-xx 属性保护逻辑：禁止因编辑标题或简单修改而发生属性退化。
    // 1. 如果原始属性不是 text，则保持原有属性（如 link, color, code, file），除非内容确实发生了根本性变化。
    // 2. 如果原始属性是 text，则根据新内容重新智能识别。
    QString finalType = m_origItemType;
    
    bool contentHasSubstantialChange = false;
    QString newPlain = StringUtils::htmlToPlainText(content).trimmed();
    QString oldPlain = StringUtils::htmlToPlainText(m_contentEdit->property("originalContent").toString()).trimmed();
    
    // 判定内容是否真的被重写了（排除仅修改标题的情况）
    if (!oldPlain.isEmpty() && newPlain != oldPlain) {
        contentHasSubstantialChange = true;
    }

    if (finalType == "image" || finalType == "file") {
        if (contentHasSubstantialChange && !m_contentEdit->isRich()) {
            finalType = StringUtils::detectItemType(content);
        }
    } else if (finalType == "link" || finalType == "color" || finalType == "code") {
        // 按照用户要求：既然创建时是网页链接，无论怎么修改内容（如修改标题），它仍然应该是网页链接。
        // 除非用户删掉了链接换成了纯文本，否则不应降级。
        if (contentHasSubstantialChange) {
            finalType = StringUtils::detectItemType(content);
            // 补偿逻辑：如果新检测出的是普通文本，但旧的是链接，说明用户可能只是在微调链接文本，应予以保留属性。
            if (finalType == "text" && m_origItemType == "link" && content.contains(".")) {
                 finalType = "link";
            }
        }
    } else {
        // 原本就是 text 类型，执行全量检测
        finalType = StringUtils::detectItemType(content);
    }

    if (finalType.isEmpty()) finalType = "text";

    // [FIX] 使用 QPointer 追踪窗口状态，彻底解决后台任务回调时的野指针崩溃风险 (Qt::WA_DeleteOnClose 冲突)
    QPointer<NoteEditWindow> safeThis(this);

    // 暂存原始属性，防止 Lambda 捕获失效
    QString origApp = m_sourceApp;
    QString origTitle = m_sourceTitle;
    QByteArray origBlob = m_origBlob;

    (void)QThreadPool::globalInstance()->start([=]() {
        if (noteId == 0) {
            DatabaseManager::instance().addNote(title, content, tagsList, color, catId, finalType, QByteArray(), "", "", remark);
        } else {
            // [CRITICAL] 锁定：调用重构后的 updateNote 接口，全量同步所有属性，彻底解决属性破坏问题。
            DatabaseManager::instance().updateNote(noteId, title, content, tagsList, color, catId, 
                                                finalType, origBlob, origApp, origTitle, remark);
            DatabaseManager::instance().recordAccess(noteId);
        }
        
        // 只有当窗口依然存在时，才切回主线程通知刷新并关闭
        if (safeThis) {
            QMetaObject::invokeMethod(safeThis, "onSaveFinished", Qt::QueuedConnection);
        }
    });

    // 立即反馈 UI 已开始处理 (可选：可在此处禁用保存按钮防止重复点击)
}

void NoteEditWindow::onSaveFinished() {
    emit noteSaved();
    close();
}

void NoteEditWindow::toggleSearchBar() {
    m_searchBar->setVisible(!m_searchBar->isVisible());
    if (m_searchBar->isVisible()) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void NoteEditWindow::openTagSelector() {
    QStringList currentTags = m_tagEdit->text().split(",", Qt::SkipEmptyParts);
    for (QString& t : currentTags) t = t.trimmed();

    auto* selector = new AdvancedTagSelector(this);
    auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
    auto allTags = DatabaseManager::instance().getAllTags();
    selector->setup(recentTags, allTags, currentTags);

    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this](const QStringList& tags){
        m_tagEdit->setText(tags.join(", "));
    });

    selector->showAtCursor();
}

void NoteEditWindow::openExpandedTitleEditor() {
    TitleEditorDialog dialog(m_titleEdit->toPlainText(), this);
    // 设置初始位置在鼠标附近
    QPoint pos = QCursor::pos();
    dialog.move(pos.x() - 160, pos.y() - 40);
    
    if (dialog.exec() == QDialog::Accepted) {
        QString newTitle = dialog.getText();
        if (!newTitle.isEmpty() && newTitle != m_titleEdit->toPlainText()) {
            m_titleEdit->setPlainText(newTitle);
        }
    }
}

void NoteEditWindow::loadNoteData(int id) {
    QVariantMap note = DatabaseManager::instance().getNoteById(id);
    if (!note.isEmpty()) {
        // [MODIFIED] 2026-03-xx 按照用户要求：加载时备份原始元数据，确保编辑保存时不破坏数据的“身世”。
        m_origItemType = note.value("item_type").toString();
        m_origBlob = note.value("data_blob").toByteArray();
        m_sourceApp = note.value("source_app").toString();
        m_sourceTitle = note.value("source_title").toString();

        m_titleEdit->setPlainText(note.value("title").toString());
        m_contentEdit->setNote(note, false);
        m_contentEdit->setProperty("originalContent", note.value("content")); // 备份原始内容以供比较
        m_contentEdit->togglePreview(false); // 确保在加载数据后也处于编辑模式
        m_tagEdit->setText(note.value("tags").toString());
        
        // [NEW] 加载备注
        if (m_remarkEdit) {
            m_remarkEdit->setPlainText(note.value("remark").toString());
        }
        
        m_catId = note["category_id"].isNull() ? -1 : note["category_id"].toInt();
        
        QString color = note["color"].toString();
        for (int i = 0; i < m_colorGroup->buttons().size(); ++i) {
            if (m_colorGroup->button(i)->property("color").toString() == color) {
                m_colorGroup->button(i)->setChecked(true);
                break;
            }
        }
    }
    m_contentEdit->setFocus();
}