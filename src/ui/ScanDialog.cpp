#include "ScanDialog.h"
#include "../core/CacheManager.h"
#include "../mft/MftReader.h"
#include "UiHelper.h"
#include <QFileInfo>
#include <QCheckBox>
#include <QFrame>
#include <QProgressBar>
#include <QFuture>
#include <QFutureWatcher>
#include <QCloseEvent>
#include <QLineEdit>
#include <QTableView>
#include <QAbstractTableModel>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QScrollArea>
#include <QScrollBar>
#include <QDateTime>
#include <algorithm>
#include <execution>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QProcess>
#include <QMessageBox>
#include <QtConcurrent>
#include <QDir>
#include <QReadLocker>
#include <algorithm>
#include <execution>
#include <QWriteLocker>
#include <numeric>
#include <windows.h>
#include <shellapi.h>

namespace ArcMeta {

// --- ScanTableModel Implementation ---

ScanTableModel::ScanTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

ScanTableModel::~ScanTableModel() {}

int ScanTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return std::min(static_cast<int>(m_filteredIndices.size()), m_displayLimit);
}

int ScanTableModel::columnCount(const QModelIndex& /*parent*/) const {
    return 4;
}

QVariant ScanTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return QVariant();
    
    int row = index.row();
    if (row < 0 || row >= m_filteredIndices.size()) return QVariant();
    
    int actualIndex = m_filteredIndices[row];
    auto& reader = MftReader::instance();
    
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return reader.getName(actualIndex);
            case 1: return reader.getFullPath(actualIndex);
            case 2: {
                if (reader.isDirectory(actualIndex)) return QVariant();
                int64_t size = reader.getSize(actualIndex);
                
                // 2026-05-10 物理修复：彻底禁绝在 data() 渲染路径中进行同步 QFileInfo::size() 磁盘 I/O。
                // 若 MFT 记录为 0 且扫描阶段未填充，此处仅展示 "0 B" 或 "-"，确保 UI 零卡顿。
                if (size <= 0) return "0 B";
                if (size < 1024) return QString("%1 B").arg(size);
                if (size < 1024 * 1024) return QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
                if (size < 1024LL * 1024 * 1024) return QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);
                return QString("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
            }
            case 3: return QDateTime::fromMSecsSinceEpoch(reader.getModifyTime(actualIndex)).toString("yyyy-MM-dd HH:mm");
        }
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        // 2026-05-11 物理优化：极致图标缓存。避免在数据渲染路径中直接进行 QFileInfo/Shell 操作
        ScanDialog* dlg = qobject_cast<ScanDialog*>(this->parent());
        if (!dlg) return QVariant();

        QString name = reader.getName(actualIndex);
        int dotIdx = name.lastIndexOf('.');
        QString ext = (dotIdx != -1) ? name.mid(dotIdx + 1).toLower() : "";
        if (reader.isDirectory(actualIndex)) ext = "folder";
        
        auto it = dlg->m_iconCache.find(ext);
        if (it != dlg->m_iconCache.end()) return *it;
        
        // 2026-05-11 物理补丁：由于主线程渲染限制，此处虽同步但通过 QFileInfo("dummy." + ext) 
        // 绕过了对真实物理文件的访问，大幅降低 I/O 延迟。
        QFileIconProvider provider;
        QIcon icon;
        if (ext == "folder") icon = provider.icon(QFileIconProvider::Folder);
        else {
            icon = provider.icon(QFileInfo("dummy." + ext));
        }
        dlg->m_iconCache[ext] = icon;
        return icon;
    } else if (role == Qt::TextAlignmentRole) {
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    } else if (role == Qt::ForegroundRole) {
        if (reader.isDirectory(actualIndex)) return QColor("#3498db");
    } else if (role == Qt::UserRole) {
        return actualIndex; 
    }
    return QVariant();
}

