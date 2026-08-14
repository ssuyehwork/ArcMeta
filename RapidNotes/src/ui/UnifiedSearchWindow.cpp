#include "UnifiedSearchWindow.h"
#include "FileSearchWidget.h"
#include "KeywordSearchWidget.h"
#include "ToolTipOverlay.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "../core/ShortcutManager.h"

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
#include <QSettings>
#include <QSplitter>
#include <QMenu>
#include <QAction>
#include <QToolButton>
#include <QButtonGroup>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QDateTime>
#include <QKeyEvent>
#include <functional>
#include <utility>

// ----------------------------------------------------------------------------
// 共享常量与辅助函数 (复刻自原有实现)
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
// UnifiedScannerThread 实现
// ----------------------------------------------------------------------------
UnifiedScannerThread::UnifiedScannerThread(const QString& folderPath, QObject* parent)
    : QThread(parent), m_folderPath(folderPath) {}

void UnifiedScannerThread::stop() {
    m_isRunning = false;
    wait();
}

void UnifiedScannerThread::run() {
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
// Sidebar 实现 (左侧文件夹，右侧文件)
// ----------------------------------------------------------------------------
FolderSidebarListWidget::FolderSidebarListWidget(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
}
void FolderSidebarListWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) event->acceptProposedAction();
}
void FolderSidebarListWidget::dragMoveEvent(QDragMoveEvent* event) { event->acceptProposedAction(); }
void FolderSidebarListWidget::dropEvent(QDropEvent* event) {
    QString path;
    if (event->mimeData()->hasUrls()) path = event->mimeData()->urls().at(0).toLocalFile();
    else if (event->mimeData()->hasText()) path = event->mimeData()->text();
    if (!path.isEmpty() && QDir(path).exists()) {
        emit folderDropped(path);
        event->acceptProposedAction();
    }
}

FileCollectionSidebarWidget::FileCollectionSidebarWidget(QWidget* parent) : QListWidget(parent) {
    setAcceptDrops(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}
void FileCollectionSidebarWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) event->acceptProposedAction();
}
void FileCollectionSidebarWidget::dragMoveEvent(QDragMoveEvent* event) { event->acceptProposedAction(); }
void FileCollectionSidebarWidget::dropEvent(QDropEvent* event) {
    QStringList paths;
    if (event->mimeData()->hasUrls()) {
        for (const QUrl& url : event->mimeData()->urls()) {
            QString p = url.toLocalFile();
            if (!p.isEmpty() && QFileInfo(p).isFile()) paths << p;
        }
    } else if (event->mimeData()->hasText()) {
        QStringList texts = event->mimeData()->text().split("\n", Qt::SkipEmptyParts);
        for (const QString& t : texts) {
            QString p = t.trimmed();
            if (!p.isEmpty() && QFileInfo(p).isFile()) paths << p;
        }
    }
    if (!paths.isEmpty()) {
        emit filesDropped(paths);
        event->acceptProposedAction();
    } else if (event->source() && event->source() != this) {
        QListWidget* sourceList = qobject_cast<QListWidget*>(event->source());
        if (sourceList) {
            QStringList sourcePaths;
            for (auto* item : sourceList->selectedItems()) {
                QString p = item->data(Qt::UserRole).toString();
                if (!p.isEmpty()) sourcePaths << p;
            }
            if (!sourcePaths.isEmpty()) { emit filesDropped(sourcePaths); event->acceptProposedAction(); }
        }
    }
}

