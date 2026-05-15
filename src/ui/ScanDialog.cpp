#ifndef NOMINMAX
#define NOMINMAX
#endif
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
#include <QPointer>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QDir>
#include <QReadLocker>
#include <QWriteLocker>
#include <numeric>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <windows.h>
#include <shellapi.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#ifdef run
#undef run
#endif


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
        
        QJsonArray qArr = obj["queryHistory"].toArray();
        for (const auto& v : qArr) queryHistory.append(v.toString());
        QJsonArray eArr = obj["extHistory"].toArray();
        for (const auto& v : eArr) extHistory.append(v.toString());
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
        
        QJsonArray qArr; for (const auto& v : queryHistory) qArr.append(v);
        obj["queryHistory"] = qArr;
        QJsonArray eArr; for (const auto& v : extHistory) eArr.append(v);
        obj["extHistory"] = eArr;
        
        file.write(QJsonDocument(obj).toJson());
    }
}

// --- ScanTableModel Implementation ---

ScanTableModel::ScanTableModel(QObject* parent) : QAbstractTableModel(parent) {
    connect(&MftReader::instance(), &MftReader::dataChanged, this, [this](int index) {
        if (index == -1) {
            beginResetModel();
            endResetModel();
            return;
        }
        // 性能优化：仅刷新受影响的行
        for (int i = 0; i < m_filteredIndices.size(); ++i) {
            if (m_filteredIndices[i] == index) {
                emit dataChanged(this->index(i, 0), this->index(i, 3));
                break;
            }
        }
    });
}
ScanTableModel::~ScanTableModel() {}

int ScanTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return (std::min)(static_cast<int>(m_filteredIndices.size()), m_displayLimit);
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
                if (size == 0 && !reader.isMetadataFetched(actualIndex)) {
                    const_cast<MftReader&>(reader).requestMetadata(actualIndex);
                    return "...";
                }
                if (size < 1024) return QString("%1 B").arg(size);
                if (size < 1024 * 1024) return QString("%1 KB").arg(size / 1024.0, 0, 'f', 2);
                if (size < 1024LL * 1024 * 1024) return QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 2);
                return QString("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
            }
            case 3: {
                int64_t ts = reader.getModifyTime(actualIndex);
                if (ts == 0 && !reader.isMetadataFetched(actualIndex)) {
                    const_cast<MftReader&>(reader).requestMetadata(actualIndex);
                    return "-";
                }
                if (ts == 0) return "-";
                return QDateTime::fromMSecsSinceEpoch(ts).toString("yyyy-MM-dd HH:mm");
            }
        }
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        QString name = reader.getName(actualIndex);
        int dotIdx = name.lastIndexOf('.');
        QString ext = (dotIdx != -1) ? name.mid(dotIdx + 1).toLower() : "";
        return reader.getCachedIcon(ext, reader.isDirectory(actualIndex));
    } else if (role == Qt::ForegroundRole && reader.isDirectory(actualIndex)) {
        return QColor("#3498db");
    } else if (role == Qt::TextAlignmentRole) {
        switch (index.column()) {
            case 0: case 1: return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
            case 2: case 3: return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
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
    m_filterText = text;
}

void ScanTableModel::triggerSearch() {
    startAsyncRebuild();
}

void ScanTableModel::setFilterState(const ScanFilterState& state) {
    m_filterState = state;
}

void ScanTableModel::startAsyncRebuild() {
    if (m_filterWatcher.isRunning()) m_filterWatcher.cancel();
    
    QElapsedTimer* timer = new QElapsedTimer();
    timer->start();

    QFuture<QVector<int>> future = (QtConcurrent::run)([this, text = m_filterText, state = m_filterState]() {
        return MftReader::instance().search(text, state.useRegex, state.caseSensitive, state.extensionList, state.includeHidden, state.includeSystem);
    });

    disconnect(&m_filterWatcher, &QFutureWatcher<QVector<int>>::finished, this, nullptr);

    connect(&m_filterWatcher, &QFutureWatcher<QVector<int>>::finished, this, [this, timer]() {
        if (m_filterWatcher.isCanceled()) {
            delete timer;
            return;
        }

        beginResetModel();
        m_filteredIndices = m_filterWatcher.result();
        m_displayLimit = 200;
        endResetModel();

        ScanDialog* dlg = qobject_cast<ScanDialog*>(parent());
        if (dlg) {
            dlg->m_lastSearchMs = timer->elapsed();
        }
        delete timer;

        emit filterFinished(m_filteredIndices.size());
    });

    m_filterWatcher.setFuture(future);
}

void ScanTableModel::loadMore(int count) {
    if (m_displayLimit >= m_filteredIndices.size()) return;
    int oldLimit = m_displayLimit;
    int newLimit = (std::min)(static_cast<int>(m_filteredIndices.size()), m_displayLimit + count);
    beginInsertRows(QModelIndex(), oldLimit, newLimit - 1);
    m_displayLimit = newLimit;
    endInsertRows();
}

// --- ScanDialog Implementation ---

ScanDialog::ScanDialog(QWidget* parent)
    : FramelessDialog("FERREX", parent) 
{
    m_config.load();
    resize(1000, 700);
    setMinimumSize(800, 500);

    m_titleStatusLabel = new QLabel("READY - 0");
    m_titleStatusLabel->setStyleSheet("color: #46B478; font-size: 10px; font-weight: bold; margin-left: 12px;");

    if (m_titleLabel && m_pinBtn && m_pinBtn->parentWidget() && m_pinBtn->parentWidget()->layout()) {
        m_titleLabel->hide(); 
        auto* titleLayout = qobject_cast<QHBoxLayout*>(m_pinBtn->parentWidget()->layout());
        if (titleLayout) {
            QLabel* logoLabel = new QLabel();
            logoLabel->setFixedSize(18, 18);
            logoLabel->setPixmap(UiHelper::getIcon("ferrex", QColor("#FF8C00"), 18).pixmap(18, 18));
            titleLayout->insertWidget(0, logoLabel);
            
            QLabel* brandLabel = new QLabel("FERREX");
            brandLabel->setStyleSheet("color: #FF8C00; font-size: 14px; font-weight: bold; letter-spacing: 1.5px; margin-left: 6px;");
            titleLayout->insertWidget(1, brandLabel);
            
            titleLayout->insertWidget(2, m_titleStatusLabel);
        } else {
            m_titleStatusLabel->hide(); 
        }
    } else {
        m_titleStatusLabel->hide();
    }

    setupUi();

    QTimer::singleShot(100, this, [this]() {
        updateStatus("正在载入本地快照...");
        QPointer<ScanDialog> weakThis(this);
        (void)(QtConcurrent::run)([weakThis]() {
            bool ok = MftReader::instance().loadFromCache();
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, ok]() {
                if (!weakThis) return;
                if (ok) {
                    weakThis->updateStatus("就绪");
                    weakThis->m_tableModel->setFilterText("");
                    weakThis->refreshDriveList(true); // 后台探测硬件
                } else {
                    weakThis->updateStatus("未检测到快照，全自动初始化...");
                    weakThis->refreshDriveList(true);
                    weakThis->onStartScan();
                }
            });
        });
    });
}

