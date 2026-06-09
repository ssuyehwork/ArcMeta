#include "KeywordSearchWidget.h"
#include "ToolTipOverlay.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QDirIterator>
#include <utility>
#include <QTextStream>
#include <QRegularExpression>
#include <QDateTime>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QtConcurrent>
#include <QScrollBar>
#include <QSettings>
#include <QMenu>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QCheckBox>
#include <QProgressBar>
#include <QDrag>
#include <QPixmap>

#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>

// ----------------------------------------------------------------------------
// KeywordSearchHistory 相关辅助类 (复刻 FileSearchHistoryPopup 逻辑)
// ----------------------------------------------------------------------------
class KeywordResultListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    void startDrag(Qt::DropActions supportedActions) override {
        QList<QListWidgetItem*> items = selectedItems();
        if (items.isEmpty()) return;

        QMimeData* mime = new QMimeData();
        QList<QUrl> urls;
        QStringList paths;
        for (auto* item : items) {
            QString p = item->data(Qt::UserRole).toString();
            if (!p.isEmpty()) {
                urls << QUrl::fromLocalFile(p);
                paths << p;
            }
        }
        mime->setUrls(urls);
        mime->setText(paths.join("\n"));

        QDrag* drag = new QDrag(this);
        drag->setMimeData(mime);
        
        QPixmap pixmap(1, 1);
        pixmap.fill(Qt::transparent);
        drag->setPixmap(pixmap);
        
        drag->exec(supportedActions);
    }
};

class KeywordChip : public QFrame {
    Q_OBJECT
public:
    KeywordChip(const QString& text, QWidget* parent = nullptr) : QFrame(parent), m_text(text) {
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        setObjectName("KeywordChip");
        
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(10);
        
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet("border: none; background: transparent; color: #DDD; font-size: 13px;");
        layout->addWidget(lbl);
        layout->addStretch();
        
        auto* btnDel = new QPushButton();
        btnDel->setIcon(IconHelper::getIcon("close", "#666", 16));
        btnDel->setIconSize(QSize(10, 10));
        btnDel->setFixedSize(16, 16);
        btnDel->setCursor(Qt::PointingHandCursor);
        btnDel->setStyleSheet(
            "QPushButton { background-color: transparent; border-radius: 4px; padding: 0px; }"
            "QPushButton:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
        );
        
        connect(btnDel, &QPushButton::clicked, this, [this](){ emit deleted(m_text); });
        layout->addWidget(btnDel);

        setStyleSheet(
            "#KeywordChip { background-color: transparent; border: none; border-radius: 4px; }"
            "#KeywordChip:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
        );
    }
    
    void mousePressEvent(QMouseEvent* e) override { 
        if(e->button() == Qt::LeftButton) emit clicked(m_text); 
        QFrame::mousePressEvent(e);
    }

signals:
    void clicked(const QString& text);
    void deleted(const QString& text);
private:
    QString m_text;
};

class KeywordSearchHistoryPopup : public QWidget {
    Q_OBJECT
public:
    enum Type { Path, Keyword, Replace };

    explicit KeywordSearchHistoryPopup(KeywordSearchWidget* widget, QLineEdit* edit, Type type) 
        : QWidget(widget->window(), Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) 
    {
        m_widget = widget;
        m_edit = edit;
        m_type = type;
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(12, 12, 12, 12);
        
        auto* container = new QFrame();
        container->setObjectName("PopupContainer");
        container->setStyleSheet(
            "#PopupContainer { background-color: #252526; border: 1px solid #444; border-radius: 10px; }"
        );
        rootLayout->addWidget(container);

        auto* shadow = new QGraphicsDropShadowEffect(container);
        shadow->setBlurRadius(20); shadow->setXOffset(0); shadow->setYOffset(5);
        shadow->setColor(QColor(0, 0, 0, 120));
        container->setGraphicsEffect(shadow);

        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);

        auto* top = new QHBoxLayout();
        QString titleStr = "最近记录";
        if (m_type == Path) titleStr = "最近扫描路径";
        else if (m_type == Keyword) titleStr = "最近查找内容";
        else if (m_type == Replace) titleStr = "最近替换内容";

        auto* icon = new QLabel();
        icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14));
        icon->setStyleSheet("border: none; background: transparent;");