// ----------------------------------------------------------------------------
// UnifiedFileSearchHistoryPopup 实现 (复刻自 FileSearchWindow.cpp)
// ----------------------------------------------------------------------------
class UnifiedFileSearchHistoryChip : public QFrame {
    Q_OBJECT
public:
    UnifiedFileSearchHistoryChip(const QString& text, QWidget* parent = nullptr) : QFrame(parent), m_text(text) {
        setAttribute(Qt::WA_StyledBackground); setCursor(Qt::PointingHandCursor); setObjectName("PathChip");
        auto* layout = new QHBoxLayout(this); layout->setContentsMargins(10, 6, 10, 6); layout->setSpacing(10);
        auto* lbl = new QLabel(text); lbl->setStyleSheet("border: none; background: transparent; color: #DDD; font-size: 13px;");
        layout->addWidget(lbl); layout->addStretch();
        auto* btnDel = new QPushButton(); btnDel->setIcon(IconHelper::getIcon("close", "#666", 16)); btnDel->setIconSize(QSize(10, 10)); btnDel->setFixedSize(16, 16);
        btnDel->setStyleSheet("QPushButton { background-color: transparent; border-radius: 4px; padding: 0px; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
        connect(btnDel, &QPushButton::clicked, this, [this](){ emit deleted(m_text); });
        layout->addWidget(btnDel);
        setStyleSheet("#PathChip { background-color: transparent; border: none; border-radius: 4px; } #PathChip:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
    }
    void mousePressEvent(QMouseEvent* e) override { if(e->button() == Qt::LeftButton) emit clicked(m_text); QFrame::mousePressEvent(e); }
signals:
    void clicked(const QString& text); void deleted(const QString& text);
private:
    QString m_text;
};

class UnifiedFileSearchHistoryPopup : public QWidget {
    Q_OBJECT
public:
    enum Type { Path, Filename };
    explicit UnifiedFileSearchHistoryPopup(UnifiedSearchWindow* parentWindow, QLineEdit* edit, Type type) 
        : QWidget(parentWindow->window(), Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) 
    {
        m_parent = parentWindow; m_edit = edit; m_type = type;
        setAttribute(Qt::WA_TranslucentBackground);
        auto* rootLayout = new QVBoxLayout(this); rootLayout->setContentsMargins(12, 12, 12, 12);
        auto* container = new QFrame(); container->setObjectName("PopupContainer");
        container->setStyleSheet("#PopupContainer { background-color: #252526; border: 1px solid #444; border-radius: 10px; }");
        rootLayout->addWidget(container);
        auto* layout = new QVBoxLayout(container); layout->setContentsMargins(12, 12, 12, 12); layout->setSpacing(10);
        auto* top = new QHBoxLayout(); auto* icon = new QLabel();
        icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14));
        top->addWidget(icon);
        auto* title = new QLabel(m_type == Path ? "最近扫描路径" : "最近搜索文件名");
        title->setStyleSheet("color: #888; font-weight: bold; font-size: 11px;");
        top->addWidget(title); top->addStretch();
        auto* clearBtn = new QPushButton("清空");
        clearBtn->setStyleSheet("QPushButton { background: transparent; color: #666; border: none; font-size: 11px; } QPushButton:hover { color: #E74C3C; }");
        connect(clearBtn, &QPushButton::clicked, [this](){ m_parent->clearHistory(m_type == Path); refreshUI(); });
        top->addWidget(clearBtn); layout->addLayout(top);
        auto* scroll = new QScrollArea(); scroll->setWidgetResizable(true);
        scroll->setStyleSheet("QScrollArea { background-color: transparent; border: none; }");
        m_chipsWidget = new QWidget(); m_vLayout = new QVBoxLayout(m_chipsWidget);
        m_vLayout->setContentsMargins(0, 0, 0, 0); m_vLayout->setSpacing(2); m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget); layout->addWidget(scroll);
        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity"); m_opacityAnim->setDuration(200);
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
    void refreshUI() {
        QLayoutItem* item; while ((item = m_vLayout->takeAt(0))) { if(item->widget()) item->widget()->deleteLater(); delete item; }
        m_vLayout->addStretch();
        QStringList history = m_parent->getHistory(m_type == Path);
        if(history.isEmpty()) {
            auto* lbl = new QLabel("暂无历史记录"); lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px;");
            m_vLayout->insertWidget(0, lbl);
        } else {
            for(const QString& val : std::as_const(history)) {
                auto* chip = new UnifiedFileSearchHistoryChip(val); chip->setFixedHeight(32);
                connect(chip, &UnifiedFileSearchHistoryChip::clicked, this, [this](const QString& v){ 
                    if (m_type == Path) {
                        auto* fcw = m_parent->findChild<FileSearchContentWidget*>();
                        if(fcw) fcw->setPath(v);
                    } else m_edit->setText(v);
                    close(); 
                });
                connect(chip, &UnifiedFileSearchHistoryChip::deleted, this, [this](const QString& v){ m_parent->removeHistoryEntry(m_type == Path, v); refreshUI(); });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        resize(m_edit->width() + 24, 410);
    }
    void showAnimated() {
        refreshUI(); QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height()));
        move(pos.x() - 12, pos.y() - 7);
        setWindowOpacity(0); show();
        m_opacityAnim->setStartValue(0); m_opacityAnim->setEndValue(1); m_opacityAnim->start();
    }
private:
    UnifiedSearchWindow* m_parent; QLineEdit* m_edit; Type m_type; QWidget* m_chipsWidget; QVBoxLayout* m_vLayout; QPropertyAnimation* m_opacityAnim;
};