ScanDialog::~ScanDialog() {
    // 2026-05-14 架构优化：移除 MftReader::instance().clear()
    // MftReader 作为全局单例，其生命周期不应与搜索窗口绑定。
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

    m_driveContainer = new QWidget();
    m_driveLayout = new QHBoxLayout(m_driveContainer);
    m_driveLayout->setContentsMargins(10, 0, 10, 0);
    m_driveLayout->setSpacing(15);
    driveScroll->setWidget(m_driveContainer);

    auto* topControl = new QHBoxLayout();
    topControl->addWidget(driveScroll, 1);
    mainLayout->addLayout(topControl);

    auto* searchContainer = new QWidget();
    auto* searchVLayout = new QVBoxLayout(searchContainer);
    searchVLayout->setContentsMargins(0, 0, 0, 0);
    searchVLayout->setSpacing(0);

    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(5, 8, 5, 8);
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("输入文件名 / 关键词...");
    m_searchEdit->setMinimumHeight(36);
    m_searchEdit->setStyleSheet("QLineEdit { background: #2D2D2D; border: 1px solid #3F3F3F; border-radius: 6px 0 0 6px; padding: 0 10px; color: #EEE; font-size: 14px; border-right: none; }");
    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &ScanDialog::onTriggerSearch);
    searchRow->addWidget(m_searchEdit, 1);

    m_extEdit = new QLineEdit();
    m_extEdit->setPlaceholderText("后缀");
    m_extEdit->setFixedWidth(80);
    m_extEdit->setMinimumHeight(36);
    m_extEdit->setStyleSheet("QLineEdit { background: #2D2D2D; border-top: 1px solid #3F3F3F; border-bottom: 1px solid #3F3F3F; border-left: 1px solid #444; border-right: none; color: #EEE; font-size: 14px; }");
    m_extEdit->installEventFilter(this);
    connect(m_extEdit, &QLineEdit::returnPressed, this, &ScanDialog::onTriggerSearch);
    searchRow->addWidget(m_extEdit);

    m_searchBtn = new QPushButton("搜索");
    m_searchBtn->setFixedWidth(70);
    m_searchBtn->setMinimumHeight(36);
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setStyleSheet("QPushButton { background: #FF8C00; color: #000; border: none; border-radius: 0 6px 6px 0; font-weight: bold; font-size: 13px; } QPushButton:hover { background: #FFA500; } QPushButton:pressed { background: #CC6600; }");
    connect(m_searchBtn, &QPushButton::clicked, this, &ScanDialog::onTriggerSearch);
    searchRow->addWidget(m_searchBtn);

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
    searchVLayout->addLayout(searchRow);

    m_progressBar = new QProgressBar();
    m_progressBar->setFixedHeight(2);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet("QProgressBar { background: transparent; border: none; } QProgressBar::chunk { background: #FF8C00; }");
    m_progressBar->hide();
    searchVLayout->addWidget(m_progressBar);

    mainLayout->addWidget(searchContainer);

    m_resultView = new QTableView();
    m_tableModel = new ScanTableModel(this);
    m_resultView->setModel(m_tableModel);
    m_resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // 2026-05-14 视觉优化：基于色码分析，将斑马纹调整为深灰色 (#1E1E1E) 与纯黑色 (#000000) 搭配
    m_resultView->setStyleSheet(
        "QTableView { "
        "background-color: #1E1E1E; "
        "alternate-background-color: #000000; "
        "border: 1px solid #333; "
        "color: #D4D4D4; "
        "selection-background-color: #094771; "
        "selection-color: #FFFFFF; "
        "outline: none; "
        "gridline-color: transparent; "
        "}"
        "QTableView::item { border-bottom: 1px solid #252526; }"
        "QHeaderView::section { background-color: #252526; color: #888; border: none; border-right: 1px solid #333; padding: 4px; height: 24px; }"
        "QHeaderView { background-color: #252526; border: none; }"
    );
    
    m_resultView->horizontalHeader()->setStretchLastSection(false); 
    m_resultView->horizontalHeader()->setMinimumSectionSize(60);
    // 2026-05-14 物理修正：强制列标题水平居中对齐
    m_resultView->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    
    m_resultView->setColumnWidth(0, 260); 
    m_resultView->setColumnWidth(2, 100); 
    m_resultView->setColumnWidth(3, 140); 
    
    m_resultView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_resultView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_resultView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_resultView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);

    m_resultView->verticalHeader()->setVisible(false);
    m_resultView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultView->setShowGrid(false);
    m_resultView->setAlternatingRowColors(true);
    
    connect(m_resultView, &QTableView::customContextMenuRequested, this, &ScanDialog::onCustomContextMenu);
    connect(m_resultView, &QTableView::doubleClicked, this, &ScanDialog::onItemDoubleClicked);
    connect(m_resultView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ScanDialog::onSelectionChanged);
    connect(m_resultView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value >= m_resultView->verticalScrollBar()->maximum() * 0.9) m_tableModel->loadMore(200);
    });
    mainLayout->addWidget(m_resultView);

    auto* statusContainer = new QWidget();
    statusContainer->setFixedHeight(26);
    auto* statusBar = new QHBoxLayout(statusContainer);
    statusBar->setContentsMargins(16, 0, 16, 0);
    statusBar->setSpacing(0);

    m_selectionLabel = new QLabel("");
    m_selectionLabel->setStyleSheet("color: #7A8F9E; font-size: 10px;");
    statusBar->addWidget(m_selectionLabel);

    m_csvBtn = new QPushButton("导出所选为 CSV");
    m_csvBtn->setFlat(true);
    m_csvBtn->setCursor(Qt::PointingHandCursor);
    m_csvBtn->setStyleSheet("QPushButton { color: #FF8C00; font-size: 10px; border: none; padding: 0; text-decoration: none; } QPushButton:hover { text-decoration: underline; }");
    m_csvBtn->hide();
    statusBar->addWidget(m_csvBtn);

    m_statLabelMain = new QLabel("");
    m_statLabelMain->setStyleSheet("color: #7A8F9E; font-size: 10px;");
    statusBar->addWidget(m_statLabelMain);

    m_statLabelTime = new QLabel("");
    m_statLabelTime->setStyleSheet("color: #7A8F9E; font-size: 10px; margin-left: 12px;");
    statusBar->addWidget(m_statLabelTime);

    statusBar->addStretch();

    m_statLabelMemory = new QLabel("");
    m_statLabelMemory->setStyleSheet("color: #7A8F9E; font-size: 10px;");
    statusBar->addWidget(m_statLabelMemory);

    mainLayout->addWidget(statusContainer);

    connect(m_tableModel, &ScanTableModel::filterFinished, this, [this](int count) {
        Q_UNUSED(count);
        updateStatusBar();
    });
}