QVariant ScanTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case 0: return "名称";
            case 1: return "路径";
            case 2: return "大小";
            case 3: return "修改日期";
        }
    }
    return QVariant();
}

void ScanTableModel::setFilterText(const QString& text) {
    if (m_filterText == text) return;
    m_filterText = text;
    startAsyncRebuild();
}

void ScanTableModel::setFilterState(const ScanFilterState& state) {
    m_filterState = state;
    startAsyncRebuild();
}

void ScanTableModel::startAsyncRebuild() {
    // 2026-05-10 物理加固：取消先前的过滤任务，避免结果交织导致乱序或崩溃
    if (m_filterWatcher.isRunning()) {
        m_filterWatcher.cancel();
        // 此处不调用 waitForFinished 以免阻塞 UI 线程，利用 QFutureWatcher 的生命周期管理
    }
    
    QFuture<QVector<int>> future = QtConcurrent::run([this, text = m_filterText, state = m_filterState]() {
        return performRebuild(text, state);
    });
    
    // 断开旧连接，防止多次执行回调
    disconnect(&m_filterWatcher, &QFutureWatcher<QVector<int>>::finished, nullptr, nullptr);

    connect(&m_filterWatcher, &QFutureWatcher<QVector<int>>::finished, this, [this]() {
        // 检查是否已被取消
        if (m_filterWatcher.isCanceled()) return;

        beginResetModel();
        m_filteredIndices = m_filterWatcher.result();
        m_displayLimit = 200; // 重置显示限制
        endResetModel();
        emit filterFinished(m_filteredIndices.size());
    });
    
    m_filterWatcher.setFuture(future);
}

void ScanTableModel::loadMore(int count) {
    if (m_displayLimit >= m_filteredIndices.size()) return;
    int oldLimit = m_displayLimit;
    int newLimit = std::min(static_cast<int>(m_filteredIndices.size()), m_displayLimit + count);
    beginInsertRows(QModelIndex(), oldLimit, newLimit - 1);
    m_displayLimit = newLimit;
    endInsertRows();
}

void ScanTableModel::applyChanges(const QList<UsnChange>& changes) {
    Q_UNUSED(changes);
    startAsyncRebuild();
}

QVector<int> ScanTableModel::performRebuild(const QString& filterText, const ScanFilterState& filterState) {
    // 2026-05-10 对标 Rust：传递属性过滤参数，让隐藏/系统文件过滤真正生效
    return MftReader::instance().search(filterText, filterState.useRegex, filterState.caseSensitive, filterState.extensionList, filterState.includeHidden, filterState.includeSystem);
}

// --- ScanDialog Implementation ---

ScanDialog::ScanDialog(QWidget* parent)
    : FramelessDialog("极致扫描与查找", parent) 
{
    m_cacheManager = std::make_unique<CacheManager>();
    resize(960, 640);
    setMinimumSize(800, 500);
    setupUi();

    QTimer::singleShot(100, this, [this]() {
        m_statusLabel->setText("正在热加载本地快照...");
        m_progressBar->show();
        m_progressBar->setRange(0, 0);

        (void)QtConcurrent::run([this]() {
            bool ok = MftReader::instance().loadFromCache();
            QMetaObject::invokeMethod(this, [this, ok]() {
                m_progressBar->hide();
                if (ok) {
                    m_statusLabel->setText("快照加载成功 (就绪)");
                    m_tableModel->setFilterText(""); 
                } else {
                    // 2026-05-11 按照用户要求对标原版：若无快照，全自动启动扫描，无需手动干预
                    m_statusLabel->setText("未检测到快照，正在全自动初始化索引...");
                    onStartScan();
                }
            });
        });
    });
}

ScanDialog::~ScanDialog() {
    // 2026-05-10 按照用户要求：关闭 ScanDialog 界面后自动卸载 .scch 缓存数据，避免常驻内存
    MftReader::instance().clear();
}

void ScanDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(12);

    auto* driveScroll = new QScrollArea();
    driveScroll->setFixedHeight(45);
    driveScroll->setWidgetResizable(true);
    driveScroll->setFrameShape(QFrame::NoFrame);
    driveScroll->setStyleSheet("background: #252526; border: 1px solid #333; border-radius: 4px;");

    auto* driveContainer = new QWidget();
    auto* driveLayout = new QHBoxLayout(driveContainer);
    driveLayout->setContentsMargins(10, 0, 10, 0);
    driveLayout->setSpacing(15);

    QLabel* driveLabel = new QLabel("扫描盘符:");
    driveLabel->setStyleSheet("color: #AAA; font-weight: bold;");
    driveLayout->addWidget(driveLabel);

    auto createSmallBtn = [&](const QString& text) {
        QPushButton* btn = new QPushButton(text);
        btn->setStyleSheet("QPushButton { background: transparent; color: #378ADD; border: none; padding: 0 5px; font-size: 12px; } QPushButton:hover { color: #4A9AEC; text-decoration: underline; }");
        return btn;
    };
    QPushButton* btnToggle = createSmallBtn("全清");
    
    connect(btnToggle, &QPushButton::clicked, this, [this, btnToggle]() {
        bool anyUnchecked = std::any_of(m_driveChecks.begin(), m_driveChecks.end(), [](QCheckBox* c){ return !c->isChecked(); });
        for(auto* c : m_driveChecks) c->setChecked(anyUnchecked);
        btnToggle->setText(anyUnchecked ? "全清" : "全选");
        onStartScan(); // 勾选变动后立即触发自动增量扫描
    });

    driveLayout->addWidget(btnToggle);
    driveLayout->addSpacing(10);

    DWORD driveMask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (driveMask & (1 << i)) {
            QString driveLetter = QString(QChar('A' + i)) + ":";
            QCheckBox* cb = new QCheckBox(driveLetter);
            cb->setChecked(true); 
            
            cb->setIcon(m_iconProvider.icon(QFileInfo(driveLetter + "/")));
            cb->setIconSize(QSize(16, 16));
            
            cb->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(cb, &QCheckBox::customContextMenuRequested, this, [this, cb](const QPoint& pos) {
                this->onDriveContextMenu(cb, pos);
            });

            cb->setStyleSheet("QCheckBox { color: #EEE; spacing: 5px; } QCheckBox::indicator { width: 14px; height: 14px; }");
            driveLayout->addWidget(cb);
            m_driveChecks.append(cb);
        }
    }
    driveLayout->addStretch();
    driveScroll->setWidget(driveContainer);

    auto* topControl = new QHBoxLayout();
    topControl->addWidget(driveScroll, 1);

    // 2026-05-11 按照用户要求：同步原版逻辑，移除手动扫描按钮。
    // 系统采用全自动 USN 监控 + 启动时热加载，不再提供显式 MFT 重扫入口。

    mainLayout->addLayout(topControl);

    auto* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("输入关键词进行极速搜索...");
    m_searchEdit->setMinimumHeight(36);
    m_searchEdit->setStyleSheet("QLineEdit { background: #2D2D2D; border: 1px solid #3F3F3F; border-radius: 6px; padding: 0 10px; color: #EEE; font-size: 14px; }");
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_tableModel->setFilterText(text);
    });
    searchRow->addWidget(m_searchEdit, 1);

    m_extEdit = new QLineEdit();
    m_extEdit->setPlaceholderText("后缀 (如 exe,dll)");
    m_extEdit->setFixedWidth(120);
    m_extEdit->setMinimumHeight(36);
    m_extEdit->setStyleSheet(m_searchEdit->styleSheet());
    connect(m_extEdit, &QLineEdit::textChanged, this, &ScanDialog::onFilterOptionChanged);
    searchRow->addWidget(m_extEdit);

    m_checkRegex = new QCheckBox("正则");
    m_checkCase = new QCheckBox("大小写");
    m_checkHidden = new QCheckBox("隐藏");
    m_checkSystem = new QCheckBox("系统");
    for (auto* cb : {m_checkRegex, m_checkCase, m_checkHidden, m_checkSystem}) {
        cb->setStyleSheet("QCheckBox { color: #AAA; }");
        cb->setChecked(cb != m_checkCase);
        connect(cb, &QCheckBox::toggled, this, &ScanDialog::onFilterOptionChanged);
        searchRow->addWidget(cb);
    }
    mainLayout->addLayout(searchRow);

    m_resultView = new QTableView();
    m_tableModel = new ScanTableModel(this);
    m_resultView->setModel(m_tableModel);
    
    m_resultView->setDragEnabled(true);
    m_resultView->setAcceptDrops(true);
    m_resultView->setDropIndicatorShown(true);
    m_resultView->setDragDropMode(QAbstractItemView::DragOnly);
    m_resultView->setDefaultDropAction(Qt::IgnoreAction);

    m_resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultView->setStyleSheet(
        "QTableView { background: #1E1E1E; border: 1px solid #333; color: #D4D4D4; gridline-color: #2A2A2A; selection-background-color: #094771; outline: none; }"
        "QTableView::item { border-bottom: 1px solid #252526; padding: 2px 4px; margin: 1px 2px; border-radius: 4px; }"
        "QTableView::item:alternate { background: #252526; }" 
        "QHeaderView::section { background: #252526; color: #888; border: none; border-right: 1px solid #333; border-bottom: 1px solid #333; padding: 4px; }"
    );
    
    m_resultView->horizontalHeader()->setStretchLastSection(true);
    m_resultView->verticalHeader()->setVisible(false);
    m_resultView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultView->setShowGrid(false);
    m_resultView->setAlternatingRowColors(true);
    
    // 交互连接
    connect(&MftReader::instance(), &MftReader::dataChanged, this, [this]() {
        // 2026-05-11 响应式刷新：当 USN 监控到变动时，自动触发模型重绘
        m_tableModel->setFilterText(m_searchEdit->text()); 
    });

    connect(m_resultView, &QTableView::customContextMenuRequested, this, &ScanDialog::onCustomContextMenu);
    connect(m_resultView, &QTableView::doubleClicked, this, &ScanDialog::onItemDoubleClicked);
    connect(m_resultView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ScanDialog::onSelectionChanged);

    // 2026-05-10 对标 Rust 极速加载：通过监听垂直滚动条进行虚拟分页加载
    connect(m_resultView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        int maximum = m_resultView->verticalScrollBar()->maximum();
        if (maximum > 0 && value >= maximum * 0.9) {
            m_tableModel->loadMore(200);
        }
    });

    mainLayout->addWidget(m_resultView);

    // 4. 状态反馈 (对标 Rust: 选择统计)
    auto* statusBar = new QHBoxLayout();
    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("color: #007ACC; font-weight: bold; font-size: 11px;");
    statusBar->addWidget(m_statusLabel);
    
    QLabel* sep = new QLabel("|");
    sep->setStyleSheet("color: #444;");
    statusBar->addWidget(sep);

    m_summaryLabel = new QLabel("索引总数: 0");
    m_summaryLabel->setStyleSheet("color: #888; font-size: 11px;");
    statusBar->addWidget(m_summaryLabel);

    m_selectionLabel = new QLabel("");
    m_selectionLabel->setStyleSheet("color: #AAA; font-size: 11px; margin-left: 10px;");
    statusBar->addWidget(m_selectionLabel);

    statusBar->addStretch();

    m_progressBar = new QProgressBar();
    m_progressBar->setFixedWidth(150);
    m_progressBar->setFixedHeight(10);
    m_progressBar->setTextVisible(false);
    m_progressBar->hide();
    statusBar->addWidget(m_progressBar);

    mainLayout->addLayout(statusBar);

    connect(m_tableModel, &ScanTableModel::filterFinished, this, [this](int count) {
        m_summaryLabel->setText(QString("匹配结果: %1 | 索引总数: %2").arg(count).arg(MftReader::instance().totalCount()));
    });
}