//         icon->setToolTip(StringUtils::wrapToolTip(titleStr));
        top->addWidget(icon);

        top->addStretch();

        auto* clearBtn = new QPushButton();
        clearBtn->setIcon(IconHelper::getIcon("trash", "#e74c3c", 14)); // 2026-03-13 统一 Trash 图标颜色为红色
        clearBtn->setIconSize(QSize(14, 14));
        clearBtn->setFixedSize(20, 20);
        clearBtn->setCursor(Qt::PointingHandCursor);
//         clearBtn->setToolTip(StringUtils::wrapToolTip("清空历史记录"));
        clearBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
        connect(clearBtn, &QPushButton::clicked, [this](){
            clearAllHistory();
            refreshUI();
        });
        top->addWidget(clearBtn);
        layout->addLayout(top);

        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setStyleSheet(
            "QScrollArea { background-color: transparent; border: none; }"
            "QScrollArea > QWidget > QWidget { background-color: transparent; }"
        );
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_chipsWidget = new QWidget();
        m_chipsWidget->setStyleSheet("background-color: transparent;");
        m_vLayout = new QVBoxLayout(m_chipsWidget);
        m_vLayout->setContentsMargins(0, 0, 0, 0);
        m_vLayout->setSpacing(2);
        m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget);
        layout->addWidget(scroll);

        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity");
        m_opacityAnim->setDuration(200);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void clearAllHistory() {
        QString key = "keywordList";
        if (m_type == Path) key = "pathList";
        else if (m_type == Replace) key = "replaceList";

        QSettings settings("SearchTool_Standalone", "KeywordSearchHistory");
        settings.setValue(key, QStringList());
    }

    void removeEntry(const QString& text) {
        QString key = "keywordList";
        if (m_type == Path) key = "pathList";
        else if (m_type == Replace) key = "replaceList";

        QSettings settings("SearchTool_Standalone", "KeywordSearchHistory");
        QStringList history = settings.value(key).toStringList();
        history.removeAll(text);
        settings.setValue(key, history);
    }

    QStringList getHistory() const {
        QString key = "keywordList";
        if (m_type == Path) key = "pathList";
        else if (m_type == Replace) key = "replaceList";

        QSettings settings("SearchTool_Standalone", "KeywordSearchHistory");
        return settings.value(key).toStringList();
    }

    void refreshUI() {
        QLayoutItem* item;
        while ((item = m_vLayout->takeAt(0))) {
            if(item->widget()) item->widget()->deleteLater();
            delete item;
        }
        m_vLayout->addStretch();
        
        QStringList history = getHistory();
        if(history.isEmpty()) {
            auto* lbl = new QLabel("暂无历史记录");
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px; border: none;");
            m_vLayout->insertWidget(0, lbl);
        } else {
            for(const QString& val : history) {
                auto* chip = new KeywordChip(val);
                chip->setFixedHeight(32);
                connect(chip, &KeywordChip::clicked, this, [this](const QString& v){ 
                    m_edit->setText(v);
                    close(); 
                });
                connect(chip, &KeywordChip::deleted, this, [this](const QString& v){ 
                    removeEntry(v);
                    refreshUI(); 
                });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        
        int targetWidth = m_edit->width();
        int contentHeight = qMin(410, (int)history.size() * 34 + 60);
        setFixedWidth(targetWidth + 24);
        resize(targetWidth + 24, contentHeight);
    }

public:
    void showAnimated() {
        refreshUI();
        QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height()));
        move(pos.x() - 12, pos.y() - 7);
        setWindowOpacity(0);
        show();
        m_opacityAnim->setStartValue(0);
        m_opacityAnim->setEndValue(1);
        m_opacityAnim->start();
    }

private:
    KeywordSearchWidget* m_widget;
    QLineEdit* m_edit;
    Type m_type;
    QWidget* m_chipsWidget;
    QVBoxLayout* m_vLayout;
    QPropertyAnimation* m_opacityAnim;
};

// ----------------------------------------------------------------------------
// KeywordSearchWidget 实现
// ----------------------------------------------------------------------------
KeywordSearchWidget::KeywordSearchWidget(QWidget* parent) : QWidget(parent) {
    m_ignoreDirs = {".git", ".svn", ".idea", ".vscode", "__pycache__", "node_modules", "dist", "build", "venv"};
    setupStyles();
    initUI();
}

