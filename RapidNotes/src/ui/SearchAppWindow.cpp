#include "SearchAppWindow.h"
#include "FileSearchWidget.h"
#include "KeywordSearchWidget.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QApplication>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QClipboard>
#include <QProcess>
#include <QDateTime>
#include <QFileDialog>
#include <QShortcut>

// ----------------------------------------------------------------------------
// 合并逻辑相关常量与辅助函数 (同步自 FileSearchWidget)
// ----------------------------------------------------------------------------
static const QSet<QString> SUPPORTED_EXTENSIONS = {
    ".py", ".pyw", ".cpp", ".cc", ".cxx", ".c", ".h", ".hpp", ".hxx",
    ".java", ".js", ".jsx", ".ts", ".tsx", ".cs", ".go", ".rs", ".swift",
    ".kt", ".kts", ".php", ".rb", ".lua", ".r", ".m", ".scala", ".sh",
    ".bash", ".zsh", ".ps1", ".bat", ".cmd", ".html", ".htm", ".css",
    ".scss", ".sass", ".less", ".xml", ".svg", ".vue", ".json", ".yaml",
    ".yml", ".toml", ".ini", ".cfg", ".conf", ".env", ".properties",
    ".cmake", ".gradle", ".make", ".mk", ".dockerfile", ".md", ".markdown",
    ".txt", ".rst", ".qml", ".qrc", ".qss", ".ui", ".sql", ".graphql",
    ".gql", ".proto", ".asm", ".s", ".v", ".vh", ".vhdl", ".vhd"
};

static const QSet<QString> SPECIAL_FILENAMES = {
    "Makefile", "makefile", "Dockerfile", "dockerfile", "CMakeLists.txt",
    "Rakefile", "Gemfile", ".gitignore", ".dockerignore", ".editorconfig",
    ".eslintrc", ".prettierrc"
};

static QString getFileLanguage(const QString& filePath) {
    QFileInfo fi(filePath);
    QString basename = fi.fileName();
    QString ext = "." + fi.suffix().toLower();
    
    static const QMap<QString, QString> specialMap = {
        {"Makefile", "makefile"}, {"makefile", "makefile"},
        {"Dockerfile", "dockerfile"}, {"dockerfile", "dockerfile"},
        {"CMakeLists.txt", "cmake"}
    };
    if (specialMap.contains(basename)) return specialMap[basename];

    static const QMap<QString, QString> extMap = {
        {".py", "python"}, {".pyw", "python"}, {".cpp", "cpp"}, {".cc", "cpp"},
        {".cxx", "cpp"}, {".c", "c"}, {".h", "cpp"}, {".hpp", "cpp"},
        {".hxx", "cpp"}, {".java", "java"}, {".js", "javascript"},
        {".jsx", "jsx"}, {".ts", "typescript"}, {".tsx", "tsx"},
        {".cs", "csharp"}, {".go", "go"}, {".rs", "rust"}, {".swift", "swift"},
        {".kt", "kotlin"}, {".kts", "kotlin"}, {".php", "php"}, {".rb", "ruby"},
        {".lua", "lua"}, {".r", "r"}, {".m", "objectivec"}, {".scala", "scala"},
        {".sh", "bash"}, {".bash", "bash"}, {".zsh", "zsh"}, {".ps1", "powershell"},
        {".bat", "batch"}, {".cmd", "batch"}, {".html", "html"}, {".htm", "html"},
        {".css", "css"}, {".scss", "scss"}, {".sass", "sass"}, {".less", "less"},
        {".xml", "xml"}, {".svg", "svg"}, {".vue", "vue"}, {".json", "json"},
        {".yaml", "yaml"}, {".yml", "yaml"}, {".toml", "toml"}, {".ini", "ini"},
        {".cfg", "ini"}, {".conf", "conf"}, {".env", "bash"},
        {".properties", "properties"}, {".cmake", "cmake"}, {".gradle", "gradle"},
        {".make", "makefile"}, {".mk", "makefile"}, {".dockerfile", "dockerfile"},
        {".md", "markdown"}, {".markdown", "markdown"}, {".txt", "text"},
        {".rst", "restructuredtext"}, {".qml", "qml"}, {".qrc", "xml"},
        {".qss", "css"}, {".ui", "xml"}, {".sql", "sql"}, {".graphql", "graphql"},
        {".gql", "graphql"}, {".proto", "protobuf"}, {".asm", "asm"},
        {".s", "asm"}, {".v", "verilog"}, {".vh", "verilog"}, {".vhdl", "vhdl"},
        {".vhd", "vhdl"}
    };
    return extMap.value(ext, ext.mid(1).isEmpty() ? "text" : ext.mid(1));
}

