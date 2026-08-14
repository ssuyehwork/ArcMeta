#include "RecentNotesWindow.h"
#include "RecentNotesDelegate.h"
#include "../models/NoteModel.h"
#include "../core/DatabaseManager.h"
#include "../ui/StringUtils.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include "../core/ClipboardMonitor.h"
#include "../core/ShortcutManager.h"
#include <QSqlQuery>
#include <QSqlRecord>
#include <QMutexLocker>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTimer>
#include <QShortcut>
#include <QElapsedTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QKeyEvent>
#include <QGraphicsDropShadowEffect>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QClipboard>
#include <QTimer>
#include <QDebug>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QListView>
#include <QPushButton>
#include <QHBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

// ──────────────────────────────────────────────
//  常量
// ──────────────────────────────────────────────
static constexpr int kPanelWidth      = 350;   // 浮窗固定宽度（px）
static constexpr int kMaxVisibleItems = 8;     // 不出滚动条时最多显示几行
static constexpr int kItemHeight      = 45;    // 每行高度（px，与 QuickNoteDelegate 一致）
// 移除 kPageSize 限制，支持加载所有数据

// ──────────────────────────────────────────────
//  单例
// ──────────────────────────────────────────────
RecentNotesWindow* RecentNotesWindow::s_instance = nullptr;

RecentNotesWindow* RecentNotesWindow::instance() {
    if (!s_instance) {
        s_instance = new RecentNotesWindow();
    }
    return s_instance;
}

// ──────────────────────────────────────────────
//  构造
// ──────────────────────────────────────────────
RecentNotesWindow::RecentNotesWindow(QWidget* parent)
    : QWidget(parent,
              Qt::Tool                  // Tool 窗口：不抢主窗口激活状态
              | Qt::FramelessWindowHint
              | Qt::NoDropShadowWindowHint
              | Qt::WindowStaysOnTopHint) // 2026-05-04 按照用户要求：强制置顶，防止位于其他置顶窗口下方
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFixedWidth(kPanelWidth);
    
    // 安装事件过滤器，用于处理失去焦点自动关闭
    installEventFilter(this);

    initUI();
    loadRecentNotes();
}

RecentNotesWindow::~RecentNotesWindow() {
    s_instance = nullptr;
}

// ──────────────────────────────────────────────
//  initUI — 初始化界面
// ──────────────────────────────────────────────
void RecentNotesWindow::initUI() {
    // ── 外层容器（用于 shadow 留边）──────────────────
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(12, 12, 12, 12);   // shadow 留白
    m_mainLayout->setSpacing(0);

    m_cardWidget = new QWidget();
    m_cardWidget->setObjectName("rnwCard");
    m_mainLayout->addWidget(m_cardWidget);

    // ── 阴影 ──────────────────────────────────────
    m_shadowEffect = new QGraphicsDropShadowEffect(m_cardWidget);
    m_shadowEffect->setBlurRadius(24);
    m_shadowEffect->setColor(QColor(0, 0, 0, 140));
    m_shadowEffect->setOffset(0, 6);
    m_cardWidget->setGraphicsEffect(m_shadowEffect);

    // ── 卡片内布局 ─────────────────────────────────
    auto* cardLayout = new QVBoxLayout(m_cardWidget);
    cardLayout->setContentsMargins(10, 10, 10, 10);
    cardLayout->setSpacing(5); // 增加间距以容纳标题栏

    // ── [NEW] 顶部标题栏 (最近访问 / 收藏) ─────────────────
    m_tabContainer = new QWidget();
    auto* tabLayout = new QHBoxLayout(m_tabContainer);
    tabLayout->setContentsMargins(5, 0, 5, 5); // 顶部 5->0, 底部 10->5
    tabLayout->setSpacing(20);

    QString tabStyle = R"(
        QPushButton {
            background: transparent;
            border: none;
            color: #888;
            font-size: 13px;
            font-weight: bold;
            padding: 2px 2px;
        }
        QPushButton:hover {
            color: #bbb;
        }
        QPushButton[active="true"] {
            color: #3498db;
            border-bottom: 2px solid #3498db;
        }
    )";

    m_tabRecent = new QPushButton("最近访问");
    m_tabFavorite = new QPushButton("收藏");
    
    m_tabRecent->setStyleSheet(tabStyle);
    m_tabFavorite->setStyleSheet(tabStyle);
    m_tabRecent->setCursor(Qt::PointingHandCursor);
    m_tabFavorite->setCursor(Qt::PointingHandCursor);

    connect(m_tabRecent, &QPushButton::clicked, this, [this](){ switchView(Recent); });
    connect(m_tabFavorite, &QPushButton::clicked, this, [this](){ switchView(Favorite); });

    tabLayout->addWidget(m_tabRecent);
    tabLayout->addWidget(m_tabFavorite);
    tabLayout->addStretch();

    cardLayout->addWidget(m_tabContainer);

    // ── 全局样式 ───────────────────────────────────
    m_cardWidget->setStyleSheet(R"(
        QWidget#rnwCard {
            background-color: #1E1E1E;
            border-radius: 10px;
            border: 1px solid #383838;
        }
    )");

    // ── 列表视图 ───────────────────────────────────
    m_listView = new QListView();
    m_listView->setObjectName("recentNotesList");
    m_listView->setAlternatingRowColors(true);
    m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setMouseTracking(true);
    m_listView->setItemDelegate(new RecentNotesDelegate(this));
    
    // 创建数据模型
    m_model = new NoteModel(this);
    m_listView->setModel(m_model);
    
    // 设置样式
    QString listStyle = "QListView { "
                       "  border: none; "
                       "  background-color: #1e1e1e; "
                       "  alternate-background-color: #252526; "
                       "  selection-background-color: transparent; "
                       "  color: #eee; "
                       "  outline: none; "
                       "}";
    m_listView->setStyleSheet(listStyle);
    
    // 连接信号
    connect(m_listView, &QListView::doubleClicked, this, &RecentNotesWindow::onItemDoubleClicked);
    connect(m_listView, &QListView::clicked, this, &RecentNotesWindow::onItemClicked);
    
    // 2026-05-04 按照用户要求：移除RecentNotesWindow的空格键预览功能
    
    cardLayout->addWidget(m_listView);
}