// ----------------------------------------------------------------------------
// FileSearchContentWidget 实现
// ----------------------------------------------------------------------------
FileSearchContentWidget::FileSearchContentWidget(QWidget* parent) : QWidget(parent) {
    initUI();
}
FileSearchContentWidget::~FileSearchContentWidget() {
    if (m_scanThread) { m_scanThread->stop(); m_scanThread->deleteLater(); }
}
void FileSearchContentWidget::initUI() {
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    auto* pathLayout = new QHBoxLayout();
    m_pathInput = new QLineEdit(); m_pathInput->setPlaceholderText("双击查看历史，或在此粘贴路径..."); m_pathInput->setClearButtonEnabled(true);
    m_pathInput->installEventFilter(this);
    connect(m_pathInput, &QLineEdit::returnPressed, this, &FileSearchContentWidget::onPathReturnPressed);
    auto* btnScan = new QToolButton(); btnScan->setIcon(IconHelper::getIcon("scan", "#1abc9c", 18)); btnScan->setFixedSize(38, 38);
    btnScan->setCursor(Qt::PointingHandCursor); btnScan->setStyleSheet("QToolButton { border: 1px solid #444; background: #2D2D30; border-radius: 6px; }");
    connect(btnScan, &QToolButton::clicked, this, &FileSearchContentWidget::onPathReturnPressed);
    auto* btnBrowse = new QToolButton(); btnBrowse->setFixedSize(38, 38); btnBrowse->setIcon(IconHelper::getIcon("folder", "#ffffff", 18));
    btnBrowse->setStyleSheet("QToolButton { background-color: #007ACC; border-radius: 6px; }");
    connect(btnBrowse, &QToolButton::clicked, this, &FileSearchContentWidget::selectFolder);
    pathLayout->addWidget(m_pathInput); pathLayout->addWidget(btnScan); pathLayout->addWidget(btnBrowse);
    layout->addLayout(pathLayout);
    auto* searchLayout = new QHBoxLayout();
    m_searchInput = new QLineEdit(); m_searchInput->setPlaceholderText("输入文件名过滤..."); m_searchInput->setClearButtonEnabled(true);
    m_searchInput->installEventFilter(this);
    connect(m_searchInput, &QLineEdit::textChanged, this, &FileSearchContentWidget::refreshList);
    connect(m_searchInput, &QLineEdit::returnPressed, this, [this](){ addSearchHistoryEntry(m_searchInput->text().trimmed()); });
    m_extInput = new QLineEdit(); m_extInput->setPlaceholderText("后缀 (如 py)"); m_extInput->setClearButtonEnabled(true); m_extInput->setFixedWidth(120);
    connect(m_extInput, &QLineEdit::textChanged, this, &FileSearchContentWidget::refreshList);
    searchLayout->addWidget(m_searchInput); searchLayout->addWidget(m_extInput);
    layout->addLayout(searchLayout);
    auto* infoLayout = new QHBoxLayout();
    m_infoLabel = new QLabel("等待操作..."); m_infoLabel->setStyleSheet("color: #888; font-size: 12px;");
    m_showHiddenCheck = new QCheckBox("显示隐性文件");
    m_showHiddenCheck->setStyleSheet("QCheckBox { color: #888; font-size: 12px; }");
    connect(m_showHiddenCheck, &QCheckBox::toggled, this, &FileSearchContentWidget::refreshList);
    infoLayout->addWidget(m_infoLabel); infoLayout->addWidget(m_showHiddenCheck); infoLayout->addStretch();
    layout->addLayout(infoLayout);
    m_fileList = new QListWidget(); m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection); m_fileList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileList->setDragEnabled(true); m_fileList->setDragDropMode(QAbstractItemView::DragOnly);
    connect(m_fileList, &QListWidget::customContextMenuRequested, this, &FileSearchContentWidget::showFileContextMenu);
    layout->addWidget(m_fileList);
    updateShortcuts();
}
void FileSearchContentWidget::setPath(const QString& path) { m_pathInput->setText(path); startScan(path); }
QString FileSearchContentWidget::getPath() const { return m_pathInput->text().trimmed(); }
void FileSearchContentWidget::onPathReturnPressed() { QString p = m_pathInput->text().trimmed(); if (QDir(p).exists()) startScan(p); else m_infoLabel->setText("路径不存在"); }
void FileSearchContentWidget::selectFolder() { QString d = QFileDialog::getExistingDirectory(this, "选择文件夹"); if (!d.isEmpty()) setPath(d); }
void FileSearchContentWidget::startScan(const QString& path) {
    if (m_scanThread) { m_scanThread->stop(); m_scanThread->deleteLater(); }
    m_fileList->clear(); m_filesData.clear(); m_visibleCount = 0; m_hiddenCount = 0;
    m_infoLabel->setText("正在扫描: " + path);
    m_scanThread = new UnifiedScannerThread(path, this);
    connect(m_scanThread, &UnifiedScannerThread::fileFound, this, &FileSearchContentWidget::onFileFound);
    connect(m_scanThread, &UnifiedScannerThread::finished, this, &FileSearchContentWidget::onScanFinished);
    m_scanThread->start();
    emit scanStarted(path);
}
void FileSearchContentWidget::onFileFound(const QString& name, const QString& path, bool isHidden) {
    m_filesData.append({name, path, isHidden});
    if (isHidden) m_hiddenCount++; else m_visibleCount++;
}
void FileSearchContentWidget::onScanFinished(int count) {
    m_infoLabel->setText(QString("扫描结束，共 %1 个文件 (可见:%2 隐性:%3)").arg(count).arg(m_visibleCount).arg(m_hiddenCount));
    std::sort(m_filesData.begin(), m_filesData.end(), [](const FileData& a, const FileData& b){ return a.name.localeAwareCompare(b.name) < 0; });
    refreshList();
}
void FileSearchContentWidget::refreshList() {
    m_fileList->clear(); QString txt = m_searchInput->text().toLower(); QString ext = m_extInput->text().toLower().trimmed();
    if (ext.startsWith(".")) ext = ext.mid(1);
    bool showHidden = m_showHiddenCheck->isChecked();
    int shown = 0;
    for (const auto& data : std::as_const(m_filesData)) {
        if (!showHidden && data.isHidden) continue;
        if (!ext.isEmpty() && !data.name.toLower().endsWith("." + ext)) continue;
        if (!txt.isEmpty() && !data.name.toLower().contains(txt)) continue;
        auto* item = new QListWidgetItem(data.name); item->setData(Qt::UserRole, data.path); 
        m_fileList->addItem(item);
        if (++shown >= 500) {
            auto* warn = new QListWidgetItem("--- 仅显示前 500 条 ---"); warn->setForeground(QColor(255, 170, 0));
            warn->setTextAlignment(Qt::AlignCenter); warn->setFlags(Qt::NoItemFlags); m_fileList->addItem(warn); break;
        }
    }
}
void FileSearchContentWidget::showFileContextMenu(const QPoint& pos) {
    auto selectedItems = m_fileList->selectedItems(); if (selectedItems.isEmpty()) return;
    QStringList paths; for (auto* item : std::as_const(selectedItems)) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) paths << p; }
    if (paths.isEmpty()) return;
    QMenu menu(this); IconHelper::setupMenu(&menu);
    if (selectedItems.size() == 1) {
        QString filePath = paths.first();
        menu.addAction(IconHelper::getIcon("folder", "#F1C40F"), "定位文件夹", [filePath](){ QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath())); });
        menu.addAction(IconHelper::getIcon("search", "#4A90E2"), "定位文件", [filePath](){
#ifdef Q_OS_WIN
            QStringList args; args << "/select," << QDir::toNativeSeparators(filePath); QProcess::startDetached("explorer.exe", args);
#endif
        });
        menu.addAction(IconHelper::getIcon("edit", "#3498DB"), "编辑", [this](){ onEditFile(); });
        menu.addSeparator();
    }
    menu.addAction(IconHelper::getIcon("copy", "#2ECC71"), "复制完整路径", [paths](){ QApplication::clipboard()->setText(paths.join("\n")); });
    menu.addAction(IconHelper::getIcon("file", "#4A90E2"), "复制文件", [this](){ copySelectedFiles(); });
    menu.addAction(IconHelper::getIcon("merge", "#3498DB"), "合并选中内容", [this](){ onMergeSelectedFiles(); });
    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("Favorite", "#F1C40F"), "加入收藏", [this](){
        for (auto* item : m_fileList->selectedItems()) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) emit addFileToCollection(p); }
    });
    menu.addSeparator();
    menu.addAction(IconHelper::getIcon("cut", "#E67E22"), "剪切", [this](){ onCutFile(); });
    menu.addAction(IconHelper::getIcon("trash", "#E74C3C"), "删除", [this](){ onDeleteFile(); });
    menu.exec(m_fileList->mapToGlobal(pos));
}
void FileSearchContentWidget::onEditFile() {
    auto selectedItems = m_fileList->selectedItems(); if (selectedItems.isEmpty()) return;
    QStringList paths; for (auto* item : std::as_const(selectedItems)) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) paths << p; }
    QSettings settings("RapidNotes", "ExternalEditor"); QString editorPath = settings.value("EditorPath").toString();
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        for (const QString& p : {"C:/Program Files/Notepad++/notepad++.exe", "C:/Program Files (x86)/Notepad++/notepad++.exe"}) if (QFile::exists(p)) { editorPath = p; break; }
    }
    if (editorPath.isEmpty() || !QFile::exists(editorPath)) {
        editorPath = QFileDialog::getOpenFileName(this, "选择编辑器", "C:/Program Files", "Executable (*.exe)");
        if (editorPath.isEmpty()) return; settings.setValue("EditorPath", editorPath);
    }
    for (const QString& fp : paths) QProcess::startDetached(editorPath, { QDir::toNativeSeparators(fp) });
}
void FileSearchContentWidget::copySelectedFiles() {
    auto selectedItems = m_fileList->selectedItems(); if (selectedItems.isEmpty()) return;
    
    // 2026-03-20 按照用户要求：复制文件也属于数据导出，必须验证
    auto* uswin = qobject_cast<UnifiedSearchWindow*>(window());
    if (uswin && !uswin->verifyExportPermission()) return;

    QList<QUrl> urls; QStringList paths;
    for (auto* item : std::as_const(selectedItems)) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) { urls << QUrl::fromLocalFile(p); paths << p; } }
    QMimeData* mimeData = new QMimeData(); mimeData->setUrls(urls); mimeData->setText(paths.join("\n"));
    QApplication::clipboard()->setMimeData(mimeData);
    // 2026-04-xx 提升为 ActivePriority 以防被悬浮提示覆盖
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已复制到剪贴板", 700, QColor("#B0B0B0"), ToolTipOverlay::ActivePriority);
}
void FileSearchContentWidget::onDeleteFile() {
    auto selectedItems = m_fileList->selectedItems(); if (selectedItems.isEmpty()) return;
    int successCount = 0;
    for (auto* item : std::as_const(selectedItems)) {
        QString fp = item->data(Qt::UserRole).toString(); if (fp.isEmpty()) continue;
        if (QFile::moveToTrash(fp)) {
            successCount++; for (int i = 0; i < m_filesData.size(); ++i) if (m_filesData[i].path == fp) { m_filesData.removeAt(i); break; }
            delete item;
        }
    }
    if (successCount > 0) ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已删除");
}
void FileSearchContentWidget::onCutFile() {
    auto selectedItems = m_fileList->selectedItems(); if (selectedItems.isEmpty()) return;
    QList<QUrl> urls; for (auto* item : std::as_const(selectedItems)) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty()) urls << QUrl::fromLocalFile(p); }
    if (urls.isEmpty()) return;
    QMimeData* mimeData = new QMimeData(); mimeData->setUrls(urls);