KeywordSearchWidget::~KeywordSearchWidget() {
}

void KeywordSearchWidget::setupStyles() {
    setStyleSheet(R"(
        QWidget {
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 14px;
            color: #E0E0E0;
            outline: none;
        }
        QSplitter::handle {
            background-color: #333;
        }
        QListWidget {
            background-color: #252526; 
            border: 1px solid #333333;
            border-radius: 6px;
            padding: 4px;
        }
        QListWidget::item {
            height: 30px;
            padding-left: 8px;
            border-radius: 4px;
            color: #CCCCCC;
        }
        QListWidget::item:selected {
            background-color: #3e3e42; // 2026-03-xx 统一选中色
            border-left: 3px solid #007ACC;
            color: #FFFFFF;
        }
        QListWidget::item:hover {
            background-color: #2A2D2E;
        }
        QLineEdit {
            background-color: #333333;
            border: 1px solid #444444;
            color: #FFFFFF;
            border-radius: 6px;
            padding: 8px;
            selection-background-color: #264F78;
        }
        QLineEdit:focus {
            border: 1px solid #007ACC;
            background-color: #2D2D2D;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #555555;
            min-height: 20px;
            border-radius: 4px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )");
}

void KeywordSearchWidget::initUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(15);

    auto* rightWidget = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(15);

    // --- 配置区域 ---
    auto* configGroup = new QWidget();
    auto* configLayout = new QGridLayout(configGroup);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->setHorizontalSpacing(10); 
    configLayout->setVerticalSpacing(10);
    configLayout->setColumnStretch(1, 1);
    configLayout->setColumnStretch(0, 0);
    configLayout->setColumnStretch(2, 0);

    auto createLabel = [](const QString& text) {
        auto* lbl = new QLabel(text);
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lbl->setStyleSheet("color: #AAA; font-weight: bold; border: none; background: transparent;");
        return lbl;
    };

    auto setEditStyle = [](QLineEdit* edit) {
        edit->setClearButtonEnabled(true);
        edit->setStyleSheet(
            "QLineEdit { background: #252526; border: 1px solid #333; border-radius: 4px; padding: 6px; color: #EEE; }"
            "QLineEdit:focus { border-color: #007ACC; }"
        );
    };

    // 1. 搜索目录
    configLayout->addWidget(createLabel("搜索目录:"), 0, 0);
    m_pathEdit = new ClickableLineEdit();
    m_pathEdit->setPlaceholderText("选择搜索根目录 (双击查看历史)...");
    setEditStyle(m_pathEdit);
    m_pathEdit->installEventFilter(this);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_pathEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_pathEdit, 0, 1);

    auto* browseBtn = new QPushButton();
    browseBtn->setFixedSize(38, 32);
    browseBtn->setIcon(IconHelper::getIcon("folder", "#EEE", 18));
//     browseBtn->setToolTip(StringUtils::wrapToolTip("浏览文件夹"));
    browseBtn->setAutoDefault(false);
    browseBtn->setCursor(Qt::PointingHandCursor);
    browseBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; } QPushButton:hover { background: #4E4E52; }");
    connect(browseBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onBrowseFolder);
    configLayout->addWidget(browseBtn, 0, 2);

    // 2. 文件过滤
    configLayout->addWidget(createLabel("文件过滤:"), 1, 0);
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("例如: *.py, *.txt (留空则扫描所有文本文件)");
    setEditStyle(m_filterEdit);
    m_filterEdit->installEventFilter(this);
    connect(m_filterEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    configLayout->addWidget(m_filterEdit, 1, 1, 1, 2);

    // 3. 查找内容
    configLayout->addWidget(createLabel("查找内容:"), 2, 0);
    m_searchEdit = new ClickableLineEdit();
    m_searchEdit->setPlaceholderText("输入要查找的内容 (双击查看历史)...");
    setEditStyle(m_searchEdit);
    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_searchEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_searchEdit, 2, 1);

    // 4. 替换内容
    configLayout->addWidget(createLabel("替换内容:"), 3, 0);
    m_replaceEdit = new ClickableLineEdit();
    m_replaceEdit->setPlaceholderText("替换为 (双击查看历史)...");
    setEditStyle(m_replaceEdit);
    m_replaceEdit->installEventFilter(this);
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &KeywordSearchWidget::onSearch);
    connect(m_replaceEdit, &ClickableLineEdit::doubleClicked, this, &KeywordSearchWidget::onShowHistory);
    configLayout->addWidget(m_replaceEdit, 3, 1);

    // 交换按钮 (跨越查找和替换行)
    auto* swapBtn = new QPushButton();
    swapBtn->setFixedSize(32, 74); 
    swapBtn->setCursor(Qt::PointingHandCursor);