// ──────────────────────────────────────────────
//  loadRecentNotes — 加载最近访问的笔记
// ──────────────────────────────────────────────
void RecentNotesWindow::loadRecentNotes() {
    // 每次都重新从数据库实时读取数据，确保数据是最新的
    qDebug() << "[RecentNotesWindow] 开始重新加载数据...";
    
    // 清空当前数据，强制刷新
    m_recentNotes.clear();
    if (m_model) {
        m_model->setNotes(m_recentNotes);
    }
    
    // 2026-05-04 按照用户要求：RecentNotesWindow 无视置顶逻辑，纯按访问时间排序
    QVariantMap criteria;
    criteria["sort_by"] = "last_accessed_at";
    criteria["sort_order"] = "DESC";
    criteria["recent_notes_window"] = true; // 特殊标记，确保完全无视置顶逻辑
    
    // 2026-05-09 按照用户要求：支持标签页切换展示
    QString filterType = "all";
    if (m_viewType == Favorite) {
        filterType = "bookmark"; // DatabaseManager 中 bookmark 对应 is_favorite = 1
    }

    auto notes = DatabaseManager::instance().searchNotes(
        "",                    // 关键词：空
        filterType,            // 过滤类型
        QVariant(-1),          // 过滤值：无
        1,                     // 页码：第1页
        -1,                    // 每页大小：-1 表示加载所有数据
        criteria               // 自定义排序：按访问时间 DESC，无视置顶
    );
    
    qDebug() << "[RecentNotesWindow] 使用自定义排序：按 last_accessed_at DESC 排序（无视置顶）";

    m_recentNotes = notes;
    m_model->setNotes(m_recentNotes);
    
    // 固定窗口高度为675像素
    setFixedHeight(675);
    
    qDebug() << "[RecentNotesWindow] 实时加载完成，共" << m_recentNotes.size() << "条笔记（按访问时间排序）";
    
    // 显示前几条笔记的标题用于调试
    if (!m_recentNotes.isEmpty()) {
        QStringList debugTitles;
        for (int i = 0; i < qMin(3, m_recentNotes.size()); ++i) {
            QString title = m_recentNotes[i].value("title").toString();
            QString updatedAt = m_recentNotes[i].value("updated_at").toString();
            debugTitles << QString("%1 (更新: %2)").arg(title.left(20), updatedAt.left(19));
        }
        qDebug() << "[RecentNotesWindow] 前3条:" << debugTitles.join(" | ");
    }
}

// ──────────────────────────────────────────────
//  popup — 对外入口
// ──────────────────────────────────────────────
void RecentNotesWindow::popup(const QPoint& globalPos) {
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

    // 3. 重置并重新加载数据（Alt+A 默认进入最近访问）
    m_viewType = Recent;
    updateTabStyles();
    loadRecentNotes();

    // 4. 定位并显示
    adjustPosition(globalPos);
    show();
    raise();
    activateWindow();
    m_listView->setFocus();

    // 5. 默认选中第一行
    if (m_model->rowCount() > 0) {
        m_listView->setCurrentIndex(m_model->index(0, 0));
    }
}

