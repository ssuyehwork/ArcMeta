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
#include <QInputDialog>
#include <QtConcurrent>
#include <QDir>
#include <QReadLocker>
#include <QWriteLocker>
#include <QElapsedTimer>
#include <QFileDialog>
#include <numeric>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <windows.h>
#include <shellapi.h>

namespace ArcMeta {

// --- ScanConfig Implementation ---

void ScanConfig::load() {
    QFile file("arcmeta_scan_config.json");
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QJsonObject obj = doc.object();

        auto loadSet = [&](const QString& key, QSet<QString>& set) {
            set.clear();
            QJsonArray arr = obj[key].toArray();
            for (const auto& v : arr) set.insert(v.toString());
        };

        loadSet("activeDrives", activeDrives);
        loadSet("defaultDrives", defaultDrives);
        loadSet("ignoredDrives", ignoredDrives);
    }
}

void ScanConfig::save() {
    QFile file("arcmeta_scan_config.json");
    if (file.open(QIODevice::WriteOnly)) {
        QJsonObject obj;
        auto saveSet = [&](const QString& key, const QSet<QString>& set) {
            QJsonArray arr;
            for (const auto& v : set) arr.append(v);
            obj[key] = arr;
        };

        saveSet("activeDrives", activeDrives);
        saveSet("defaultDrives", defaultDrives);
        saveSet("ignoredDrives", ignoredDrives);

        file.write(QJsonDocument(obj).toJson());
    }
}

// --- ScanTableModel Implementation ---

ScanTableModel::ScanTableModel(QObject* parent) : QAbstractTableModel(parent) {}
ScanTableModel::~ScanTableModel() {
    m_filterWatcher.cancel();
    m_filterWatcher.waitForFinished();
}

int ScanTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return std::min(static_cast<int>(m_filteredIndices.size()), m_displayLimit);
}

int ScanTableModel::columnCount(const QModelIndex& /*parent*/) const { return 4; }

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
                if (reader.isDirectory(actualIndex)) return "-";
                int64_t size = reader.getSize(actualIndex);
                if (size <= 0) return "0 B";
                if (size < 1024) return QString("%1 B").arg(size);
                if (size < 1024 * 1024) return QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
                if (size < 1024LL * 1024 * 1024) return QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);
                return QString("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
            }
            case 3: return QDateTime::fromMSecsSinceEpoch(reader.getModifyTime(actualIndex)).toString("yyyy-MM-dd HH:mm");
        }
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        ScanDialog* dlg = qobject_cast<ScanDialog*>(this->parent());
        if (!dlg) return QVariant();
        QString name = reader.getName(actualIndex);
        int dotIdx = name.lastIndexOf('.');
        QString ext = (dotIdx != -1) ? name.mid(dotIdx + 1).toLower() : "";
        if (reader.isDirectory(actualIndex)) ext = "folder";
        auto it = dlg->m_iconCache.find(ext);
        if (it != dlg->m_iconCache.end()) return *it;
        QFileIconProvider provider;
        QIcon icon = reader.isDirectory(actualIndex) ? provider.icon(QFileIconProvider::Folder) : provider.icon(QFileInfo("dummy." + ext));
        dlg->m_iconCache[ext] = icon;
        return icon;
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 2 || index.column() == 3)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    } else if (role == Qt::ForegroundRole && reader.isDirectory(actualIndex)) {
        return QColor("#3498db");
    } else if (role == Qt::UserRole) {
        return actualIndex;
    }
    return QVariant();
}