//     swapBtn->setToolTip(StringUtils::wrapToolTip("交换查找与替换内容"));
    swapBtn->setIcon(IconHelper::getIcon("swap", "#AAA", 20));
    swapBtn->setAutoDefault(false);
    swapBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; } QPushButton:hover { background: #4E4E52; }");
    connect(swapBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSwapSearchReplace);
    configLayout->addWidget(swapBtn, 2, 2, 2, 1);

    // 选项
    m_caseCheck = new QCheckBox("区分大小写");
    m_caseCheck->setStyleSheet("QCheckBox { color: #AAA; }");
    configLayout->addWidget(m_caseCheck, 4, 1, 1, 2);

    rightLayout->addWidget(configGroup);

    // --- 按钮区域 ---
    auto* btnLayout = new QHBoxLayout();
    auto* searchBtn = new QPushButton(" 智能搜索");
    searchBtn->setAutoDefault(false);
    searchBtn->setIcon(IconHelper::getIcon("find_keyword", "#FFF", 16));
    searchBtn->setStyleSheet("QPushButton { background: #007ACC; border: none; border-radius: 4px; padding: 8px 20px; color: #FFF; font-weight: bold; } QPushButton:hover { background: #0098FF; }");
    connect(searchBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onSearch);

    auto* replaceBtn = new QPushButton(" 执行替换");
    replaceBtn->setAutoDefault(false);
    replaceBtn->setIcon(IconHelper::getIcon("edit", "#FFF", 16));
    replaceBtn->setStyleSheet("QPushButton { background: #D32F2F; border: none; border-radius: 4px; padding: 8px 20px; color: #FFF; font-weight: bold; } QPushButton:hover { background: #F44336; }");
    connect(replaceBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onReplace);

    auto* undoBtn = new QPushButton(" 撤销替换");
    undoBtn->setAutoDefault(false);
    undoBtn->setIcon(IconHelper::getIcon("undo", "#EEE", 16));
    undoBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; padding: 8px 20px; color: #EEE; } QPushButton:hover { background: #4E4E52; }");
    connect(undoBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onUndo);

    auto* clearBtn = new QPushButton(" 清空日志");
    clearBtn->setAutoDefault(false);
    clearBtn->setIcon(IconHelper::getIcon("trash", "#e74c3c", 16)); // 2026-03-13 统一 Trash 图标颜色为红色
    clearBtn->setStyleSheet("QPushButton { background: #3E3E42; border: none; border-radius: 4px; padding: 8px 20px; color: #EEE; } QPushButton:hover { background: #4E4E52; }");
    connect(clearBtn, &QPushButton::clicked, this, &KeywordSearchWidget::onClearLog);

    btnLayout->addWidget(searchBtn);
    btnLayout->addWidget(replaceBtn);
    btnLayout->addWidget(undoBtn);
    btnLayout->addWidget(clearBtn);
    btnLayout->addStretch();
    rightLayout->addLayout(btnLayout);

    // --- 结果展示区域 ---
    m_resultList = new KeywordResultListWidget();
    m_resultList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultList->setDragEnabled(true);
    m_resultList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_resultList, &QListWidget::customContextMenuRequested, this, &KeywordSearchWidget::showResultContextMenu);
    connect(m_resultList, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    rightLayout->addWidget(m_resultList, 1);

    // --- 状态栏 ---
    auto* statusLayout = new QVBoxLayout();
    m_progressBar = new QProgressBar();
    m_progressBar->setFixedHeight(4);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet("QProgressBar { background: #252526; border: none; } QProgressBar::chunk { background: #007ACC; }");
    m_progressBar->hide();
    
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    
    statusLayout->addWidget(m_progressBar);
    statusLayout->addWidget(m_statusLabel);
    rightLayout->addLayout(statusLayout);

    mainLayout->addWidget(rightWidget);
}