// ──────────────────────────────────────────────
//  adjustPosition — 调整窗口位置
// ──────────────────────────────────────────────
void RecentNotesWindow::adjustPosition(const QPoint& preferredPos) {
    QScreen* screen = QGuiApplication::screenAt(preferredPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    QRect screenGeometry = screen->availableGeometry();
    QSize windowSize = size();
    
    QPoint pos = preferredPos;
    
    // 防止窗口超出屏幕边界
    if (pos.x() + windowSize.width() > screenGeometry.right()) {
        pos.setX(screenGeometry.right() - windowSize.width());
    }
    if (pos.x() < screenGeometry.left()) {
        pos.setX(screenGeometry.left());
    }
    if (pos.y() + windowSize.height() > screenGeometry.bottom()) {
        pos.setY(screenGeometry.bottom() - windowSize.height());
    }
    if (pos.y() < screenGeometry.top()) {
        pos.setY(screenGeometry.top());
    }
    
    move(pos);
}

// ──────────────────────────────────────────────
//  事件处理
// ──────────────────────────────────────────────
bool RecentNotesWindow::event(QEvent* e) {
    if (e->type() == QEvent::WindowDeactivate) {
        // 失去焦点时自动关闭
        hide();
        return true;
    }
    return QWidget::event(e);
}

void RecentNotesWindow::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Escape:
            hide();
            break;
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
            // 让列表视图处理导航键
            QApplication::sendEvent(m_listView, e);
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateCurrentItem();
            break;
        default:
            QWidget::keyPressEvent(e);
            break;
    }
}