#ifdef Q_OS_WIN
    QByteArray data; data.resize(4); data[0] = 2; data[1] = 0; data[2] = 0; data[3] = 0; mimeData->setData("Preferred DropEffect", data);
#endif
    QApplication::clipboard()->setMimeData(mimeData);
    // 2026-03-13 按照用户要求：提示时长缩短为 700ms
    ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已剪切到剪贴板", 700);
}
void FileSearchContentWidget::onMergeSelectedFiles() {
    QStringList paths; for (auto* item : m_fileList->selectedItems()) { QString p = item->data(Qt::UserRole).toString(); if (!p.isEmpty() && isSupportedFile(p)) paths << p; }
    if (paths.isEmpty()) return;
    QString rootPath = m_pathInput->text().trimmed(); if (!QDir(rootPath).exists()) rootPath = QFileInfo(paths.first()).absolutePath();
    auto* uswin = qobject_cast<UnifiedSearchWindow*>(window()); if(uswin) {
        // 通过内部机制调用 UnifiedSearchWindow 的 onMergeFiles
        QMetaObject::invokeMethod(uswin, "onMergeFiles", Q_ARG(QStringList, paths), Q_ARG(QString, rootPath));
    }
}
void FileSearchContentWidget::updateShortcuts() {
    auto& sm = ShortcutManager::instance();
    auto* actionSelectAll = new QAction(this); actionSelectAll->setShortcut(sm.getShortcut("fs_select_all"));
    connect(actionSelectAll, &QAction::triggered, [this](){ m_fileList->selectAll(); });
    m_fileList->addAction(actionSelectAll);

    auto* actionCopy = new QAction(this); actionCopy->setShortcut(sm.getShortcut("fs_copy"));
    connect(actionCopy, &QAction::triggered, this, [this](){ copySelectedFiles(); });
    m_fileList->addAction(actionCopy);

    auto* actionDelete = new QAction(this); actionDelete->setShortcut(sm.getShortcut("fs_delete"));
    connect(actionDelete, &QAction::triggered, this, [this](){ onDeleteFile(); });
    m_fileList->addAction(actionDelete);
}
void FileSearchContentWidget::addHistoryEntry(const QString& path) {
    auto* uswin = qobject_cast<UnifiedSearchWindow*>(window()); if(uswin) uswin->addHistoryEntry(true, path);
}
void FileSearchContentWidget::addSearchHistoryEntry(const QString& text) {
    auto* uswin = qobject_cast<UnifiedSearchWindow*>(window()); if(uswin) uswin->addSearchHistoryEntry(text);
}