void KeywordSearchWidget::setSearchPath(const QString& path) {
    m_pathEdit->setText(path);
    onSearch();
}

QString KeywordSearchWidget::currentPath() const {
    return m_pathEdit->text().trimmed();
}

void KeywordSearchWidget::onBrowseFolder() {
    QString folder = QFileDialog::getExistingDirectory(this, "选择搜索目录");
    if (!folder.isEmpty()) {
        m_pathEdit->setText(folder);
    }
}

void KeywordSearchWidget::showResultContextMenu(const QPoint& pos) {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) {
        auto* item = m_resultList->itemAt(pos);
        if (item) {
            item->setSelected(true);
            selectedItems << item;
        }
    }
    if (selectedItems.isEmpty()) return;

    QMenu menu(this);
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setAttribute(Qt::WA_NoSystemBackground);

    QStringList paths;
    for (auto* item : selectedItems) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }

    if (paths.size() == 1) {
        QString filePath = paths.first();
        menu.addAction(IconHelper::getIcon("folder", "#F1C40F", 18), "定位文件夹", [filePath](){
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
        });
        menu.addAction(IconHelper::getIcon("search", "#4A90E2", 18), "定位文件", [filePath](){
#ifdef Q_OS_WIN
            QStringList args;
            args << "/select," << QDir::toNativeSeparators(filePath);
            QProcess::startDetached("explorer.exe", args);
#endif
        });
        menu.addAction(IconHelper::getIcon("edit", "#3498DB", 18), "编辑", [this](){ onEditFile(); });
        menu.addSeparator();
    }

    // [USER_REQUEST] 补全关键字查找菜单，并与文件查找保持完全视觉一致
    QString copyPathText = selectedItems.size() > 1 ? "复制选中路径" : "复制完整路径";
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71", 18), copyPathText, [paths](){
        QApplication::clipboard()->setText(paths.join("\n"));
    });

    QString copyNameText = selectedItems.size() > 1 ? "复制选中文件名" : "复制文件名";
    menu.addAction(IconHelper::getIcon("file_export", "#2ECC71", 18), copyNameText, [paths](){
        QStringList names;
        for (const auto& p : paths) names << QFileInfo(p).fileName();
        QApplication::clipboard()->setText(names.join("\n"));
    });

    QString copyFileText = selectedItems.size() > 1 ? "复制选中文件" : "复制文件";
    menu.addAction(IconHelper::getIcon("file", "#4A90E2", 18), copyFileText, [this](){ copySelectedFiles(); });

    menu.addAction(IconHelper::getIcon("Favorite", "#F1C40F", 18), "收藏文件", [this, paths](){
        emit requestAddFileFavorite(paths);
    });

    menu.addAction(IconHelper::getIcon("merge", "#3498DB", 18), "合并选中内容", [this](){ onMergeSelectedFiles(); });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("cut", "#E67E22", 18), "剪切", [this](){ onCutFile(); });
    menu.addAction(IconHelper::getIcon("trash", "#E74C3C", 18), "删除", [this](){ onDeleteFile(); });

    menu.exec(m_resultList->mapToGlobal(pos));
}

void KeywordSearchWidget::onEditFile() {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;
    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }
    QSettings settings("RapidNotes", "ExternalEditor");
    QString editorPath = settings.value("EditorPath").toString();
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        for (const QString& p : {"C:/Program Files/Notepad++/notepad++.exe", "C:/Program Files (x86)/Notepad++/notepad++.exe"}) {
            if (QFile::exists(p)) { editorPath = p; break; }
        }
    }
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        editorPath = QFileDialog::getOpenFileName(this, "选择编辑器", "C:/Program Files", "Executable (*.exe)");
        if (editorPath.isEmpty()) return;
        settings.setValue("EditorPath", editorPath);
    }
    for (const QString& fp : paths) QProcess::startDetached(editorPath, { QDir::toNativeSeparators(fp) });
}

void KeywordSearchWidget::copySelectedFiles() {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;
    QList<QUrl> urls;
    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) {
            urls << QUrl::fromLocalFile(p);
            paths << p;
        }
    }

    // 2026-03-20 按照用户要求：复制文件也属于数据导出，必须验证
    if (!verifyExportPermission()) return;

    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);
    mimeData->setText(paths.join("\n"));
    QApplication::clipboard()->setMimeData(mimeData);
    // 2026-03-13 按照用户要求：提示时长缩短为 700ms
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已复制到剪贴板", 700);
}