bool RecentNotesWindow::eventFilter(QObject* obj, QEvent* event) {
    if (obj == this && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ──────────────────────────────────────────────
//  列表项处理
// ──────────────────────────────────────────────
void RecentNotesWindow::onItemDoubleClicked(const QModelIndex& index) {
    if (index.isValid()) {
        int row = index.row();
        if (row >= 0 && row < m_recentNotes.size()) {
            // 记录访问
            int noteId = m_recentNotes[row].value("id").toInt();
            if (noteId > 0) {
                DatabaseManager::instance().recordAccess(noteId);
            }
            
            sendNote(m_recentNotes[row]);
            hide();
        }
    }
}

void RecentNotesWindow::onItemClicked(const QModelIndex& index) {
    // 单击时仅选中，不执行发送操作
    m_listView->setCurrentIndex(index);
}

void RecentNotesWindow::activateCurrentItem() {
    QModelIndex current = m_listView->currentIndex();
    if (current.isValid()) {
        int row = current.row();
        if (row >= 0 && row < m_recentNotes.size()) {
            // 记录访问
            int noteId = m_recentNotes[row].value("id").toInt();
            if (noteId > 0) {
                DatabaseManager::instance().recordAccess(noteId);
            }
            
            sendNote(m_recentNotes[row]);
            hide();
        }
    }
}

// ──────────────────────────────────────────────
//  sendNote — 发送笔记
// ──────────────────────────────────────────────
// 2026-05-04 按照用户要求：移除RecentNotesWindow的预览功能
// 原来的 doPreview() 和 updatePreviewContent() 函数已移除

void RecentNotesWindow::sendNote(const QVariantMap& note) {
    QString title = note.value("title").toString();
    QString itemType = note.value("item_type").toString();
    QString content = note.value("content").toString();
    QByteArray blob = note.value("data_blob").toByteArray();

    // 复制逻辑 - 与 QuickWindow::activateNote 保持一致
    if (itemType == "image") {
        QImage img;
        img.loadFromData(blob);
        ClipboardMonitor::instance().skipNext();
        QApplication::clipboard()->setImage(img);
    } else if (itemType == "local_file" || itemType == "local_folder" || itemType == "local_batch" ||
               (QFileInfo(StringUtils::htmlToPlainText(content).trimmed()).exists() && QFileInfo(StringUtils::htmlToPlainText(content).trimmed()).isAbsolute())) {
        
        QString plainContent = StringUtils::htmlToPlainText(content).trimmed();
        bool isExplicitPath = (itemType == "local_file" || itemType == "local_folder" || itemType == "local_batch");

        QString path = isExplicitPath ? content : plainContent;
        QString fullPath = path;
        if (path.startsWith("attachments/")) {
            fullPath = QCoreApplication::applicationDirPath() + "/" + path;
        }

        QFileInfo fi(fullPath);
        if (fi.exists()) {
            QMimeData* mimeData = new QMimeData();
            if (itemType == "local_batch") {
                QDir dir(fullPath);
                QList<QUrl> urls;
                for (const QString& fileName : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                    urls << QUrl::fromLocalFile(dir.absoluteFilePath(fileName));
                }
                if (urls.isEmpty()) urls << QUrl::fromLocalFile(fullPath); 
                mimeData->setUrls(urls);
            } else {
                mimeData->setUrls({QUrl::fromLocalFile(fullPath)});
            }
            ClipboardMonitor::instance().skipNext();
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 文件已丢失或被移动</b>");
        }
    } else if (!blob.isEmpty() && (itemType == "file" || itemType == "folder")) {
        // 数据库附件处理
        QString exportDir = QDir::tempPath() + "/RapidNotes_Export";
        QDir().mkpath(exportDir);
        QString tempPath = exportDir + "/" + title;

        QFile f(tempPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(blob);
            f.close();

            QMimeData* mimeData = new QMimeData();
            mimeData->setUrls({QUrl::fromLocalFile(tempPath)});
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
        }
    } else if (itemType != "text" && !itemType.isEmpty()) {
        // 多文件路径处理
        QStringList rawPaths = content.split(';', Qt::SkipEmptyParts);
        QList<QUrl> validUrls;
        QStringList missingFiles;

        for (const QString& p : rawPaths) {
            QString path = p.trimmed().remove('\"');
            if (QFileInfo::exists(path)) {
                validUrls << QUrl::fromLocalFile(path);
            } else {
                missingFiles << QFileInfo(path).fileName();
            }
        }

        if (!validUrls.isEmpty()) {
            QMimeData* mimeData = new QMimeData();
            mimeData->setUrls(validUrls);
            QApplication::clipboard()->setMimeData(mimeData);
        } else {
            QApplication::clipboard()->setText(content);
            if (!missingFiles.isEmpty()) {
                ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 原文件已丢失，已复制路径文本</b>", 700);
            }
        }
    } else {
        // 纯文本处理
        StringUtils::copyNoteToClipboard(content);
    }

    // 显示提示
    QString displayTitle = title.length() > 30 ? title.left(27) + "..." : title;
    ToolTipOverlay::instance()->showText(QCursor::pos(), 
        QString("<b style='color: #2ecc71;'>[已发送] %1</b>").arg(displayTitle));

#ifdef Q_OS_WIN
    // 切换到目标窗口并粘贴 - 与 QuickWindow 保持一致
    if (m_prevHwnd && IsWindow(m_prevHwnd)) {
        DWORD currThread = GetCurrentThreadId();
        bool attached = false;
        if (m_prevThreadId != 0 && m_prevThreadId != currThread) {
            attached = AttachThreadInput(currThread, m_prevThreadId, TRUE);
        }

        if (IsIconic(m_prevHwnd)) {
            ShowWindow(m_prevHwnd, SW_RESTORE);
        }
        SetForegroundWindow(m_prevHwnd);

        if (m_prevFocusHwnd && IsWindow(m_prevFocusHwnd)) {
            SetFocus(m_prevFocusHwnd);
        }

        DWORD lastThread = m_prevThreadId;
        QTimer::singleShot(300, [lastThread, attached]() {
            // 释放所有修饰键
            INPUT releaseInputs[8];
            memset(releaseInputs, 0, sizeof(releaseInputs));
            BYTE keys[] = { VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN };
            for (int i = 0; i < 8; ++i) {
                releaseInputs[i].type = INPUT_KEYBOARD;
                releaseInputs[i].ki.wVk = keys[i];
                releaseInputs[i].ki.dwFlags = KEYEVENTF_KEYUP;
            }
            SendInput(8, releaseInputs, sizeof(INPUT));

            // 发送 Ctrl+V
            INPUT inputs[4];
            memset(inputs, 0, sizeof(inputs));
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_LCONTROL;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = 'V';
            inputs[2].type = INPUT_KEYBOARD;
            inputs[2].ki.wVk = 'V';
            inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[3].type = INPUT_KEYBOARD;
            inputs[3].ki.wVk = VK_LCONTROL;
            inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, inputs, sizeof(INPUT));

            if (attached) {
                AttachThreadInput(GetCurrentThreadId(), lastThread, FALSE);
            }
        });
    }
#endif
}

void RecentNotesWindow::switchView(ViewType type) {
    if (m_viewType != type) {
        m_viewType = type;
        updateTabStyles();
        loadRecentNotes();
    }
}

void RecentNotesWindow::updateTabStyles() {
    m_tabRecent->setProperty("active", m_viewType == Recent);
    m_tabFavorite->setProperty("active", m_viewType == Favorite);
    
    m_tabRecent->style()->unpolish(m_tabRecent);
    m_tabRecent->style()->polish(m_tabRecent);
    m_tabFavorite->style()->unpolish(m_tabFavorite);
    m_tabFavorite->style()->polish(m_tabFavorite);
}