static bool isSupportedFile(const QString& filePath) {
    QFileInfo fi(filePath);
    if (SPECIAL_FILENAMES.contains(fi.fileName())) return true;
    return SUPPORTED_EXTENSIONS.contains("." + fi.suffix().toLower());
}

// ----------------------------------------------------------------------------
// Sidebar ListWidget subclass for Drag & Drop
// ----------------------------------------------------------------------------
class GlobalSidebarListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit GlobalSidebarListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
    }
signals:
    void foldersDropped(const QStringList& paths); // [USER_REQUEST] 改为批量路径信号
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        // [USER_REQUEST] 修复原本只取 at(0) 的 Bug，支持批量处理拖入的路径
        QStringList paths;
        if (event->mimeData()->hasUrls()) {
            for (const QUrl& url : event->mimeData()->urls()) {
                QString p = url.toLocalFile();
                if (!p.isEmpty() && QDir(p).exists()) paths << p;
            }
        } else if (event->mimeData()->hasText()) {
            QString t = event->mimeData()->text();
            if (QDir(t).exists()) paths << t;
        }
        
        if (!paths.isEmpty()) {
            emit foldersDropped(paths);
            event->acceptProposedAction();
        }
    }
};

class GlobalFileFavoriteListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit GlobalFileFavoriteListWidget(QWidget* parent = nullptr) : QListWidget(parent) {
        setAcceptDrops(true);
    }
signals:
    void filesDropped(const QStringList& paths);
protected:
    void dragEnterEvent(QDragEnterEvent* event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        event->acceptProposedAction();
    }
    void dropEvent(QDropEvent* event) override {
        QStringList paths;
        if (event->mimeData()->hasUrls()) {
            for (const QUrl& url : event->mimeData()->urls()) {
                QString p = url.toLocalFile();
                if (!p.isEmpty()) paths << p;
            }
        } else if (event->mimeData()->hasText()) {
            paths = event->mimeData()->text().split("\n", Qt::SkipEmptyParts);
        }
        
        if (!paths.isEmpty()) {
            emit filesDropped(paths);
            event->acceptProposedAction();
        }
    }
};

class FavoriteItem : public QListWidgetItem {
public:
    using QListWidgetItem::QListWidgetItem;
    bool operator<(const QListWidgetItem &other) const override {
        bool thisPinned = data(Qt::UserRole + 1).toBool();
        bool otherPinned = other.data(Qt::UserRole + 1).toBool();
        if (thisPinned != otherPinned) return thisPinned; 
        return text().localeAwareCompare(other.text()) < 0;
    }
};

SearchAppWindow::SearchAppWindow(QWidget* parent) 
    : FramelessDialog("搜索工具", parent) 
{
    setObjectName("SearchTool_SearchAppWindow_Standalone");
    resize(1200, 800);
    setupStyles();
    initUI();
    loadFolderFavorites();
    loadFileFavorites();
}

SearchAppWindow::~SearchAppWindow() {
}