void KeywordSearchWidget::onCutFile() {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;
    QList<QUrl> urls;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) urls << QUrl::fromLocalFile(p);
    }
    if (urls.isEmpty()) return;
    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);
#ifdef Q_OS_WIN
    QByteArray data; data.resize(4); data[0] = 2; data[1] = 0; data[2] = 0; data[3] = 0;
    mimeData->setData("Preferred DropEffect", data);
#endif
    QApplication::clipboard()->setMimeData(mimeData);
    // 2026-03-13 按照用户要求：提示时长缩短为 700ms
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已剪切到剪贴板", 700);
}

void KeywordSearchWidget::onDeleteFile() {
    auto selectedItems = m_resultList->selectedItems();
    if (selectedItems.isEmpty()) return;
    int successCount = 0;
    for (auto* item : std::as_const(selectedItems)) {
        QString fp = item->data(Qt::UserRole).toString();
        if (fp.isEmpty()) continue;
        if (QFile::moveToTrash(fp)) {
            successCount++;
            delete item;
        }
    }
    if (successCount > 0) ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已删除");
}

void KeywordSearchWidget::onMergeSelectedFiles() {
    QStringList paths;
    for (auto* item : m_resultList->selectedItems()) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }
    if (paths.isEmpty()) return;
    QString rootPath = m_pathEdit->text().trimmed();
    if (!QDir(rootPath).exists()) rootPath = QFileInfo(paths.first()).absolutePath();
    onMergeFiles(paths, rootPath);
}

#include "PasswordVerifyDialog.h"

bool KeywordSearchWidget::verifyExportPermission() {
    // 2026-03-20 按照用户要求，所有导出操作必须验证身份
    return PasswordVerifyDialog::verify();
}

void KeywordSearchWidget::onMergeFiles(const QStringList& filePaths, const QString& rootPath) {
    if (filePaths.isEmpty()) return;
    
    // 2026-03-20 按照用户要求，所有导出操作必须验证身份
    if (!verifyExportPermission()) return;

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outName = QString("%1_keyword_export.md").arg(ts);
    QString outPath = QDir(rootPath).filePath(outName);
    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&outFile);
    out.setEncoding(QStringConverter::Utf8);
    out << "# 关键字搜索导出结果 - " << ts << "\n\n";
    for (const QString& fp : filePaths) {
        QString relPath = QDir(rootPath).relativeFilePath(fp);
        out << "## 文件: `" << relPath << "`\n\n```\n";
        QFile inFile(fp);
        if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) out << inFile.readAll();
        out << "\n```\n\n";
    }
    outFile.close();
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已保存: " + outName);
}