void ScanDialog::onCustomContextMenu(const QPoint& pos) {
    QModelIndex index = m_resultView->indexAt(pos);
    if (!index.isValid()) return;

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #252526; color: #EEE; border: 1px solid #333; } QMenu::item:selected { background: #094771; }");
    
    menu.addAction("打开 / 运行", [this, index]() { onItemDoubleClicked(index); });
    menu.addAction("打开文件所在位置", [this, index]() {
        QString path = m_tableModel->data(m_tableModel->index(index.row(), 1)).toString();
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
    });
    menu.addSeparator();
    menu.addAction("复制名称", [this, index]() {
        QApplication::clipboard()->setText(m_tableModel->data(m_tableModel->index(index.row(), 0)).toString());
    });
    menu.addAction("复制完整路径", [this, index]() {
        QApplication::clipboard()->setText(m_tableModel->data(m_tableModel->index(index.row(), 1)).toString());
    });
    menu.addSeparator();
    menu.addAction("删除 (移至回收站)", [this, index]() {
        QString path = m_tableModel->data(m_tableModel->index(index.row(), 1)).toString();
        if (QMessageBox::question(this, "确认", QString("确定要删除 %1 吗？").arg(path)) == QMessageBox::Yes) {
            QFile::moveToTrash(path);
        }
    });
    menu.addAction("查看属性", [this, index]() {
        QString path = m_tableModel->data(m_tableModel->index(index.row(), 1)).toString();
        std::wstring wpath = path.toStdWString();
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.lpVerb = L"properties";
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        ShellExecuteExW(&sei);
    });
    menu.addSeparator();
    menu.addAction("全选 (Ctrl+A)", [this]() { m_resultView->selectAll(); });
    menu.addAction("全清", [this]() { m_resultView->clearSelection(); });
    menu.addAction("反选", [this]() {
        QItemSelectionModel* selectionModel = m_resultView->selectionModel();
        QItemSelection newSelection;
        for (int i = 0; i < m_tableModel->rowCount(); ++i) {
            QModelIndex idx = m_tableModel->index(i, 0);
            if (!selectionModel->isSelected(idx)) {
                newSelection.select(idx, idx);
            }
        }
        selectionModel->select(newSelection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    });

    menu.exec(m_resultView->viewport()->mapToGlobal(pos));
}

void ScanDialog::onItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    QString path = m_tableModel->data(m_tableModel->index(index.row(), 1)).toString();
    ShellExecuteW(NULL, L"open", reinterpret_cast<const wchar_t*>(path.utf16()), NULL, NULL, SW_SHOWNORMAL);
}

