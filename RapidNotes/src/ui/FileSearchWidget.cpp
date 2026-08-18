#include "FileSearchWidget.h"
#include "FileSearchHistoryPopup.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDirIterator>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QClipboard>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QDir>
#include <QFile>
#include "ToolTipOverlay.h"
#include <QSettings>
#include <QSplitter>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QDrag>
#include <QPixmap>
#include <functional>
#include <utility>
#include <QSet>
#include <QDateTime>

// ----------------------------------------------------------------------------
// 合并逻辑相关常量与辅助函数
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
// FileResultListWidget 辅助类
// ----------------------------------------------------------------------------
class FileResultListWidget : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    void startDrag(Qt::DropActions supportedActions) override {
        QList<QListWidgetItem*> items = selectedItems();
        if (items.isEmpty()) return;

        QMimeData* data = mimeData(items);
        if (!data) return;

        QDrag* drag = new QDrag(this);
        drag->setMimeData(data);
        
        // 设置一个透明的 1x1 像素图片来隐藏拖拽快照
        QPixmap pixmap(1, 1);
        pixmap.fill(Qt::transparent);
        drag->setPixmap(pixmap);
        
        drag->exec(supportedActions);
    }

    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
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
        return mime;
    }
};

// ----------------------------------------------------------------------------
// ScannerThread 实现
// ----------------------------------------------------------------------------
ScannerThread::ScannerThread(const QString& folderPath, QObject* parent)
    : QThread(parent), m_folderPath(folderPath) {}

void ScannerThread::stop() {
    m_isRunning = false;
    wait();
}

void ScannerThread::run() {
    int count = 0;
    if (m_folderPath.isEmpty() || !QDir(m_folderPath).exists()) {
        emit finished(0);
        return;
    }

    QStringList ignored = {".git", ".idea", "__pycache__", "node_modules", "$RECYCLE.BIN", "System Volume Information"};
    
    std::function<void(const QString&)> scanDir = [&](const QString& currentPath) {
        if (!m_isRunning) return;

        QDir dir(currentPath);
        QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& fi : std::as_const(files)) {
            if (!m_isRunning) return;
            bool hidden = fi.isHidden();
            if (!hidden && fi.fileName().startsWith('.')) hidden = true;
            
            emit fileFound(fi.fileName(), fi.absoluteFilePath(), hidden);
            count++;
        }

        QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const auto& di : std::as_const(subDirs)) {
            if (!m_isRunning) return;
            if (!ignored.contains(di.fileName())) {
                scanDir(di.absoluteFilePath());
            }
        }
    };

    scanDir(m_folderPath);
    emit finished(count);
}


// ----------------------------------------------------------------------------
// FileSearchWidget 实现
// ----------------------------------------------------------------------------
FileSearchWidget::FileSearchWidget(QWidget* parent) : QWidget(parent) {
    setupStyles();
    initUI();
}

FileSearchWidget::~FileSearchWidget() {
    if (m_scanThread) {
        m_scanThread->stop();
        m_scanThread->deleteLater();
    }
}

void FileSearchWidget::setupStyles() {
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
        #ActionBtn {
            background-color: #007ACC;
            color: white;
            border: none;
            border-radius: 6px;
            font-weight: bold;
        }
        #ActionBtn:hover {
            background-color: #0062A3;
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