bool KeywordSearchWidget::eventFilter(QObject* watched, QEvent* event) {
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
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            if (watched == m_pathEdit || watched == m_filterEdit || 
                watched == m_searchEdit || watched == m_replaceEdit) 
            {
                // [MODIFIED] 两段式：不为空则清空内容，否则清除焦点
                QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
                if (edit) {
                    if (!edit->text().isEmpty()) {
                        edit->clear();
                    } else {
                        edit->clearFocus();
                    }
                }
                event->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool KeywordSearchWidget::isTextFile(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QByteArray chunk = file.read(1024);
    file.close();

    if (chunk.isEmpty()) return true;
    if (chunk.contains('\0')) return false;

    return true;
}

void KeywordSearchWidget::log(const QString& msg, const QString& type, int count) {
    if (type == "file") {
        auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#E1523D"), 
            QString("%1 (匹配 %2 次)").arg(QFileInfo(msg).fileName()).arg(count));
        item->setData(Qt::UserRole, msg);
//         item->setToolTip(StringUtils::wrapToolTip(msg));
        m_resultList->addItem(item);
    }
}

void KeywordSearchWidget::onSearch() {
    QString rootDir = m_pathEdit->text().trimmed();
    QString keyword = m_searchEdit->text().trimmed();
    QString replaceText = m_replaceEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>[ERR] 目录和查找内容不能为空!</b>"));
        return;
    }

    // 保存历史记录
    addHistoryEntry(Path, rootDir);
    addHistoryEntry(Keyword, keyword);
    if (!replaceText.isEmpty()) {
        addHistoryEntry(Replace, replaceText);
    }

    m_resultList->clear();
    m_progressBar->show();
    m_progressBar->setRange(0, 0);
    m_statusLabel->setText("正在搜索...");

    QString filter = m_filterEdit->text();
    bool caseSensitive = m_caseCheck->isChecked();

    (void)QtConcurrent::run([this, rootDir, keyword, filter, caseSensitive]() {
        int foundFiles = 0;
        int scannedFiles = 0;

        QStringList filters;
        if (!filter.isEmpty()) {
            filters = filter.split(QRegularExpression("[,\\s;]+"), Qt::SkipEmptyParts);
        }

        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            
            // 过滤目录
            bool skip = false;
            for (const QString& ignore : m_ignoreDirs) {
                if (filePath.contains("/" + ignore + "/") || filePath.contains("\\" + ignore + "\\")) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            // 过滤文件名
            if (!filters.isEmpty()) {
                bool matchFilter = false;
                QString fileName = QFileInfo(filePath).fileName();
                for (const QString& f : filters) {
                    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(f));
                    if (re.match(fileName).hasMatch()) {
                        matchFilter = true;
                        break;
                    }
                }
                if (!matchFilter) continue;
            }

            if (!isTextFile(filePath)) continue;

            scannedFiles++;
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                QString content = in.readAll();
                file.close();

                Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
                if (content.contains(keyword, cs)) {
                    foundFiles++;
                    int count = content.count(keyword, cs);
                    QMetaObject::invokeMethod(this, [this, filePath, count]() {
                        log(filePath, "file", count);
                    });
                }
            }
        }

        QMetaObject::invokeMethod(this, [this, scannedFiles, foundFiles, keyword, caseSensitive]() {
            m_statusLabel->setText(QString("完成: 找到 %1 个文件 (扫描了 %2 个)").arg(foundFiles).arg(scannedFiles));
            m_progressBar->hide();
        });
    });
}


void KeywordSearchWidget::onReplace() {
    QString rootDir = m_pathEdit->text().trimmed();
    QString keyword = m_searchEdit->text().trimmed();
    QString replaceText = m_replaceEdit->text().trimmed();
    if (rootDir.isEmpty() || keyword.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>[ERR] 目录和查找内容不能为空!</b>"));
        return;
    }

    // 保存历史记录
    addHistoryEntry(Path, rootDir);
    addHistoryEntry(Keyword, keyword);
    if (!replaceText.isEmpty()) {
        addHistoryEntry(Replace, replaceText);
    }

    // 遵从非阻塞规范，直接执行替换（已有备份机制）
    ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #007acc;'>[INFO] 正在开始批量替换...</b>"));

    m_progressBar->show();
    m_progressBar->setRange(0, 0);
    m_statusLabel->setText("正在替换...");

    QString filter = m_filterEdit->text();
    bool caseSensitive = m_caseCheck->isChecked();

    (void)QtConcurrent::run([this, rootDir, keyword, replaceText, filter, caseSensitive]() {
        int modifiedFiles = 0;
        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString backupDirName = "_backup_" + timestamp;
        QDir root(rootDir);
        root.mkdir(backupDirName);
        m_lastBackupPath = root.absoluteFilePath(backupDirName);

        QStringList filters;
        if (!filter.isEmpty()) {
            filters = filter.split(QRegularExpression("[,\\s;]+"), Qt::SkipEmptyParts);
        }

        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString filePath = it.next();
            if (filePath.contains(backupDirName)) continue;

            // 过滤目录和文件名（逻辑同搜索）
            bool skip = false;
            for (const QString& ignore : m_ignoreDirs) {
                if (filePath.contains("/" + ignore + "/") || filePath.contains("\\" + ignore + "\\")) {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            if (!filters.isEmpty()) {
                bool matchFilter = false;
                QString fileName = QFileInfo(filePath).fileName();
                for (const QString& f : filters) {
                    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(f));
                    if (re.match(fileName).hasMatch()) {
                        matchFilter = true;
                        break;
                    }
                }
                if (!matchFilter) continue;
            }

            if (!isTextFile(filePath)) continue;

            QFile file(filePath);
            if (file.open(QIODevice::ReadWrite | QIODevice::Text)) {
                QTextStream in(&file);
                QString content = in.readAll();
                
                Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
                if (content.contains(keyword, cs)) {
                    // 备份
                    QString fileName = QFileInfo(filePath).fileName();
                    QFile::copy(filePath, m_lastBackupPath + "/" + fileName + ".bak");

                    // 替换
                    QString newContent;
                    if (caseSensitive) {
                        newContent = content.replace(keyword, replaceText);
                    } else {
                        newContent = content.replace(QRegularExpression(QRegularExpression::escape(keyword), QRegularExpression::CaseInsensitiveOption), replaceText);
                    }

                    file.resize(0);
                    in << newContent;
                    modifiedFiles++;
                }
                file.close();
            }
        }

        QMetaObject::invokeMethod(this, [this, modifiedFiles]() {
            m_statusLabel->setText(QString("替换完成: 修改了 %1 个文件").arg(modifiedFiles));
            m_progressBar->hide();
            ToolTipOverlay::instance()->showText(QCursor::pos(), 
                StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>[OK] 已修改 %1 个文件 (备份于 %2)</b>")
                .arg(modifiedFiles).arg(QFileInfo(m_lastBackupPath).fileName())));
        });
    });
}