void ScanDialog::onSelectionChanged() {
    auto selectedRows = m_resultView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) {
        m_selectionLabel->clear();
        return;
    }

    int64_t totalSize = 0;
    auto& reader = MftReader::instance();
    for (const auto& index : selectedRows) {
        int actualIdx = m_tableModel->data(index, Qt::UserRole).toInt();
        if (!reader.isDirectory(actualIdx)) {
            totalSize += reader.getSize(actualIdx);
        }
    }

    QString sizeStr;
    if (totalSize < 1024) sizeStr = QString("%1 B").arg(totalSize);
    else if (totalSize < 1024 * 1024) sizeStr = QString("%1 KB").arg(totalSize / 1024.0, 0, 'f', 1);
    else sizeStr = QString("%1 MB").arg(totalSize / (1024.0 * 1024.0), 0, 'f', 1);

    m_selectionLabel->setText(QString("已选择 %1 个项目 | 总计: %2").arg(selectedRows.size()).arg(sizeStr));
}

void ScanDialog::onStartScan() {
    QStringList selectedDrives;
    for (auto* cb : m_driveChecks) {
        if (cb->isChecked()) {
            selectedDrives << cb->text() + "/";
        }
    }

    if (selectedDrives.isEmpty()) {
        QMessageBox::warning(this, "提示", "请至少选择一个盘符进行扫描！");
        return;
    }

    m_progressBar->show();
    m_progressBar->setRange(0, 0);
    m_statusLabel->setText("正在扫描...");

    (void)QtConcurrent::run([this, selectedDrives]() {
        MftReader::instance().buildIndex(selectedDrives);
        QMetaObject::invokeMethod(this, [this]() {
            m_progressBar->hide();
            m_statusLabel->setText("就绪");
            m_tableModel->setFilterText(m_searchEdit->text());
        });
    });
}