void FileSearchWidget::initUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);
    mainLayout->setSpacing(0);

    // --- 主区域 (搜索功能) ---
    auto* centerWidget = new QWidget();
    auto* layout = new QVBoxLayout(centerWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    auto* pathLayout = new QHBoxLayout();
    m_pathInput = new QLineEdit();
    m_pathInput->setPlaceholderText("双击查看历史，或在此粘贴路径...");
    m_pathInput->setClearButtonEnabled(true);
    m_pathInput->installEventFilter(this);
    connect(m_pathInput, &QLineEdit::returnPressed, this, &FileSearchWidget::onPathReturnPressed);
    
    auto* btnScan = new QToolButton();
    btnScan->setIcon(IconHelper::getIcon("scan", "#1abc9c", 18));
//     btnScan->setToolTip(StringUtils::wrapToolTip("开始扫描"));
    btnScan->setFixedSize(38, 38);
    btnScan->setCursor(Qt::PointingHandCursor);
    btnScan->setStyleSheet("QToolButton { border: 1px solid #444; background: #2D2D30; border-radius: 6px; }"
                           "QToolButton:hover { background-color: #3e3e42; border-color: #007ACC; }"); // 2026-03-xx 统一悬停色
    connect(btnScan, &QToolButton::clicked, this, &FileSearchWidget::onPathReturnPressed);

    auto* btnBrowse = new QToolButton();
    btnBrowse->setObjectName("ActionBtn");
    btnBrowse->setIcon(IconHelper::getIcon("folder", "#ffffff", 18));
//     btnBrowse->setToolTip(StringUtils::wrapToolTip("浏览文件夹"));
    btnBrowse->setFixedSize(38, 38);
    btnBrowse->setCursor(Qt::PointingHandCursor);
    connect(btnBrowse, &QToolButton::clicked, this, &FileSearchWidget::selectFolder);

    pathLayout->addWidget(m_pathInput);
    pathLayout->addWidget(btnScan);
    pathLayout->addWidget(btnBrowse);
    layout->addLayout(pathLayout);

    auto* searchLayout = new QHBoxLayout();
    m_searchInput = new QLineEdit();
    m_searchInput->setPlaceholderText("输入文件名过滤...");
    m_searchInput->setClearButtonEnabled(true);
    m_searchInput->installEventFilter(this);
    connect(m_searchInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this](){
        addSearchHistoryEntry(m_searchInput->text().trimmed());
    });

    m_extInput = new QLineEdit();
    m_extInput->setPlaceholderText("后缀 (如 py)");
    m_extInput->setClearButtonEnabled(true);
    m_extInput->setFixedWidth(120);
    m_extInput->installEventFilter(this);
    connect(m_extInput, &QLineEdit::textChanged, this, &FileSearchWidget::refreshList);
    connect(m_extInput, &QLineEdit::returnPressed, this, [this](){
        addExtHistoryEntry(m_extInput->text().trimmed());
    });

    searchLayout->addWidget(m_searchInput);
    searchLayout->addWidget(m_extInput);
    layout->addLayout(searchLayout);

    auto* infoLayout = new QHBoxLayout();
    m_infoLabel = new QLabel("等待操作...");
    m_infoLabel->setStyleSheet("color: #888888; font-size: 12px;");
    
    m_showHiddenCheck = new QCheckBox("显示隐性文件");
    m_showHiddenCheck->setStyleSheet(R"(
        QCheckBox { color: #888; font-size: 12px; spacing: 5px; }
        QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #444; border-radius: 3px; background: #2D2D30; }
        QCheckBox::indicator:checked { background-color: #007ACC; border-color: #007ACC; }
        QCheckBox::indicator:hover { border-color: #666; }
    )");
    connect(m_showHiddenCheck, &QCheckBox::toggled, this, &FileSearchWidget::refreshList);

    infoLayout->addWidget(m_infoLabel);
    infoLayout->addWidget(m_showHiddenCheck);
    infoLayout->addStretch();
    layout->addLayout(infoLayout);

    auto* listHeaderLayout = new QHBoxLayout();
    listHeaderLayout->setContentsMargins(0, 0, 0, 0);
    auto* listTitle = new QLabel("搜索结果");
    listTitle->setStyleSheet("color: #888; font-size: 11px; font-weight: bold; border: none; background: transparent;");
    
    auto* btnCopyAll = new QToolButton();
    btnCopyAll->setIcon(IconHelper::getIcon("copy", "#1abc9c", 14));
//     btnCopyAll->setToolTip(StringUtils::wrapToolTip("复制全部搜索结果的路径"));
    btnCopyAll->setFixedSize(20, 20);
    btnCopyAll->setCursor(Qt::PointingHandCursor);
    btnCopyAll->setStyleSheet("QToolButton { border: none; background: transparent; padding: 2px; }"
                               "QToolButton:hover { background-color: #3e3e42; border-radius: 4px; }"); // 2026-03-xx 统一悬停色
    connect(btnCopyAll, &QToolButton::clicked, this, [this](){
        if (m_fileList->count() == 0) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 结果列表为空</b>"), 700);
            return;
        }
        QStringList paths;
        for (int i = 0; i < m_fileList->count(); ++i) {
            QString p = m_fileList->item(i)->data(Qt::UserRole).toString();
            if (!p.isEmpty()) paths << p;
        }
        if (paths.isEmpty()) return;
        QApplication::clipboard()->setText(paths.join("\n"));
        // 2026-03-13 按照用户要求：提示时长缩短为 700ms
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#2ecc71;'>[OK] 已复制全部搜索结果</b>"), 700);
    });

    listHeaderLayout->addWidget(listTitle);
    listHeaderLayout->addStretch();
    listHeaderLayout->addWidget(btnCopyAll);
    layout->addLayout(listHeaderLayout);

    m_fileList = new FileResultListWidget();
    m_fileList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setDragEnabled(true);
    m_fileList->setDragDropMode(QAbstractItemView::DragOnly);
    m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &FileSearchWidget::showFileContextMenu);
    
    auto* actionSelectAll = new QAction(this);
    actionSelectAll->setShortcut(QKeySequence("Ctrl+A"));
    actionSelectAll->setShortcutContext(Qt::WidgetShortcut);
    connect(actionSelectAll, &QAction::triggered, [this](){ m_fileList->selectAll(); });
    m_fileList->addAction(actionSelectAll);

    auto* actionCopy = new QAction(this);
    actionCopy->setShortcut(QKeySequence("Ctrl+C"));
    actionCopy->setShortcutContext(Qt::WidgetShortcut);
    connect(actionCopy, &QAction::triggered, this, [this](){ copySelectedFiles(); });
    m_fileList->addAction(actionCopy);

    auto* actionDelete = new QAction(this);
    actionDelete->setShortcut(QKeySequence(Qt::Key_Delete));
    connect(actionDelete, &QAction::triggered, this, [this](){ onDeleteFile(); });
    m_fileList->addAction(actionDelete);

    layout->addWidget(m_fileList);

    mainLayout->addWidget(centerWidget);
}