void KeywordSearchWidget::onUndo() {
    if (m_lastBackupPath.isEmpty() || !QDir(m_lastBackupPath).exists()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>[ERR] 未找到有效的备份目录！</b>"));
        return;
    }

    int restored = 0;
    QDir backupDir(m_lastBackupPath);
    QStringList baks = backupDir.entryList({"*.bak"});
    
    QString rootDir = m_pathEdit->text();

    for (const QString& bak : baks) {
        QString origName = bak.left(bak.length() - 4);
        
        // 在根目录下寻找原始文件（简化策略：找同名文件）
        QDirIterator it(rootDir, {origName}, QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) {
            QString targetPath = it.next();
            if (QFile::remove(targetPath)) {
                if (QFile::copy(backupDir.absoluteFilePath(bak), targetPath)) {
                    restored++;
                }
            }
        }
    }

    m_statusLabel->setText(QString("撤销完成，已恢复 %1 个文件").arg(restored));
    ToolTipOverlay::instance()->showText(QCursor::pos(), 
        StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>[OK] 已恢复 %1 个文件</b>").arg(restored)));
}

void KeywordSearchWidget::onClearLog() {
    m_resultList->clear();
    m_statusLabel->setText("就绪");
}

void KeywordSearchWidget::clearAllInputs() {
    m_pathEdit->clear();
    m_filterEdit->clear();
    m_searchEdit->clear();
    m_replaceEdit->clear();
}

void KeywordSearchWidget::focusSearchInput() {
    // [USER_REQUEST] 当按下 Ctrl+F 时，定位到查找内容输入框
    if (m_searchEdit) {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    }
}

void KeywordSearchWidget::onResultDoubleClicked(const QModelIndex& index) {
}

void KeywordSearchWidget::onSwapSearchReplace() {
    QString searchTxt = m_searchEdit->text();
    QString replaceTxt = m_replaceEdit->text();
    m_searchEdit->setText(replaceTxt);
    m_replaceEdit->setText(searchTxt);
}

void KeywordSearchWidget::addHistoryEntry(HistoryType type, const QString& text) {
    if (text.isEmpty()) return;
    QString key = "keywordList";
    if (type == Path) key = "pathList";
    else if (type == Replace) key = "replaceList";

    QSettings settings("SearchTool_Standalone", "KeywordSearchHistory");
    QStringList history = settings.value(key).toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 10) history.removeLast();
    settings.setValue(key, history);
}

void KeywordSearchWidget::onShowHistory() {
    auto* edit = qobject_cast<ClickableLineEdit*>(sender());
    if (!edit) return;

    KeywordSearchHistoryPopup::Type type = KeywordSearchHistoryPopup::Keyword;
    if (edit == m_pathEdit) type = KeywordSearchHistoryPopup::Path;
    else if (edit == m_replaceEdit) type = KeywordSearchHistoryPopup::Replace;
    
    auto* popup = new KeywordSearchHistoryPopup(this, edit, type);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->showAnimated();
}

#include "KeywordSearchWidget.moc"