QVariant ScanTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal) {
        if (role == Qt::DisplayRole) {
            switch (section) {
                case 0: return "名称";
                case 1: return "路径";
                case 2: return "大小";
                case 3: return "修改日期";
            }
        } else if (role == Qt::TextAlignmentRole) {
            if (section == 2 || section == 3)
                return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
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
    if (m_filterWatcher.isRunning()) m_filterWatcher.cancel();

    QFuture<SearchResult> future = QtConcurrent::run([text = m_filterText, state = m_filterState]() {
        QElapsedTimer timer;
        timer.start();
        auto indices = MftReader::instance().search(text, state.useRegex, state.caseSensitive, state.extensionList, state.includeHidden, state.includeSystem);
        return SearchResult{indices, timer.nsecsElapsed() / 1000000.0};
    });

    disconnect(&m_filterWatcher, &QFutureWatcher<SearchResult>::finished, nullptr, nullptr);
    connect(&m_filterWatcher, &QFutureWatcher<SearchResult>::finished, this, [this]() {
        if (m_filterWatcher.isCanceled()) return;
        beginResetModel();
        auto res = m_filterWatcher.result();
        m_filteredIndices = res.indices;
        m_displayLimit = 200;
        endResetModel();
        emit filterFinished(m_filteredIndices.size(), res.ms);
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

// --- ScanDialog Implementation ---

ScanDialog::ScanDialog(QWidget* parent)
    : FramelessDialog("FERREX", parent)
{
    m_config.load();
    resize(960, 640);
    setMinimumSize(800, 500);
    setupUi();

    QTimer::singleShot(100, this, [this]() {
        updateStatus("");
        (void)QtConcurrent::run([this]() {
            bool ok = MftReader::instance().loadFromCache();
            QMetaObject::invokeMethod(this, [this, ok]() {
                if (ok) {
                    updateStatus("");
                    m_tableModel->setFilterText(""); 
                } else {
                    updateStatus("", true);
                    onStartScan();
                }
            });
        });
    });
}

ScanDialog::~ScanDialog() {
    MftReader::instance().clear();
}

void ScanDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(12);

    // 核心修复：极致加固。在 setupUi 最开始初始化所有成员指针，彻底杜绝任何时序导致的空指针闪退
    m_statusLabel = new QLabel("READY");
    m_statLabel = new QLabel("");
    m_selectionLabel = new QLabel("");
    m_memLabel = new QLabel("");
    m_progressBar = new QProgressBar();
    m_exportBtn = new QPushButton("导出所选为 CSV");
    m_resultView = new QTableView();
    m_tableModel = new ScanTableModel(this);
    m_resultView->setModel(m_tableModel);

    auto* driveScroll = new QScrollArea();
    driveScroll->setFixedHeight(45);
    driveScroll->setWidgetResizable(true);
    driveScroll->setFrameShape(QFrame::NoFrame);
    driveScroll->setStyleSheet("background: #252526; border: 1px solid #333; border-radius: 4px;");

    m_driveContainer = new QWidget();
    m_driveLayout = new QHBoxLayout(m_driveContainer);
    m_driveLayout->setContentsMargins(10, 0, 10, 0);
    m_driveLayout->setSpacing(15);
    driveScroll->setWidget(m_driveContainer);

    refreshDriveList();

    auto* topControl = new QHBoxLayout();
    topControl->addWidget(driveScroll, 1);
    mainLayout->addLayout(topControl);

    auto* searchRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("输入文件名 / 关键词...");
    m_searchEdit->setMinimumHeight(36);
    m_searchEdit->setStyleSheet("QLineEdit { background: #2D2D2D; border: 1px solid #3F3F3F; border-radius: 6px; padding: 0 10px; color: #EEE; font-size: 14px; }");
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) { m_tableModel->setFilterText(text); });
    searchRow->addWidget(m_searchEdit, 1);

    m_extEdit = new QLineEdit();
    m_extEdit->setPlaceholderText("后缀");
    m_extEdit->setFixedWidth(100);
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
        cb->setChecked(cb != m_checkCase && cb != m_checkHidden && cb != m_checkSystem);
        connect(cb, &QCheckBox::toggled, this, &ScanDialog::onFilterOptionChanged);
        searchRow->addWidget(cb);
    }
    mainLayout->addLayout(searchRow);

    m_resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultView->setStyleSheet("QTableView { background: #1E1E1E; border: 1px solid #333; color: #D4D4D4; selection-background-color: #094771; outline: none; } QTableView::item { border-bottom: 1px solid #252526; }");
    m_resultView->horizontalHeader()->setStretchLastSection(true);
    m_resultView->horizontalHeader()->setMinimumSectionSize(100);
    m_resultView->verticalHeader()->setVisible(false);
    m_resultView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultView->setShowGrid(false);
    m_resultView->setAlternatingRowColors(true);
    
    connect(&MftReader::instance(), &MftReader::dataChanged, this, [this]() { m_tableModel->setFilterText(m_searchEdit->text()); });
    connect(m_resultView, &QTableView::customContextMenuRequested, this, &ScanDialog::onCustomContextMenu);
    connect(m_resultView, &QTableView::doubleClicked, this, &ScanDialog::onItemDoubleClicked);
    connect(m_resultView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ScanDialog::onSelectionChanged);
    connect(m_resultView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value >= m_resultView->verticalScrollBar()->maximum() * 0.9) m_tableModel->loadMore(200);
    });
    mainLayout->addWidget(m_resultView);

    auto* statusBar = new QHBoxLayout();
    m_statusLabel->setStyleSheet("color: #46B478; font-weight: bold; font-size: 10px;");
    statusBar->addWidget(m_statusLabel);
    statusBar->addSpacing(12);

    m_statLabel->setStyleSheet("color: #7A8F9E; font-size: 10px;");
    statusBar->addWidget(m_statLabel);

    m_selectionLabel->setStyleSheet("color: #7A8F9E; font-size: 10px;");
    statusBar->addWidget(m_selectionLabel);

    m_exportBtn->setFlat(true);
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    m_exportBtn->setStyleSheet("QPushButton { color: #FF8C00; font-size: 10px; border: none; background: transparent; padding: 0; } QPushButton:hover { text-decoration: underline; }");
    m_exportBtn->hide();
    connect(m_exportBtn, &QPushButton::clicked, this, &ScanDialog::onExportCsv);
    statusBar->addWidget(m_exportBtn);

    statusBar->addStretch();

    m_memLabel->setStyleSheet("color: #7A8F9E; font-size: 10px;");
    statusBar->addWidget(m_memLabel);

    m_progressBar->setFixedWidth(150);
    m_progressBar->setFixedHeight(10);
    m_progressBar->setTextVisible(false);
    m_progressBar->hide();
    statusBar->addWidget(m_progressBar);
    mainLayout->addLayout(statusBar);

    connect(m_tableModel, &ScanTableModel::filterFinished, this, [this](int count, double ms) {
        m_statLabel->setText(QString("共 %1 条 | 耗时 %2 ms").arg(QLocale(QLocale::English).toString(count)).arg(ms, 0, 'f', 1));

        // 计算大约内存占用，对标原版 (total * 184 bytes)
        double memoryMb = (count * 184.0) / 1024.0 / 1024.0;
        m_memLabel->setText(QString("数据占用 %.1f MB").arg(memoryMb));

        updateStatus("");
    });
}