void ScanDialog::refreshDriveList(bool forceProbe) {
    if (!forceProbe && !m_cachedDriveInfos.isEmpty()) {
        updateDriveButtonStyles();
        return;
    }

    QPointer<ScanDialog> weakThis(this);
    (void)(QtConcurrent::run)([weakThis]() {
        if (!weakThis) return;
        QVector<DriveInfo> drives;
        DWORD driveMask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (driveMask & (1 << i)) {
                QString letter = QString(QChar('A' + i)) + ":";
                WCHAR volName[MAX_PATH + 1] = {0};
                WCHAR fsName[MAX_PATH + 1] = {0};
                BOOL ok = GetVolumeInformationW(reinterpret_cast<const wchar_t*>((letter + "\\").utf16()), 
                                              volName, MAX_PATH + 1, NULL, NULL, NULL, 
                                              fsName, MAX_PATH + 1);
                DriveInfo info;
                info.letter = letter;
                info.hasMedia = ok;
                if (ok) {
                    info.label = QString::fromWCharArray(volName);
                    info.isNtfs = QString::fromWCharArray(fsName).contains("NTFS", Qt::CaseInsensitive);
                } else {
                    info.isNtfs = false;
                }
                drives.append(info);
            }
        }

        QMetaObject::invokeMethod(weakThis.data(), [weakThis, drives]() {
            if (!weakThis) return;
            weakThis->m_cachedDriveInfos = drives;
            
            QLayoutItem* item;
            while ((item = weakThis->m_driveLayout->takeAt(0)) != nullptr) {
                if (item->widget()) item->widget()->deleteLater();
                delete item;
            }
            weakThis->m_driveButtonMap.clear();

            // 2026-05-14 用户要求彻底移除 "DRIVES" 标签
            // QLabel* driveLabel = new QLabel("DRIVES");
            // driveLabel->setStyleSheet("color: #3D5060; font-weight: bold; font-size: 10px;");
            // weakThis->m_driveLayout->addWidget(driveLabel);

            for (const auto& info : drives) {
                if (!info.hasMedia || !info.isNtfs) continue;
                if (weakThis->m_config.ignoredDrives.contains(info.letter)) continue;

                QString label = info.label.isEmpty() ? "本地磁盘" : info.label;
                QString btnText = QString("%1 (%2)").arg(info.letter).arg(label);
                
                QPushButton* btn = new QPushButton(btnText);
                btn->setCheckable(true);
                btn->setFixedHeight(24);
                weakThis->m_driveButtonMap[info.letter] = btn;
                
                connect(btn, &QPushButton::clicked, weakThis.data(), [weakThis, letter = info.letter]() {
                    if (!weakThis) return;
                    if (weakThis->m_config.activeDrives.contains(letter)) {
                        if (weakThis->m_config.activeDrives.size() > 1) weakThis->m_config.activeDrives.remove(letter);
                    } else {
                        weakThis->m_config.activeDrives.insert(letter);
                    }
                    weakThis->updateDriveButtonStyles();
                    weakThis->onStartScan();
                });
                
                btn->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(btn, &QPushButton::customContextMenuRequested, weakThis.data(), [weakThis, letter = info.letter](const QPoint& pos) {
                    if (weakThis) weakThis->onDriveContextMenu(letter, pos);
                });
                
                weakThis->m_driveLayout->addWidget(btn);
            }
            weakThis->m_driveLayout->addStretch();
            weakThis->updateDriveButtonStyles();
        });
    });
}