void SearchAppWindow::setupStyles() {
    m_tabWidget = new QTabWidget();
    m_tabWidget->setStyleSheet(R"(
        QTabWidget::pane {
            border: 1px solid #333;
            background: #1e1e1e;
            margin-top: -1px;
            border-top-left-radius: 0px;
            border-top-right-radius: 4px;
            border-bottom-left-radius: 4px;
            border-bottom-right-radius: 4px;
        }
        QTabBar::tab {
            background: #2D2D30;
            color: #AAA;
            padding: 10px 20px;
            border: 1px solid #333;
            border-bottom: 1px solid #333;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
        }
        QTabBar::tab:hover {
            background: #3E3E42;
            color: #EEE;
        }
        QTabBar::tab:selected {
            background: #1e1e1e;
            color: #007ACC;
            border-bottom: 1px solid #1e1e1e;
            font-weight: bold;
        }
        QTabBar {
            border-bottom: 1px solid #333;
        }
    )");

    setStyleSheet(R"(
        QWidget {
            font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
            font-size: 14px;
            color: #E0E0E0;
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
            background-color: #37373D;
            border-left: 3px solid #007ACC;
            color: #FFFFFF;
        }
        QListWidget::item:hover {
            background-color: #2A2D2E;
        }
        QSplitter::handle {
            background: transparent;
        }
    )");
}

void SearchAppWindow::initUI() {
    auto* mainHLayout = new QHBoxLayout(m_contentArea);
    mainHLayout->setContentsMargins(10, 5, 10, 10);
    mainHLayout->setSpacing(0);

    auto* splitter = new QSplitter(Qt::Horizontal);
    mainHLayout->addWidget(splitter);

    // --- 左侧：目录收藏 ---
    auto* leftSidebarWidget = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftSidebarWidget);
    leftLayout->setContentsMargins(0, 0, 5, 0);
    leftLayout->setSpacing(10);

    auto* leftHeader = new QHBoxLayout();
    auto* leftIcon = new QLabel();
    leftIcon->setPixmap(IconHelper::getIcon("folder", "#888").pixmap(14, 14));
    leftHeader->addWidget(leftIcon);
    auto* leftTitle = new QLabel("收藏夹 (可拖入)");
    leftTitle->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    leftHeader->addWidget(leftTitle);
    leftHeader->addStretch();
    leftLayout->addLayout(leftHeader);

    auto* sidebar = new GlobalSidebarListWidget();
    m_folderSidebar = sidebar;
    m_folderSidebar->setMinimumWidth(180);
    m_folderSidebar->setContextMenuPolicy(Qt::CustomContextMenu);
    // [USER_REQUEST] 连接批量拖入信号
    connect(sidebar, &GlobalSidebarListWidget::foldersDropped, this, [this](const QStringList& paths){
        this->addFolderFavoriteBatch(paths);
    });
    connect(m_folderSidebar, &QListWidget::itemClicked, this, &SearchAppWindow::onSidebarItemClicked);
    connect(m_folderSidebar, &QListWidget::customContextMenuRequested, this, &SearchAppWindow::showSidebarContextMenu);
    leftLayout->addWidget(m_folderSidebar);

    auto* btnAddFav = new QPushButton("收藏当前路径");
    btnAddFav->setFixedHeight(32);
    btnAddFav->setStyleSheet("QPushButton { background-color: #2D2D30; border: 1px solid #444; color: #AAA; border-radius: 4px; font-size: 12px; } QPushButton:hover { background-color: #3E3E42; color: #FFF; }");
    connect(btnAddFav, &QPushButton::clicked, [this](){
        QString path;
        if (m_tabWidget->currentIndex() == 0) {
            path = m_fileSearchWidget->currentPath();
        } else {
            path = m_keywordSearchWidget->currentPath();
        }
        if (!path.isEmpty() && QDir(path).exists()) {
            addFolderFavorite(path);
        }
    });
    leftLayout->addWidget(btnAddFav);
    splitter->addWidget(leftSidebarWidget);

    // --- 中间：主搜索框 ---
    m_fileSearchWidget = new FileSearchWidget();
    m_keywordSearchWidget = new KeywordSearchWidget();

    m_tabWidget->addTab(m_fileSearchWidget, IconHelper::getIcon("folder", "#AAA"), "文件查找");
    m_tabWidget->addTab(m_keywordSearchWidget, IconHelper::getIcon("find_keyword", "#AAA"), "关键字查找");
    
    connect(m_fileSearchWidget, SIGNAL(requestAddFileFavorite(QStringList)), this, SLOT(addFileFavorite(QStringList)));
    connect(m_keywordSearchWidget, SIGNAL(requestAddFileFavorite(QStringList)), this, SLOT(addFileFavorite(QStringList)));
    connect(m_fileSearchWidget, SIGNAL(requestAddFolderFavorite(QString)), this, SLOT(addFolderFavorite(QString)));
    connect(m_keywordSearchWidget, SIGNAL(requestAddFolderFavorite(QString)), this, SLOT(addFolderFavorite(QString)));

    splitter->addWidget(m_tabWidget);

    // --- 右侧：文件收藏 ---
    auto* rightSidebarWidget = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightSidebarWidget);
    rightLayout->setContentsMargins(5, 0, 0, 0);
    rightLayout->setSpacing(10);

    auto* rightHeader = new QHBoxLayout();
    auto* rightIcon = new QLabel();
    rightIcon->setPixmap(IconHelper::getIcon("Favorite", "#888").pixmap(14, 14));
    rightHeader->addWidget(rightIcon);
    auto* rightTitle = new QLabel("文件收藏");
    rightTitle->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    rightHeader->addWidget(rightTitle);
    rightHeader->addStretch();
    rightLayout->addLayout(rightHeader);

    auto* favList = new GlobalFileFavoriteListWidget();
    m_fileFavoritesList = favList;
    m_fileFavoritesList->setMinimumWidth(180);
    m_fileFavoritesList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileFavoritesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(favList, SIGNAL(filesDropped(QStringList)), this, SLOT(addFileFavorite(QStringList)));
    connect(m_fileFavoritesList, &QListWidget::customContextMenuRequested, this, &SearchAppWindow::showFileFavoriteContextMenu);
    connect(m_fileFavoritesList, &QListWidget::itemDoubleClicked, this, &SearchAppWindow::onFileFavoriteItemDoubleClicked);
    rightLayout->addWidget(m_fileFavoritesList);

    splitter->addWidget(rightSidebarWidget);

    // [USER_REQUEST] 注册 Ctrl+F 快捷键，用于根据当前 Tab 智能聚焦搜索框
    auto* focusShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(focusShortcut, &QShortcut::activated, this, [this]() {
        if (m_tabWidget->currentIndex() == 0) {
            if (m_fileSearchWidget) m_fileSearchWidget->focusSearchInput();
        } else if (m_tabWidget->currentIndex() == 1) {
            if (m_keywordSearchWidget) m_keywordSearchWidget->focusSearchInput();
        }
    });

    splitter->setStretchFactor(0, 0); // 左
    splitter->setStretchFactor(1, 1); // 中
    splitter->setStretchFactor(2, 0); // 右
}