void ScanDialog::refreshDriveList() {
    QLayoutItem* item;
    while ((item = m_driveLayout->takeAt(0)) != nullptr) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QLabel* driveLabel = new QLabel("DRIVES");
    driveLabel->setStyleSheet("color: #3D5060; font-weight: bold; font-size: 10px;");
    m_driveLayout->addWidget(driveLabel);

    auto createActionBtn = [&](const QString& text) {
        QPushButton* btn = new QPushButton(text);
        btn->setFlat(true);
        btn->setFixedSize(32, 20);
        btn->setStyleSheet("QPushButton { color: #3D5060; font-size: 10px; border: none; } QPushButton:hover { color: #FF8C00; }");
        return btn;
    };

    QPushButton* btnAll = createActionBtn("全选");
    connect(btnAll, &QPushButton::clicked, this, [this, btnAll]() {
        DWORD mask = GetLogicalDrives();
        bool anyAdded = false;
        for (int i = 0; i < 26; ++i) {
            if (mask & (1 << i)) {
                QString d = QString(QChar('A' + i)) + ":";
                if (!m_config.ignoredDrives.contains(d) && !m_config.activeDrives.contains(d)) {
                    m_config.activeDrives.insert(d);
                    anyAdded = true;
                }
            }
        }
        if (!anyAdded) {
            m_config.activeDrives.clear();
            for (const auto& d : m_config.defaultDrives) m_config.activeDrives.insert(d);
        }
        m_config.save();
        refreshDriveList();
        onStartScan();
    });
    m_driveLayout->addWidget(btnAll);

    DWORD driveMask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (driveMask & (1 << i)) {
            QString driveLetter = QString(QChar('A' + i)) + ":";

            // 物理过滤：仅显示固定硬盘或已就绪的盘符（过滤掉未插入光盘的 DVD/CD 驱动器）
            std::wstring wDrive = (driveLetter + "\\").toStdWString();
            UINT type = GetDriveTypeW(wDrive.c_str());
            if (type == DRIVE_CDROM || type == DRIVE_REMOVABLE) {
                ULARGE_INTEGER freeBytes, totalBytes, totalFree;
                if (!GetDiskFreeSpaceExW(wDrive.c_str(), &freeBytes, &totalBytes, &totalFree)) {
                    continue; // 盘符未就绪（如光驱无盘），不显示
                }
            } else if (type != DRIVE_FIXED) {
                // 如果不是固定硬盘，且不是正在处理的可移动介质，也跳过
                continue;
            }

            if (m_config.ignoredDrives.contains(driveLetter)) continue;

            bool isActive = m_config.activeDrives.contains(driveLetter);
            bool isDefault = m_config.defaultDrives.contains(driveLetter);

            QString text = isDefault ? "★ " + driveLetter : driveLetter;
            QPushButton* btn = new QPushButton(text);
            btn->setCheckable(true);
            btn->setChecked(isActive);

            QString style = isActive ? "QPushButton { background: rgba(255, 140, 0, 30); color: #FF8C00; border: 1px solid #FF8C00; padding: 0 8px; }"
                                     : "QPushButton { background: #111519; color: #7A8F9E; border: 1px solid #252E37; padding: 0 8px; }";
            btn->setStyleSheet(style);

            connect(btn, &QPushButton::clicked, this, [this, driveLetter, isActive]() {
                if (isActive) m_config.activeDrives.remove(driveLetter);
                else m_config.activeDrives.insert(driveLetter);
                m_config.save();
                refreshDriveList();
                onStartScan();
            });

            btn->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(btn, &QPushButton::customContextMenuRequested, this, [this, driveLetter](const QPoint& pos) {
                onDriveContextMenu(driveLetter, pos);
            });

            m_driveLayout->addWidget(btn);
        }
    }

    for (const auto& ignored : m_config.ignoredDrives) {
        QPushButton* btn = new QPushButton(ignored + ": IGNORED");
        btn->setStyleSheet("QPushButton { background: transparent; color: #3D5060; border: 1px solid rgba(61, 80, 96, 50); padding: 0 8px; }");
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(btn, &QPushButton::customContextMenuRequested, this, [this, ignored](const QPoint& pos) {
            onIgnoredDriveContextMenu(ignored, pos);
        });
        m_driveLayout->addWidget(btn);
    }

    m_driveLayout->addStretch();
}