void ScanDialog::updateDriveButtonStyles() {
    for (auto it = m_driveButtonMap.begin(); it != m_driveButtonMap.end(); ++it) {
        bool isActive = m_config.activeDrives.contains(it.key());
        bool isDefault = m_config.defaultDrives.contains(it.key());
        it.value()->setChecked(isActive);
        
        QString style = isActive ? "QPushButton { background: rgba(255, 140, 0, 30); color: #FF8C00; border: 1px solid #FF8C00; padding: 0 10px; font-size: 12px; font-weight: bold; }" 
                                 : "QPushButton { background: #111519; color: #7A8F9E; border: 1px solid #252E37; padding: 0 10px; font-size: 12px; }";
        it.value()->setStyleSheet(style);
        
        QString label = "";
        for (const auto& info : m_cachedDriveInfos) { if (info.letter == it.key()) { label = info.label; break; } }
        it.value()->setText(QString("%1%2 (%3)").arg(isDefault ? "★ " : "").arg(it.key()).arg(label.isEmpty() ? "本地磁盘" : label));
    }
}

void ScanDialog::onDriveContextMenu(const QString& drive, const QPoint& /*pos*/) {
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");
    
    bool isDefault = m_config.defaultDrives.contains(drive);
    menu.addAction(isDefault ? "取消默认选项" : "设为默认选项", [this, drive, isDefault]() {
        if (isDefault) m_config.defaultDrives.remove(drive);
        else m_config.defaultDrives.insert(drive);
        m_config.save();
        updateDriveButtonStyles();
    });
    
    menu.addAction("忽略此驱动器", [this, drive]() {
        m_config.ignoredDrives.insert(drive);
        m_config.activeDrives.remove(drive);
        m_config.save();
        refreshDriveList(true); // 重新生成按钮
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
        refreshDriveList(true);
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
            m_tableModel->triggerSearch();
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
    if (selectedRows.isEmpty()) { m_selectionLabel->clear(); return; }
    int64_t totalSize = 0;
    auto& reader = MftReader::instance();
    for (const auto& index : selectedRows) {
        int actualIdx = m_tableModel->data(index, Qt::UserRole).toInt();
        if (!reader.isDirectory(actualIdx)) totalSize += reader.getSize(actualIdx);
    }
    QString sizeStr;
    if (totalSize < 1024) sizeStr = QString("%1 B").arg(totalSize);
    else if (totalSize < 1024 * 1024) sizeStr = QString("%1 KB").arg(totalSize / 1024.0, 0, 'f', 1);
    else sizeStr = QString("%1 MB").arg(totalSize / (1024.0 * 1024.0), 0, 'f', 1);
    m_selectionLabel->setText(QString("已选择 %1 项 | 合计大小: %2").arg(selectedRows.size()).arg(sizeStr));
}

void ScanDialog::onStartScan() {
    QStringList selectedDrives;
    for (const auto& d : m_config.activeDrives) selectedDrives << d + "\\";
    if (selectedDrives.isEmpty()) { onTriggerSearch(); return; }
    updateStatus("正在扫描...", true);

    QPointer<ScanDialog> weakThis(this);
    (void)(QtConcurrent::run)([weakThis, selectedDrives]() {
        MftReader::instance().buildIndex(selectedDrives);
        QMetaObject::invokeMethod(weakThis.data(), [weakThis]() {
            if (!weakThis) return;
            weakThis->updateStatus("就绪");
            weakThis->onTriggerSearch();
        });
    });
}

void ScanDialog::onTriggerSearch() {
    // 1. 同步搜索历史
    QString q = m_searchEdit->text().trimmed();
    if (!q.isEmpty()) {
        m_config.queryHistory.removeAll(q);
        m_config.queryHistory.prepend(q);
        if (m_config.queryHistory.size() > 10) m_config.queryHistory.removeLast();
    }
    QString e = m_extEdit->text().trimmed();
    if (!e.isEmpty()) {
        m_config.extHistory.removeAll(e);
        m_config.extHistory.prepend(e);
        if (m_config.extHistory.size() > 10) m_config.extHistory.removeLast();
    }
    m_config.save();

    // 2. 核心同步：将 UI 盘符勾选状态更新至搜索引擎掩码 (修复搜出未选盘符数据的傻逼 Bug)
    QStringList activeList;
    for (const QString& drive : m_config.activeDrives) activeList << drive;
    MftReader::instance().updateActiveDrives(activeList);

    // 3. 执行过滤并触发搜索
    onFilterOptionChanged();
    m_tableModel->setFilterText(m_searchEdit->text());
    m_tableModel->triggerSearch(); 
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
    if (m_titleStatusLabel) {
        int total = MftReader::instance().totalCount();
        m_titleStatusLabel->setText(QString("%1 - %2").arg(scanning ? "SCANNING" : "READY").arg(formatNumber(total)));
        m_titleStatusLabel->setStyleSheet(scanning ? "color: #FF8C00; font-size: 10px; font-weight: bold;" : "color: #46B478; font-size: 10px; font-weight: bold;");
    }
    
    if (scanning) { m_progressBar->show(); m_progressBar->setRange(0, 0); }
    else { m_progressBar->hide(); updateStatusBar(); }
}

void ScanDialog::updateStatusBar() {
    auto selectedRows = m_resultView->selectionModel()->selectedRows();
    if (selectedRows.size() > 1) {
        m_statLabelMain->hide();
        m_statLabelTime->hide();
        m_selectionLabel->show();
        m_csvBtn->show();
        
        int64_t totalSize = 0;
        auto& reader = MftReader::instance();
        for (const auto& index : selectedRows) {
            int actualIdx = m_tableModel->data(index, Qt::UserRole).toInt();
            if (!reader.isDirectory(actualIdx)) totalSize += reader.getSize(actualIdx);
        }
        m_selectionLabel->setText(QString("已选 %1 项 | 合计大小 %2  ").arg(selectedRows.size()).arg(formatSize(totalSize)));
    } else {
        m_selectionLabel->hide();
        m_csvBtn->hide();
        m_statLabelMain->show();
        m_statLabelTime->show();
        
        int totalMatch = m_tableModel->totalFilteredCount();
        int currentPageMatch = m_tableModel->rowCount(); 
        m_statLabelMain->setText(QString("共 %1 条 | 本页 %2 条").arg(formatNumber(totalMatch)).arg(formatNumber(currentPageMatch)));
        m_statLabelTime->setText(QString("耗时 %1 ms").arg(m_lastSearchMs));
    }
    
    double memoryMb = (MftReader::instance().totalCount() * 184.0) / 1024.0 / 1024.0;
    m_statLabelMemory->setText(QString("数据占用 %1 MB").arg(memoryMb, 0, 'f', 1));
}

QString ScanDialog::formatNumber(int64_t n) {
    return QLocale(QLocale::English).toString(n);
}

QString ScanDialog::formatSize(int64_t bytes) {
    if (bytes == 0) return "0 B";
    const QStringList units = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unit < units.size() - 1) {
        size /= 1024.0;
        unit++;
    }
    return QString("%1 %2").arg(size, 0, 'f', 2).arg(units[unit]);
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
        if (QFile::rename(oldPath, newPath)) m_tableModel->triggerSearch();
        else QMessageBox::warning(this, "错误", "重命名失败，请检查文件是否被占用。");
    }
}

void ScanDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier) { m_resultView->selectAll(); return; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_searchEdit->hasFocus() || m_extEdit->hasFocus()) {
            onTriggerSearch();
        } else {
            auto index = m_resultView->currentIndex();
            if (index.isValid()) onItemDoubleClicked(index);
        }
        return;
    }
    FramelessDialog::keyPressEvent(event);
}

bool ScanDialog::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == m_searchEdit || watched == m_extEdit) && event->type() == QEvent::MouseButtonDblClick) {
        bool isQuery = (watched == m_searchEdit);
        const QStringList& history = isQuery ? m_config.queryHistory : m_config.extHistory;
        
        if (!history.isEmpty()) {
            QMenu menu(this);
            menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");
            
            for (const QString& item : history) {
                menu.addAction(item, [this, isQuery, item]() {
                    if (isQuery) m_searchEdit->setText(item);
                    else m_extEdit->setText(item);
                    onTriggerSearch();
                });
            }
            
            menu.addSeparator();
            menu.addAction("清空历史记录", [this, isQuery]() {
                if (isQuery) m_config.queryHistory.clear();
                else m_config.extHistory.clear();
                m_config.save();
            });
            
            menu.exec(static_cast<QWidget*>(watched)->mapToGlobal(QPoint(0, static_cast<QWidget*>(watched)->height())));
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

} // namespace ArcMeta