void FileSearchWidget::selectFolder() {
    QString d = QFileDialog::getExistingDirectory(this, "选择文件夹");
    if (!d.isEmpty()) {
        m_pathInput->setText(d);
        startScan(d);
    }
}

void FileSearchWidget::onPathReturnPressed() {
    QString p = m_pathInput->text().trimmed();
    if (QDir(p).exists()) {
        startScan(p);
    } else {
        m_infoLabel->setText("路径不存在");
        m_pathInput->setStyleSheet("border: 1px solid #FF3333;");
    }
}

void FileSearchWidget::startScan(const QString& path) {
    m_pathInput->setStyleSheet("");
    
    // 开始扫描时自动保存搜索词和后缀的历史
    QString searchTxt = m_searchInput->text().trimmed();
    if (!searchTxt.isEmpty()) addSearchHistoryEntry(searchTxt);
    
    QString extTxt = m_extInput->text().trimmed();
    if (!extTxt.isEmpty()) addExtHistoryEntry(extTxt);

    if (m_scanThread) {
        m_scanThread->stop();
        m_scanThread->deleteLater();
    }

    m_fileList->clear();
    m_filesData.clear();
    m_visibleCount = 0;
    m_hiddenCount = 0;
    m_infoLabel->setText("正在扫描: " + path);

    m_scanThread = new ScannerThread(path, this);
    connect(m_scanThread, &ScannerThread::fileFound, this, &FileSearchWidget::onFileFound);
    connect(m_scanThread, &ScannerThread::finished, this, &FileSearchWidget::onScanFinished);
    m_scanThread->start();
}

void FileSearchWidget::onFileFound(const QString& name, const QString& path, bool isHidden) {
    m_filesData.append({name, path, isHidden});
    if (isHidden) m_hiddenCount++;
    else m_visibleCount++;

    if (m_filesData.size() % 300 == 0) {
        m_infoLabel->setText(QString("已发现 %1 个文件 (可见:%2 隐性:%3)...").arg(m_filesData.size()).arg(m_visibleCount).arg(m_hiddenCount));
    }
}

void FileSearchWidget::onScanFinished(int count) {
    m_infoLabel->setText(QString("扫描结束，共 %1 个文件 (可见:%2 隐性:%3)").arg(count).arg(m_visibleCount).arg(m_hiddenCount));
    addHistoryEntry(m_pathInput->text().trimmed());
    
    std::sort(m_filesData.begin(), m_filesData.end(), [](const FileData& a, const FileData& b){
        return a.name.localeAwareCompare(b.name) < 0;
    });

    refreshList();
}