void ScanDialog::onDriveContextMenu(const QString& drive, const QPoint& pos) {
    Q_UNUSED(pos);
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");

    bool isDefault = m_config.defaultDrives.contains(drive);
    menu.addAction(isDefault ? "取消默认选项" : "设为默认选项", [this, drive, isDefault]() {
        if (isDefault) m_config.defaultDrives.remove(drive);
        else m_config.defaultDrives.insert(drive);
        m_config.save();
        refreshDriveList();
    });

    menu.addAction("忽略此驱动器", [this, drive]() {
        m_config.ignoredDrives.insert(drive);
        m_config.activeDrives.remove(drive);
        m_config.defaultDrives.remove(drive);
        m_config.save();
        refreshDriveList();
        onStartScan();
    });

    menu.exec(QCursor::pos());
}

void ScanDialog::onIgnoredDriveContextMenu(const QString& drive, const QPoint& pos) {
    Q_UNUSED(pos);
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");
    menu.addAction("恢复驱动器", [this, drive]() {
        m_config.ignoredDrives.remove(drive);
        m_config.save();
        refreshDriveList();
    });
    menu.exec(QCursor::pos());
}

void ScanDialog::onCustomContextMenu(const QPoint& pos) {
    auto selectedRows = m_resultView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) return;

    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");

    int count = selectedRows.size();

    menu.addAction(count > 1 ? "批量打开文件" : "打开文件", [this, selectedRows]() {
        for (const auto& index : selectedRows) onItemDoubleClicked(index);
    });

    menu.addAction("在“资源管理器”中显示", [this, selectedRows]() {
        QString path = m_tableModel->data(m_tableModel->index(selectedRows.first().row(), 1)).toString();
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
    });

    menu.addSeparator();

    menu.addAction(count > 1 ? "批量复制路径" : "复制路径", [this, selectedRows]() {
        QStringList paths;
        for (const auto& idx : selectedRows) paths << m_tableModel->data(m_tableModel->index(idx.row(), 1)).toString();
        QApplication::clipboard()->setText(paths.join("\n"));
    });

    menu.addAction(count > 1 ? "批量复制文件名" : "复制文件名", [this, selectedRows]() {
        QStringList names;
        for (const auto& idx : selectedRows) names << m_tableModel->data(m_tableModel->index(idx.row(), 0)).toString();
        QApplication::clipboard()->setText(names.join("\n"));
    });

    if (count == 1) {
        menu.addAction("重命名", this, &ScanDialog::onRenameTriggered);
    }

    menu.addSeparator();

    menu.addAction(count > 1 ? "批量删除" : "删除", [this, selectedRows]() {
        QString msg = (selectedRows.size() == 1) ? QString("确定要永久删除 %1 吗？").arg(m_tableModel->data(m_tableModel->index(selectedRows.first().row(), 0)).toString())
                                               : QString("确定要永久删除选中的 %1 个项目吗？").arg(selectedRows.size());
        if (QMessageBox::question(this, "确认删除", msg) == QMessageBox::Yes) {
            for (const auto& idx : selectedRows) {
                QString path = m_tableModel->data(m_tableModel->index(idx.row(), 1)).toString();
                QFile::remove(path);
            }
            m_tableModel->setFilterText(m_searchEdit->text());
        }
    });

    menu.addSeparator();
    menu.addAction("属性", [this, selectedRows]() {
        QString path = m_tableModel->data(m_tableModel->index(selectedRows.first().row(), 1)).toString();
        std::wstring wpath = path.toStdWString();
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.lpVerb = L"properties";
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        ShellExecuteExW(&sei);
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
    if (selectedRows.size() <= 1) {
        m_selectionLabel->clear();
        m_exportBtn->hide();
        m_statLabel->show();
        return;
    }

    m_statLabel->hide();

    int64_t totalSize = 0;
    auto& reader = MftReader::instance();
    for (const auto& index : selectedRows) {
        int actualIdx = m_tableModel->data(index, Qt::UserRole).toInt();
        if (!reader.isDirectory(actualIdx)) totalSize += reader.getSize(actualIdx);
    }

    QString sizeStr;
    if (totalSize < 1024) sizeStr = QString("%1 B").arg(totalSize);
    else if (totalSize < 1024 * 1024) sizeStr = QString("%1 KB").arg(totalSize / 1024.0, 0, 'f', 1);
    else if (totalSize < 1024LL * 1024 * 1024) sizeStr = QString("%1 MB").arg(totalSize / (1024.0 * 1024.0), 0, 'f', 1);
    else sizeStr = QString("%1 GB").arg(totalSize / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);

    m_selectionLabel->setText(QString("已选 %1 项 | 合计大小 %2").arg(selectedRows.size()).arg(sizeStr));
    m_exportBtn->show();
}

void ScanDialog::onStartScan() {
    QStringList selectedDrives;
    for (const auto& d : m_config.activeDrives) selectedDrives << d + "\\";
    if (selectedDrives.isEmpty()) { m_tableModel->setFilterText(m_searchEdit->text()); return; }
    updateStatus("正在扫描...", true);
    (void)QtConcurrent::run([this, selectedDrives]() {
        MftReader::instance().buildIndex(selectedDrives);
        QMetaObject::invokeMethod(this, [this]() {
            updateStatus("就绪");
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
    if (!extText.isEmpty()) state.extensionList = extText.split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    m_tableModel->setFilterState(state);
}

void ScanDialog::updateStatus(const QString& text, bool scanning) {
    Q_UNUSED(text);
    m_statusLabel->setText(scanning ? "SCANNING" : "READY");
    m_statusLabel->setStyleSheet(scanning ? "color: #FF8C00; font-weight: bold; font-size: 10px;" : "color: #46B478; font-weight: bold; font-size: 10px;");

    // 对标原版标题栏显示：[品牌名] [状态 - 数量]
    int total = MftReader::instance().totalCount();
    QString totalStr = QLocale(QLocale::English).toString(total);

    if (!scanning) {
        m_titleLabel->setText(QString("FERREX    READY - %1").arg(totalStr));
        // 这里需要对文字进行部分着色，但 QLabel::setText 不支持局部样式，除非用 HTML
        m_titleLabel->setText(QString("<html><head/><body><p><span style=\"color:#FF8C00; letter-spacing:1.5pt;\">FERREX</span>"
                                     "&nbsp;&nbsp;&nbsp;&nbsp;<span style=\"color:#46B478; font-size:9pt;\">READY - %1</span></p></body></html>").arg(totalStr));
    } else {
        m_titleLabel->setText("<html><head/><body><p><span style=\"color:#FF8C00; letter-spacing:1.5pt;\">FERREX</span>"
                             "&nbsp;&nbsp;&nbsp;&nbsp;<span style=\"color:#FF8C00; font-size:9pt;\">SCANNING...</span></p></body></html>");
    }

    if (scanning) { m_progressBar->show(); m_progressBar->setRange(0, 0); }
    else { m_progressBar->hide(); }
}

void ScanDialog::onRenameTriggered() {
    auto selection = m_resultView->selectionModel()->selectedRows();
    if (selection.isEmpty()) return;
    int row = selection.first().row();
    QString oldPath = m_tableModel->data(m_tableModel->index(row, 1)).toString();
    QString oldName = m_tableModel->data(m_tableModel->index(row, 0)).toString();

    bool ok;
    QString newName = QInputDialog::getText(this, "重命名", "请输入新名称:", QLineEdit::Normal, oldName, &ok);
    if (ok && !newName.isEmpty() && newName != oldName) {
        QFileInfo fi(oldPath);
        QString newPath = fi.absolutePath() + "/" + newName;
        if (QFile::rename(oldPath, newPath)) m_tableModel->setFilterText(m_searchEdit->text());
        else QMessageBox::warning(this, "错误", "重命名失败，请检查文件是否被占用。");
    }
}

void ScanDialog::onExportCsv() {
    auto selectedRows = m_resultView->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) return;

    QString fileName = QFileDialog::getSaveFileName(this, "导出所选为 CSV", "ferrex_export.csv", "CSV 文件 (*.csv)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "名称,路径,大小,修改日期\n";
        auto& reader = MftReader::instance();
        for (const auto& index : selectedRows) {
            int actualIdx = m_tableModel->data(index, Qt::UserRole).toInt();
            out << QString("\"%1\",\"%2\",\"%3\",\"%4\"\n")
                .arg(reader.getName(actualIdx))
                .arg(reader.getFullPath(actualIdx))
                .arg(reader.getSize(actualIdx))
                .arg(QDateTime::fromMSecsSinceEpoch(reader.getModifyTime(actualIdx)).toString("yyyy-MM-dd HH:mm"));
        }
        file.close();
    }
}

void ScanDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier) { m_resultView->selectAll(); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        auto index = m_resultView->currentIndex();
        if (index.isValid()) onItemDoubleClicked(index);
        return;
    }
    FramelessDialog::keyPressEvent(event);
}

} // namespace ArcMeta