// ----------------------------------------------------------------------------
// UnifiedSearchWindow 实现
// ----------------------------------------------------------------------------
UnifiedSearchWindow::UnifiedSearchWindow(QWidget* parent) : FramelessDialog("查找文件与关键字", parent) {
    setObjectName("UnifiedSearchWindow"); resize(1200, 720); setupStyles(); initUI();
    loadFavorites(); loadCollection();
    m_resizeHandle = new ResizeHandle(this, this); m_resizeHandle->raise();
}
UnifiedSearchWindow::~UnifiedSearchWindow() {}

void UnifiedSearchWindow::switchToPage(int index) {
    if (index == 0) m_btnFileSearch->click();
    else if (index == 1) m_btnKeywordSearch->click();
}

void UnifiedSearchWindow::setupStyles() {
    setStyleSheet(R"(
        QWidget { font-family: "Microsoft YaHei"; font-size: 14px; color: #E0E0E0; }
        QSplitter::handle { background-color: #333; }
        QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 6px; }
        QListWidget::item { min-height: 24px; padding-left: 8px; border-radius: 4px; color: #CCC; }
        QListWidget::item:selected { background-color: #3e3e42; border-left: 3px solid #007ACC; color: #FFF; } // 2026-03-xx 统一选中色
        QLineEdit { background-color: #333; border: 1px solid #444; color: #FFF; border-radius: 6px; padding: 6px; }
        QLineEdit:focus { border: 1px solid #007ACC; }
        #TabBtn { background-color: #2D2D30; border: 1px solid #444; color: #888; border-radius: 4px; padding: 6px 15px; font-weight: bold; }
        #TabBtn:checked { background-color: #3e3e42; color: #007ACC; border-color: #007ACC; } // 2026-03-xx 统一选中色
        #TabBtn:hover:!checked { background-color: #3e3e42; color: #AAA; } // 2026-03-xx 统一悬停色
    )");
}

void UnifiedSearchWindow::initUI() {
    auto* mainLayout = new QHBoxLayout(m_contentArea); mainLayout->setContentsMargins(10, 10, 10, 10); mainLayout->setSpacing(0);
    auto* splitter = new QSplitter(Qt::Horizontal); mainLayout->addWidget(splitter);

    // 左侧边栏
    auto* leftSide = new QWidget(); auto* leftLayout = new QVBoxLayout(leftSide); leftLayout->setContentsMargins(0, 0, 10, 0);
    auto* leftHeader = new QLabel("收藏夹 (可拖入)"); leftHeader->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    leftLayout->addWidget(leftHeader);
    m_folderSidebar = new FolderSidebarListWidget(); leftLayout->addWidget(m_folderSidebar);
    auto* btnAddFav = new QPushButton("收藏当前路径"); btnAddFav->setFixedHeight(30);
    connect(btnAddFav, &QPushButton::clicked, this, [this](){ QString p = m_fileSearchWidget->getPath(); if(QDir(p).exists()) addFavorite(p); });
    leftLayout->addWidget(btnAddFav);
    splitter->addWidget(leftSide);

    // 中间区域
    auto* midSide = new QWidget(); auto* midLayout = new QVBoxLayout(midSide); midLayout->setContentsMargins(10, 0, 10, 0);
    auto* tabLayout = new QHBoxLayout(); tabLayout->setSpacing(5);
    m_btnFileSearch = new QPushButton(" 文件查找"); m_btnFileSearch->setObjectName("TabBtn"); m_btnFileSearch->setCheckable(true); m_btnFileSearch->setChecked(true);
    m_btnFileSearch->setIcon(IconHelper::getIcon("folder", "#888", 16));
    m_btnKeywordSearch = new QPushButton(" 关键字查找"); m_btnKeywordSearch->setObjectName("TabBtn"); m_btnKeywordSearch->setCheckable(true);
    m_btnKeywordSearch->setIcon(IconHelper::getIcon("search", "#888", 16));
    auto* tabGroup = new QButtonGroup(this); tabGroup->addButton(m_btnFileSearch, 0); tabGroup->addButton(m_btnKeywordSearch, 1);
    tabLayout->addWidget(m_btnFileSearch); tabLayout->addWidget(m_btnKeywordSearch); tabLayout->addStretch();
    midLayout->addLayout(tabLayout);
    m_stackedWidget = new QStackedWidget();
    m_fileSearchWidget = new FileSearchContentWidget();
    m_keywordSearchWidget = new KeywordSearchWidget(); // 注意：需要确保 KeywordSearchWidget 构造函数不带侧边栏
    m_stackedWidget->addWidget(m_fileSearchWidget); m_stackedWidget->addWidget(m_keywordSearchWidget);
    midLayout->addWidget(m_stackedWidget);
    connect(tabGroup, &QButtonGroup::idClicked, m_stackedWidget, &QStackedWidget::setCurrentIndex);
    splitter->addWidget(midSide);

    // 右侧边栏
    auto* rightSide = new QWidget(); auto* rightLayout = new QVBoxLayout(rightSide); rightLayout->setContentsMargins(10, 0, 0, 0);
    auto* rightHeader = new QLabel("文件收藏 (可拖入)"); rightHeader->setStyleSheet("color: #888; font-weight: bold; font-size: 12px;");
    rightLayout->addWidget(rightHeader);
    m_fileCollectionSidebar = new FileCollectionSidebarWidget(); rightLayout->addWidget(m_fileCollectionSidebar);
    auto* btnMerge = new QPushButton("合并收藏内容"); btnMerge->setFixedHeight(30);
    connect(btnMerge, &QPushButton::clicked, this, [this](){
        QStringList paths; for(int i=0; i<m_fileCollectionSidebar->count(); ++i) paths << m_fileCollectionSidebar->item(i)->data(Qt::UserRole).toString();
        onMergeFiles(paths, "", true);
    });
    rightLayout->addWidget(btnMerge);
    splitter->addWidget(rightSide);

    splitter->setStretchFactor(0, 0); splitter->setStretchFactor(1, 1); splitter->setStretchFactor(2, 0);
    splitter->setSizes({220, 760, 220});

    // 连接信号
    connect(m_folderSidebar, &QListWidget::itemClicked, this, [this](QListWidgetItem* item){
        QString p = item->data(Qt::UserRole).toString();
        m_fileSearchWidget->setPath(p);
        m_keywordSearchWidget->m_pathEdit->setText(p);
    });
    connect(m_fileSearchWidget, &FileSearchContentWidget::addFileToCollection, this, &UnifiedSearchWindow::addCollectionItem);
}

void UnifiedSearchWindow::addFavorite(const QString& path) {
    for (int i = 0; i < m_folderSidebar->count(); ++i) if (m_folderSidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    QFileInfo fi(path); QString dn = fi.fileName(); if (dn.isEmpty()) dn = QDir::toNativeSeparators(fi.absoluteFilePath());
    auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), dn); item->setData(Qt::UserRole, path); 
    m_folderSidebar->addItem(item); saveFavorites();
}
void UnifiedSearchWindow::addCollectionItem(const QString& path) {
    for (int i = 0; i < m_fileCollectionSidebar->count(); ++i) if (m_fileCollectionSidebar->item(i)->data(Qt::UserRole).toString() == path) return;
    QFileInfo fi(path); auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName()); item->setData(Qt::UserRole, path); 
    m_fileCollectionSidebar->addItem(item); saveCollection();
}
void UnifiedSearchWindow::loadFavorites() {
    QSettings s("RapidNotes", "FileSearchFavorites"); QStringList favs = s.value("list").toStringList();
    for (const QString& p : std::as_const(favs)) if (QDir(p).exists()) {
        QFileInfo fi(p); QString dn = fi.fileName(); if (dn.isEmpty()) dn = QDir::toNativeSeparators(fi.absoluteFilePath());
        auto* item = new QListWidgetItem(IconHelper::getIcon("folder", "#F1C40F"), dn); item->setData(Qt::UserRole, p); 
        m_folderSidebar->addItem(item);
    }
}
void UnifiedSearchWindow::saveFavorites() {
    QStringList favs; for (int i = 0; i < m_folderSidebar->count(); ++i) favs << m_folderSidebar->item(i)->data(Qt::UserRole).toString();
    QSettings s("RapidNotes", "FileSearchFavorites"); s.setValue("list", favs);
}
void UnifiedSearchWindow::loadCollection() {
    QSettings s("RapidNotes", "FileSearchCollection"); QStringList coll = s.value("list").toStringList();
    for (const QString& p : std::as_const(coll)) if (QFile::exists(p)) {
        QFileInfo fi(p); auto* item = new QListWidgetItem(IconHelper::getIcon("file", "#2ECC71"), fi.fileName()); item->setData(Qt::UserRole, p); 
        m_fileCollectionSidebar->addItem(item);
    }
}
void UnifiedSearchWindow::saveCollection() {
    QStringList coll; for (int i = 0; i < m_fileCollectionSidebar->count(); ++i) coll << m_fileCollectionSidebar->item(i)->data(Qt::UserRole).toString();
    QSettings s("RapidNotes", "FileSearchCollection"); s.setValue("list", coll);
}

#include "PasswordVerifyDialog.h"

void UnifiedSearchWindow::onMergeFiles(const QStringList& filePaths, const QString& rootPath, bool useCombineDir) {
    if (filePaths.isEmpty()) return;
    if (!verifyExportPermission()) return;
    
    QString targetDir = rootPath; if (useCombineDir) { targetDir = QCoreApplication::applicationDirPath() + "/Combine"; QDir().mkpath(targetDir); }
    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"); QString outName = QString("%1_export.md").arg(ts); QString outPath = QDir(targetDir).filePath(outName);
    QFile outFile(outPath); if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&outFile); out.setEncoding(QStringConverter::Utf8);
    out << "# 导出结果 - " << ts << "\n\n";
    for (const QString& fp : filePaths) {
        QString relPath = rootPath.isEmpty() ? fp : QDir(rootPath).relativeFilePath(fp);
        out << "## 文件: `" << relPath << "`\n\n```" << getFileLanguage(fp) << "\n";
        QFile inFile(fp); if (inFile.open(QIODevice::ReadOnly | QIODevice::Text)) out << inFile.readAll();
        out << "\n```\n\n";
    }
    outFile.close(); ToolTipOverlay::instance()->showText(QCursor::pos(), "[OK] 已保存: " + outName);
}

void UnifiedSearchWindow::addHistoryEntry(bool isPath, const QString& path) {
    if (path.isEmpty() || (isPath && !QDir(path).exists())) return;
    QSettings s("RapidNotes", isPath ? "FileSearchHistory" : "FileSearchFilenameHistory");
    QStringList h = s.value("list").toStringList(); h.removeAll(path); h.prepend(path); if(h.size()>10) h.removeLast(); s.setValue("list", h);
}
void UnifiedSearchWindow::addSearchHistoryEntry(const QString& text) {
    if (text.isEmpty()) return;
    QSettings s("RapidNotes", "FileSearchFilenameHistory");
    QStringList h = s.value("list").toStringList(); h.removeAll(text); h.prepend(text); if(h.size()>10) h.removeLast(); s.setValue("list", h);
}
QStringList UnifiedSearchWindow::getHistory(bool isPath) const {
    QSettings s("RapidNotes", isPath ? "FileSearchHistory" : "FileSearchFilenameHistory"); return s.value("list").toStringList();
}
void UnifiedSearchWindow::clearHistory(bool isPath) {
    QSettings s("RapidNotes", isPath ? "FileSearchHistory" : "FileSearchFilenameHistory"); s.setValue("list", QStringList());
}
void UnifiedSearchWindow::removeHistoryEntry(bool isPath, const QString& text) {
    QSettings s("RapidNotes", isPath ? "FileSearchHistory" : "FileSearchFilenameHistory"); QStringList h = s.value("list").toStringList(); h.removeAll(text); s.setValue("list", h);
}

bool UnifiedSearchWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            // [MODIFIED] 两段式：综合搜索窗口的输入框按 Esc，不为空则清空，否则清除焦点
            QLineEdit* edit = qobject_cast<QLineEdit*>(watched);
            if (edit) {
                if (!edit->text().isEmpty()) {
                    edit->clear();
                } else {
                    edit->clearFocus();
                }
                event->accept();
                return true;
            }
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

bool UnifiedSearchWindow::verifyExportPermission() {
    // 2026-03-20 按照用户要求，所有导出操作必须验证身份
    return PasswordVerifyDialog::verify();
}

void UnifiedSearchWindow::resizeEvent(QResizeEvent* event) { FramelessDialog::resizeEvent(event); if (m_resizeHandle) m_resizeHandle->move(width() - 20, height() - 20); }

#include "UnifiedSearchWindow.moc"