void FileSearchWidget::refreshList() {
    m_fileList->clear();
    QString fullTxt = m_searchInput->text().toLower();
    QStringList keywords = fullTxt.split(QRegularExpression("[,，]+"), Qt::SkipEmptyParts);
    
    QString ext = m_extInput->text().toLower().trimmed();
    if (ext.startsWith(".")) ext = ext.mid(1);

    bool showHidden = m_showHiddenCheck->isChecked();

    int limit = 500;
    int shown = 0;

    for (const auto& data : std::as_const(m_filesData)) {
        if (!showHidden && data.isHidden) continue;
        if (!ext.isEmpty() && !data.name.toLower().endsWith("." + ext)) continue;
        
        if (!keywords.isEmpty()) {
            bool found = false;
            for (const QString& kw : keywords) {
                if (data.name.toLower().contains(kw.trimmed())) {
                    found = true;
                    break;
                }
            }
            if (!found) continue;
        }

        auto* item = new QListWidgetItem(data.name);
        item->setData(Qt::UserRole, data.path);
        
        m_fileList->addItem(item);
        
        shown++;
        if (shown >= limit) {
            auto* warn = new QListWidgetItem("--- 结果过多，仅显示前 500 条 ---");
            warn->setForeground(QColor(255, 170, 0));
            warn->setTextAlignment(Qt::AlignCenter);
            warn->setFlags(Qt::NoItemFlags);
            m_fileList->addItem(warn);
            break;
        }
    }
}

void FileSearchWidget::showFileContextMenu(const QPoint& pos) {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        auto* item = m_fileList->itemAt(pos);
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

    QString copyPathText = selectedItems.size() > 1 ? "复制选中路径" : "复制完整路径";
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71", 18), copyPathText, [paths](){
        QApplication::clipboard()->setText(paths.join("\n"));
    });

    // [USER_REQUEST] 新增“复制文件名”选项，并统一视觉风格
    QString copyNameText = selectedItems.size() > 1 ? "复制选中文件名" : "复制文件名";
    menu.addAction(IconHelper::getIcon("file_export", "#2ECC71", 18), copyNameText, [paths](){
        QStringList names;
        for (const auto& p : paths) names << QFileInfo(p).fileName();
        QApplication::clipboard()->setText(names.join("\n"));
    });

    QString copyFileText = selectedItems.size() > 1 ? "复制选中文件" : "复制文件";
    menu.addAction(IconHelper::getIcon("file", "#4A90E2", 18), copyFileText, [this](){ copySelectedFiles(); });

    menu.addAction(IconHelper::getIcon("Favorite", "#F1C40F", 18), "收藏文件", this, &FileSearchWidget::onFavoriteFile);

    menu.addAction(IconHelper::getIcon("merge", "#3498DB", 18), "合并选中内容", [this](){ onMergeSelectedFiles(); });

    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("cut", "#E67E22", 18), "剪切", [this](){ onCutFile(); });
    menu.addAction(IconHelper::getIcon("trash", "#E74C3C", 18), "删除", [this](){ onDeleteFile(); });

    menu.exec(m_fileList->mapToGlobal(pos));
}

void FileSearchWidget::onEditFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 请先选择要操作的内容</b>"), 700);
        return;
    }

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
        editorPath = QFileDialog::getOpenFileName(this, "选择编辑器 (推荐 Notepad++)", "C:/Program Files", "Executable (*.exe)");
        if (editorPath.isEmpty()) return;
        settings.setValue("EditorPath", editorPath);
    }

    for (const QString& filePath : paths) {
        QProcess::startDetached(editorPath, { QDir::toNativeSeparators(filePath) });
    }
}

void FileSearchWidget::copySelectedFiles() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 请先选择要操作的内容</b>"), 700);
        return;
    }

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

    QString msg = selectedItems.size() > 1 ? QString("[OK] 已复制 %1 个文件").arg(selectedItems.size()) : "[OK] 已复制到剪贴板";
    ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)));
}

void FileSearchWidget::onCutFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 请先选择要操作的内容</b>"), 700);
        return;
    }

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

    QString msg = selectedItems.size() > 1 ? QString("[OK] 已剪切 %1 个文件").arg(selectedItems.size()) : "[OK] 已剪切到剪贴板";
    ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)));
}