void SearchAppWindow::onSidebarItemClicked(QListWidgetItem* item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    
    if (m_tabWidget->currentIndex() == 0) {
        m_fileSearchWidget->setSearchPath(path);
    } else {
        m_keywordSearchWidget->setSearchPath(path);
    }
}

void SearchAppWindow::showSidebarContextMenu(const QPoint& pos) {
    QListWidgetItem* item = m_folderSidebar->itemAt(pos);
    if (!item) return;
    
    m_folderSidebar->setCurrentItem(item);

    QMenu menu(this);
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setAttribute(Qt::WA_NoSystemBackground);

    bool isPinned = item->data(Qt::UserRole + 1).toBool();
    // 2026-03-12 按照用户要求，统一置顶图标颜色为橙色 (#FF551C)
    QAction* pinAct = menu.addAction(IconHelper::getIcon("pin_vertical", isPinned ? "#FF551C" : "#AAA"), isPinned ? "取消置顶" : "置顶文件夹");
    QAction* removeAct = menu.addAction(IconHelper::getIcon("close", "#E74C3C"), "取消收藏");
    
    QAction* selected = menu.exec(m_folderSidebar->mapToGlobal(pos));
    if (selected == pinAct) {
        bool newPinned = !isPinned;
        item->setData(Qt::UserRole + 1, newPinned);
        // 2026-03-12 按照用户要求，统一置顶图标颜色为橙色 (#FF551C)
        item->setIcon(IconHelper::getIcon("folder", newPinned ? "#FF551C" : "#F1C40F"));
        m_folderSidebar->sortItems(Qt::AscendingOrder);
        saveFolderFavorites();
    } else if (selected == removeAct) {
        delete m_folderSidebar->takeItem(m_folderSidebar->row(item));
        saveFolderFavorites();
    }
}