void ScanDialog::onFilterOptionChanged() {
    ScanFilterState state;
    state.useRegex = m_checkRegex->isChecked();
    state.caseSensitive = m_checkCase->isChecked();
    state.includeHidden = m_checkHidden->isChecked();
    state.includeSystem = m_checkSystem->isChecked();
    
    QString extText = m_extEdit->text().toLower();
    if (!extText.isEmpty()) {
        state.extensionList = extText.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    }
    
    m_tableModel->setFilterState(state);
}

void ScanDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier) {
        m_resultView->selectAll();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (event->modifiers() == Qt::AltModifier) {
            auto selection = m_resultView->selectionModel()->selectedRows();
            if (!selection.isEmpty()) {
                QString path = m_tableModel->data(m_tableModel->index(selection.first().row(), 1)).toString();
                SHELLEXECUTEINFOW sei = {sizeof(sei)};
                sei.fMask = SEE_MASK_INVOKEIDLIST;
                sei.lpVerb = L"properties";
                sei.lpFile = reinterpret_cast<const wchar_t*>(path.utf16());
                sei.nShow = SW_SHOW;
                ShellExecuteExW(&sei);
            }
        } else {
            auto index = m_resultView->currentIndex();
            if (index.isValid()) onItemDoubleClicked(index);
        }
        return;
    } else if (event->matches(QKeySequence::Copy)) {
        auto selectedRows = m_resultView->selectionModel()->selectedRows();
        if (!selectedRows.isEmpty()) {
            QStringList paths;
            for (const auto& idx : selectedRows) {
                paths << m_tableModel->data(m_tableModel->index(idx.row(), 1)).toString();
            }
            QApplication::clipboard()->setText(paths.join("\n"));
        }
    }
    FramelessDialog::keyPressEvent(event);
}

void ScanDialog::onDriveContextMenu(QCheckBox* cb, const QPoint& pos) {
    if (!cb) return;
    
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #252526; color: #EEE; border: 1px solid #333; } QMenu::item:selected { background: #094771; }");

    menu.addAction("仅扫描此盘", [this, cb]() {
        for (auto* check : m_driveChecks) check->setChecked(check == cb);
        onStartScan();
    });
    menu.addSeparator();
    menu.addAction("在资源管理器中打开", [cb]() {
        QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(cb->text() + "/")});
    });
    menu.addAction("磁盘属性", [cb]() {
        QString drive = cb->text() + "/";
        SHELLEXECUTEINFOW sei = {sizeof(sei)};
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.lpVerb = L"properties";
        sei.lpFile = reinterpret_cast<const wchar_t*>(drive.utf16());
        sei.nShow = SW_SHOW;
        ShellExecuteExW(&sei);
    });
    menu.addSeparator();
    menu.addAction("全部勾选", [this]() {
        for (auto* check : m_driveChecks) check->setChecked(true);
    });
    menu.addAction("全部取消勾选", [this]() {
        for (auto* check : m_driveChecks) check->setChecked(false);
    });

    menu.exec(cb->mapToGlobal(pos));
}

} // namespace ArcMeta