void FileSearchWidget::onDeleteFile() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 请先选择要操作的内容</b>"), 700);
        return;
    }

    int successCount = 0;
    for (auto* item : std::as_const(selectedItems)) {
        QString filePath = item->data(Qt::UserRole).toString();
        if (filePath.isEmpty()) continue;

        if (QFile::moveToTrash(filePath)) {
            successCount++;
            for (int i = 0; i < m_filesData.size(); ++i) {
                if (m_filesData[i].path == filePath) {
                    m_filesData.removeAt(i);
                    break;
                }
            }
            delete item; 
        }
    }

    if (successCount > 0) {
        QString msg = selectedItems.size() > 1 ? QString("[OK] %1 个文件已删除").arg(successCount) : "[OK] 文件已删除";
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)));
        m_infoLabel->setText(msg);
    } else if (!selectedItems.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color: #e74c3c;'>[ERR] 无法删除文件，请检查是否被占用</b>"));
    }
}

void FileSearchWidget::clearAllInputs() {
    m_pathInput->clear();
    m_searchInput->clear();
    m_extInput->clear();
}

void FileSearchWidget::focusSearchInput() {
    // [USER_REQUEST] 当按下 Ctrl+F 时，定位到文件名过滤输入框
    if (m_searchInput) {
        m_searchInput->setFocus();
        m_searchInput->selectAll();
    }
}

#include "PasswordVerifyDialog.h"

bool FileSearchWidget::verifyExportPermission() {
    // 2026-03-20 按照用户要求，所有导出操作必须验证身份
    return PasswordVerifyDialog::verify();
}

void FileSearchWidget::onMergeFiles(const QStringList& filePaths, const QString& rootPath) {
    if (filePaths.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 没有可合并的文件</b>"), 700);
        return;
    }

    // 判断文件是否来自不同文件夹
    bool differentFolders = false;
    if (filePaths.size() > 1) {
        QString firstDir = QFileInfo(filePaths.first()).absolutePath();
        for (const QString& fp : filePaths) {
            if (QFileInfo(fp).absolutePath() != firstDir) {
                differentFolders = true;
                break;
            }
        }
    }

    QString actualRoot = rootPath;
    if (actualRoot.isEmpty() && !filePaths.isEmpty()) {
        actualRoot = QFileInfo(filePaths.first()).absolutePath();
    }

    QString targetDir = actualRoot;
    if (differentFolders && !actualRoot.isEmpty()) {
        QDir root(actualRoot);
        if (!root.exists("Combine")) {
            root.mkdir("Combine");
        }
        targetDir = root.absoluteFilePath("Combine");
    }

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outName = QString("%1_code_export.md").arg(ts);
    QString outPath = QDir(targetDir).filePath(outName);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 无法创建输出文件</b>"), 700);
        return;
    }

    QTextStream out(&outFile);
    out.setEncoding(QStringConverter::Utf8);

    out << "# 代码导出结果 - " << ts << "\n\n";
    out << "**项目路径**: `" << rootPath << "`\n\n";
    out << "**文件总数**: " << filePaths.size() << "\n\n";

    for (const QString& fp : filePaths) {
        QString relPath = QDir(rootPath).relativeFilePath(fp);
        QString lang = getFileLanguage(fp);

        out << "## 文件: `" << relPath << "`\n\n";
        out << "```" << lang << "\n";

        QFile inFile(fp);
        if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray content = inFile.readAll();
            out << QString::fromUtf8(content);
            if (!content.endsWith('\n')) out << "\n";
        } else {
            out << "# 读取文件失败\n";
        }
        out << "```\n\n";
    }

    outFile.close();
    
    QString msg = QString("[OK] 已保存: %1 (%2个文件)").arg(outName).arg(filePaths.size());
    ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip(QString("<b style='color: #2ecc71;'>%1</b>").arg(msg)), 700);
}

void FileSearchWidget::onMergeSelectedFiles() {
    auto selectedItems = m_fileList->selectedItems();
    if (selectedItems.isEmpty()) return;

    QStringList paths;
    for (auto* item : std::as_const(selectedItems)) {
        QString p = item->data(Qt::UserRole).toString();
        if (!p.isEmpty() && isSupportedFile(p)) {
            paths << p;
        }
    }
    
    if (paths.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 选中项中没有支持的文件类型</b>"), 700);
        return;
    }

    onMergeFiles(paths, m_pathInput->text().trimmed());
}