void SearchAppWindow::addFolderFavorite(const QString& path, bool pinned) {
    addFolderFavoriteBatch({path});
}

void SearchAppWindow::addFolderFavoriteBatch(const QStringList& paths, bool pinned) {
    // [USER_REQUEST] 核心修复：通过暂时阻塞信号，防止批量添加过程中触发“单击/选中”关联的扫描流程
    m_folderSidebar->blockSignals(true);
    
    bool changed = false;
    for (const QString& path : paths) {
        bool exists = false;
        for (int i = 0; i < m_folderSidebar->count(); ++i) {
            if (m_folderSidebar->item(i)->data(Qt::UserRole).toString() == path) {
                exists = true;
                break;
            }
        }
        
        if (!exists && QDir(path).exists()) {
            QFileInfo fi(path);
            // 2026-03-12 按照用户要求，统一置顶图标颜色为橙色
            auto* item = new FavoriteItem(IconHelper::getIcon("folder", pinned ? "#FF551C" : "#F1C40F"), fi.fileName());
            item->setData(Qt::UserRole, path);
            item->setData(Qt::UserRole + 1, pinned); 
            
            m_folderSidebar->addItem(item);
            changed = true;
        }
    }

    if (changed) {
        m_folderSidebar->sortItems(Qt::AscendingOrder);
        saveFolderFavorites();
    }
    
    // 恢复信号
    m_folderSidebar->blockSignals(false);
}

void SearchAppWindow::addFileFavorite(const QStringList& paths) {
    QSettings settings("SearchTool_Standalone", "GlobalFileFavorites");
    QStringList favs = settings.value("list").toStringList();
    bool changed = false;
    for (const QString& path : paths) {
        if (!path.isEmpty() && !favs.contains(path)) {
            favs.prepend(path);
            changed = true;
        }
    }
    if (changed) {
        settings.setValue("list", favs);
        loadFileFavorites();
    }
}

void SearchAppWindow::onFileFavoriteItemDoubleClicked(QListWidgetItem* item) {
    QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SearchAppWindow::showFileFavoriteContextMenu(const QPoint& pos) {
    auto selectedItems = m_fileFavoritesList->selectedItems();
    if (selectedItems.isEmpty()) {
        auto* item = m_fileFavoritesList->itemAt(pos);
        if (item) {
            item->setSelected(true);
            selectedItems << item;
        }
    }
    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }

    if (paths.isEmpty()) return;

    QMenu menu(this);
    menu.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setAttribute(Qt::WA_NoSystemBackground);
    
    if (selectedItems.size() == 1) {
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

    QString removeText = selectedItems.size() > 1 ? QString("取消收藏 (%1)").arg(selectedItems.size()) : "取消收藏";
    menu.addAction(IconHelper::getIcon("close", "#E74C3C", 18), removeText, this, SLOT(removeFileFavorite()));
    
    menu.addSeparator();
    
    QString copyPathText = selectedItems.size() > 1 ? "复制选中路径" : "复制完整路径";
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71", 18), copyPathText, [paths](){
        QApplication::clipboard()->setText(paths.join("\n"));
    });

    // [USER_REQUEST] 新增“复制文件名”选项，保持视觉一致
    QString copyNameText = selectedItems.size() > 1 ? "复制选中文件名" : "复制文件名";
    menu.addAction(IconHelper::getIcon("file_export", "#2ECC71", 18), copyNameText, [paths](){
        QStringList names;
        for (const auto& p : paths) names << QFileInfo(p).fileName();
        QApplication::clipboard()->setText(names.join("\n"));
    });

    QString copyFileText = selectedItems.size() > 1 ? "复制选中文件" : "复制文件";
    menu.addAction(IconHelper::getIcon("file", "#4A90E2", 18), copyFileText, [this](){ copySelectedFiles(); });

    menu.addAction(IconHelper::getIcon("merge", "#3498DB", 18), "合并选中内容", [this](){ onMergeSelectedFiles(); });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("cut", "#E67E22", 18), "剪切", [this](){ onCutFile(); });
    menu.addAction(IconHelper::getIcon("trash", "#E74C3C", 18), "删除", [this](){ onDeleteFile(); });

    menu.exec(m_fileFavoritesList->mapToGlobal(pos));
}