void FileSearchWidget::onMergeFolderContent() {
    QString rootPath = m_pathInput->text().trimmed();
    if (rootPath.isEmpty() || !QDir(rootPath).exists()) return;

    QStringList paths;
    for (const auto& data : std::as_const(m_filesData)) {
        if (!data.isHidden && isSupportedFile(data.path)) {
            paths << data.path;
        }
    }

    if (paths.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), StringUtils::wrapToolTip("<b style='color:#e74c3c;'>[ERR] 目录中没有支持的文件类型</b>"), 700);
        return;
    }

    onMergeFiles(paths, rootPath);
}

void FileSearchWidget::addHistoryEntry(const QString& path) {
    if (path.isEmpty()) return;
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    QStringList history = settings.value("pathList").toStringList();
    history.removeAll(path);
    history.prepend(path);
    while (history.size() > 10) history.removeLast();
    settings.setValue("pathList", history);
}

QStringList FileSearchWidget::getHistory() const {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    return settings.value("pathList").toStringList();
}

void FileSearchWidget::clearHistory() {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    settings.setValue("pathList", QStringList());
}

void FileSearchWidget::removeHistoryEntry(const QString& path) {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    QStringList history = settings.value("pathList").toStringList();
    history.removeAll(path);
    settings.setValue("pathList", history);
}

void FileSearchWidget::useHistoryPath(const QString& path) {
    m_pathInput->setText(path);
    onPathReturnPressed();
}

void FileSearchWidget::setSearchPath(const QString& path) {
    m_pathInput->setText(path);
    startScan(path);
}

QString FileSearchWidget::currentPath() const {
    return m_pathInput->text().trimmed();
}

void FileSearchWidget::addSearchHistoryEntry(const QString& text) {
    if (text.isEmpty()) return;
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    QStringList history = settings.value("filenameList").toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 10) history.removeLast();
    settings.setValue("filenameList", history);
}

QStringList FileSearchWidget::getSearchHistory() const {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    return settings.value("filenameList").toStringList();
}

void FileSearchWidget::removeSearchHistoryEntry(const QString& text) {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    QStringList history = settings.value("filenameList").toStringList();
    history.removeAll(text);
    settings.setValue("filenameList", history);
}

void FileSearchWidget::clearSearchHistory() {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    settings.setValue("filenameList", QStringList());
}

void FileSearchWidget::addExtHistoryEntry(const QString& text) {
    if (text.isEmpty()) return;
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    QStringList history = settings.value("extensionList").toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 10) history.removeLast();
    settings.setValue("extensionList", history);
}

QStringList FileSearchWidget::getExtHistory() const {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    return settings.value("extensionList").toStringList();
}

void FileSearchWidget::removeExtHistoryEntry(const QString& text) {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    QStringList history = settings.value("extensionList").toStringList();
    history.removeAll(text);
    settings.setValue("extensionList", history);
}

void FileSearchWidget::clearExtHistory() {
    QSettings settings("SearchTool_Standalone", "FileSearchHistory");
    settings.setValue("extensionList", QStringList());
}

void FileSearchWidget::onFavoriteFile() {
    auto items = m_fileList->selectedItems();
    if (items.isEmpty()) return;
    QStringList paths;
    for (auto* item : items) {
        QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) paths << path;
    }
    emit requestAddFileFavorite(paths);
}

void FileSearchWidget::saveFileFavorites() {}
void FileSearchWidget::refreshFileFavoritesList(const QString&) {}

bool FileSearchWidget::eventFilter(QObject* watched, QEvent* event) {
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
            if (watched == m_pathInput || watched == m_searchInput || watched == m_extInput) {
                // [MODIFIED] 两段式逻辑：不为空则清空，否则清除焦点
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

    if (event->type() == QEvent::MouseButtonDblClick) {
        if (watched == m_pathInput) {
            auto* popup = new FileSearchHistoryPopup(this, m_pathInput, FileSearchHistoryPopup::Path);
            popup->showAnimated();
            return true;
        } else if (watched == m_searchInput) {
            auto* popup = new FileSearchHistoryPopup(this, m_searchInput, FileSearchHistoryPopup::Filename);
            popup->showAnimated();
            return true;
        } else if (watched == m_extInput) {
            auto* popup = new FileSearchHistoryPopup(this, m_extInput, FileSearchHistoryPopup::Extension);
            popup->showAnimated();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}