void SearchAppWindow::onEditFile() {
    auto selectedItems = m_fileFavoritesList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) paths << p;
    }
    if (paths.isEmpty()) return;

    QSettings settings("SearchTool_Standalone", "ExternalEditor");
    QString editorPath = settings.value("EditorPath").toString();

    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        QStringList commonPaths = {
            "C:/Program Files/Notepad++/notepad++.exe",
            "C:/Program Files (x86)/Notepad++/notepad++.exe"
        };
        for (const QString& p : commonPaths) {
            if (QFile::exists(p)) {
                editorPath = p;
                break;
            }
        }
    }

    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        editorPath = QFileDialog::getOpenFileName(this, "选择编辑器", "C:/Program Files", "Executable (*.exe)");
        if (editorPath.isEmpty()) return;
        settings.setValue("EditorPath", editorPath);
    }

    for (const QString& filePath : paths) {
        QProcess::startDetached(editorPath, { QDir::toNativeSeparators(filePath) });
    }
}

void SearchAppWindow::copySelectedFiles() {
    auto selectedItems = m_fileFavoritesList->selectedItems();
    if (selectedItems.isEmpty()) return;

    // 2026-03-20 按照用户要求：复制文件也属于数据导出，必须验证
    if (!verifyExportPermission()) return;

    QList<QUrl> urls;
    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty()) {
            urls << QUrl::fromLocalFile(p);
            paths << p;
        }
    }
    if (urls.isEmpty()) return;

    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);
    mimeData->setText(paths.join("\n"));
    QApplication::clipboard()->setMimeData(mimeData);
    
    // 2026-03-13 按照用户要求：提示时长缩短为 700ms
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已复制到剪贴板", 700);
}

void SearchAppWindow::onCutFile() {
    auto selectedItems = m_fileFavoritesList->selectedItems();
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
    QByteArray data;
    data.resize(4);
    data[0] = 2; // DROPEFFECT_MOVE
    data[1] = 0;
    data[2] = 0;
    data[3] = 0;
    mimeData->setData("Preferred DropEffect", data);
#endif
    QApplication::clipboard()->setMimeData(mimeData);
    // 2026-03-13 按照用户要求：提示时长缩短为 700ms
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已剪切到剪贴板", 700);
}

void SearchAppWindow::onDeleteFile() {
    auto selectedItems = m_fileFavoritesList->selectedItems();
    if (selectedItems.isEmpty()) return;

    int successCount = 0;
    for (auto* item : std::as_const(selectedItems)) {
        QString filePath = item->data(Qt::UserRole).toString();
        if (filePath.isEmpty()) continue;

        if (QFile::moveToTrash(filePath)) {
            successCount++;
            // 从界面移除
            delete item; 
        }
    }

    if (successCount > 0) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已删除");
        // 更新持久化列表
        saveFileFavorites();
    }
}

#include "PasswordVerifyDialog.h"

bool SearchAppWindow::verifyExportPermission() {
    // 2026-03-20 按照用户要求，所有导出操作必须验证身份
    return PasswordVerifyDialog::verify();
}

void SearchAppWindow::onMergeFiles(const QStringList& filePaths, const QString& rootPath) {
    if (filePaths.isEmpty()) return;
    
    if (!verifyExportPermission()) return;

    QString actualRoot = rootPath;
    if (actualRoot.isEmpty()) {
        actualRoot = QFileInfo(filePaths.first()).absolutePath();
    }

    QString targetDir = actualRoot;
    QDir root(actualRoot);
    if (!root.exists("Combine")) {
        root.mkdir("Combine");
    }
    targetDir = root.absoluteFilePath("Combine");

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outName = QString("%1_favorite_export.md").arg(ts);
    QString outPath = QDir(targetDir).filePath(outName);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&outFile);
    out.setEncoding(QStringConverter::Utf8);
    out << "# 收藏文件导出结果 - " << ts << "\n\n";

    for (const QString& fp : filePaths) {
        QString relPath = rootPath.isEmpty() ? fp : QDir(rootPath).relativeFilePath(fp);
        QString lang = getFileLanguage(fp);
        out << "## 文件: `" << relPath << "`\n\n";
        out << "```" << lang << "\n";
        QFile inFile(fp);
        if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            out << inFile.readAll();
        }
        out << "\n```\n\n";
    }
    outFile.close();
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已保存: " + outName);
}

void SearchAppWindow::onMergeSelectedFiles() {
    auto selectedItems = m_fileFavoritesList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty() && isSupportedFile(p)) {
            paths << p;
        }
    }
    if (paths.isEmpty()) return;

    onMergeFiles(paths, "");
}

void SearchAppWindow::removeFileFavorite() {
    auto items = m_fileFavoritesList->selectedItems();
    if (items.isEmpty()) return;
    QSettings settings("SearchTool_Standalone", "GlobalFileFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (auto* item : items) {
        QString path = item->data(Qt::UserRole).toString();
        favs.removeAll(path);
        delete item;
    }
    settings.setValue("list", favs);
}

void SearchAppWindow::loadFolderFavorites() {
    QSettings settings("SearchTool_Standalone", "GlobalFolderFavorites");
    QVariantList favs = settings.value("list").toList();
    for (const auto& fav : favs) {
        QVariantMap map = fav.toMap();
        addFolderFavorite(map["path"].toString(), map["pinned"].toBool());
    }
}

void SearchAppWindow::saveFolderFavorites() {
    QVariantList favs;
    for (int i = 0; i < m_folderSidebar->count(); ++i) {
        QVariantMap map;
        map["path"] = m_folderSidebar->item(i)->data(Qt::UserRole).toString();
        map["pinned"] = m_folderSidebar->item(i)->data(Qt::UserRole + 1).toBool();
        favs << map;
    }
    QSettings settings("SearchTool_Standalone", "GlobalFolderFavorites");
    settings.setValue("list", favs);
}

void SearchAppWindow::loadFileFavorites() {
    m_fileFavoritesList->clear();
    QSettings settings("SearchTool_Standalone", "GlobalFileFavorites");
    QStringList favs = settings.value("list").toStringList();
    for (const QString& path : favs) {
        QFileInfo fi(path);
        auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#4A90E2"), fi.fileName());
        item->setData(Qt::UserRole, path);
        
        m_fileFavoritesList->addItem(item);
    }
}

void SearchAppWindow::saveFileFavorites() {
    QStringList favs;
    for (int i = 0; i < m_fileFavoritesList->count(); ++i) {
        favs << m_fileFavoritesList->item(i)->data(Qt::UserRole).toString();
    }
    QSettings settings("SearchTool_Standalone", "GlobalFileFavorites");
    settings.setValue("list", favs);
}

#include "SearchAppWindow.moc"

void SearchAppWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}
void SearchAppWindow::switchToFileSearch() { m_tabWidget->setCurrentIndex(0); }
void SearchAppWindow::switchToKeywordSearch() { m_tabWidget->setCurrentIndex(1); }
