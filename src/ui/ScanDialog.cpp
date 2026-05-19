#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ScanDialog.h"
#include "../core/CacheManager.h"
#include "../core/ScanController.h"
#include "../core/ThumbnailManager.h"
#include <QPainter>
#include <QIcon>
#include "../mft/MftReader.h"
#include "UiHelper.h"
#include "../meta/MetadataManager.h"
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

class MftDataProvider : public IDataProvider {
public:
    int totalCount() const override { return MftReader::instance().totalCount(); }
    QVector<int> search(const QString& query, const ScanFilterState& state) override {
        return MftReader::instance().search(query, state.useRegex, state.caseSensitive, state.extensionList, state.includeHidden, state.includeSystem);
    }
    QString getName(int index) const override { return MftReader::instance().getName(index); }
    QString getFullPath(int index) const override { return MftReader::instance().getFullPath(index); }
    int64_t getSize(int index) const override { return MftReader::instance().getSize(index); }
    int64_t getModifyTime(int index) const override { return MftReader::instance().getModifyTime(index); }
    bool isDirectory(int index) const override { return MftReader::instance().isDirectory(index); }
    QIcon getCachedIcon(const QString& ext, bool isDir) override { return MftReader::instance().getCachedIcon(ext, isDir); }
    bool isMetadataFetched(int index) const override { return MftReader::instance().isMetadataFetched(index); }
    void requestMetadata(int index) override { MftReader::instance().requestMetadata(index); }
};

// --- ScanTableModel Implementation ---

ScanTableModel::ScanTableModel(IDataProvider* provider, QObject* parent)
    : QAbstractTableModel(parent), m_provider(provider) {
    m_throttleTimer = new QTimer(this);
    m_throttleTimer->setInterval(100);
    connect(m_throttleTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingRows.isEmpty()) return;
        
        QList<int> rows = m_pendingRows.values();
        std::sort(rows.begin(), rows.end());
        m_pendingRows.clear();

        int startRow = rows[0];
        int endRow = rows[0];
        for (int i = 1; i < rows.size(); ++i) {
            if (rows[i] == endRow + 1) {
                endRow = rows[i];
            } else {
                emit dataChanged(index(startRow, 0), index(endRow, 3));
                startRow = rows[i];
                endRow = rows[i];
            }
        }
        emit dataChanged(index(startRow, 0), index(endRow, 3));
    });

    connect(&MftReader::instance(), &MftReader::dataChanged, this, [this](int index) {
        if (index == -1) {
            m_pendingRows.clear();
            beginResetModel();
            endResetModel();
            return;
        }
        auto it = m_actualToRow.find(index);
        if (it != m_actualToRow.end()) {
            int row = it->second;
            if (row < m_displayLimit) {
                m_pendingRows.insert(row);
                if (!m_throttleTimer->isActive()) m_throttleTimer->start();
            }
        }
    });
}
ScanTableModel::~ScanTableModel() {
    delete m_provider;
}

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
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: return m_provider->getName(actualIndex);
            case 1: return m_provider->getFullPath(actualIndex);
            case 2: {
                if (m_provider->isDirectory(actualIndex)) return "-";
                int64_t size = m_provider->getSize(actualIndex);
                if (size == 0 && !m_provider->isMetadataFetched(actualIndex)) {
                    m_provider->requestMetadata(actualIndex);
                    return "...";
                }
                if (size < 1024) return QString("%1 B").arg(size);
                if (size < 1024 * 1024) return QString("%1 KB").arg(size / 1024.0, 0, 'f', 2);
                if (size < 1024LL * 1024 * 1024) return QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 2);
                return QString("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
            }
            case 3: {
                int64_t ts = m_provider->getModifyTime(actualIndex);
                if (ts == 0 && !m_provider->isMetadataFetched(actualIndex)) {
                    m_provider->requestMetadata(actualIndex);
                    return "-";
                }
                if (ts == 0) return "-";
                return QDateTime::fromMSecsSinceEpoch(ts).toString("yyyy-MM-dd HH:mm");
            }
        }
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        QString name = m_provider->getName(actualIndex);
        int dotIdx = name.lastIndexOf('.');
        QString ext = (dotIdx != -1) ? name.mid(dotIdx + 1).toLower() : "";
        
        static const QSet<QString> thumbExts = {"psd", "ai", "eps", "jpg", "jpeg", "png", "webp"};
        if (thumbExts.contains(ext) && !m_provider->isDirectory(actualIndex)) {
            QString fullPath = m_provider->getFullPath(actualIndex);
            auto& config = ScanController::instance().config();
            int thumbSize = config.iconSize; // 简化处理，使用配置的图标大小

            QPixmap thumb = ThumbnailManager::instance().getThumbnail(fullPath, thumbSize, this, [this, actualIndex](const QPixmap& /*p*/) {
                auto itRow = m_actualToRow.find(actualIndex);
                if (itRow != m_actualToRow.end()) {
                    int currentRow = itRow->second;
                    if (currentRow < m_displayLimit) {
                        emit dataChanged(index(currentRow, 0), index(currentRow, 0), {Qt::DecorationRole});
                    }
                }
            });

            if (!thumb.isNull()) return thumb;
        }
        return m_provider->getCachedIcon(ext, m_provider->isDirectory(actualIndex));
    } else if (role == Qt::ForegroundRole) {
        std::wstring path = m_provider->getFullPath(actualIndex).toStdWString();
        auto meta = MetadataManager::instance().getMeta(path);
        if (!meta.color.empty()) {
            QColor tagC = UiHelper::parseColorName(QString::fromStdWString(meta.color));
            if (tagC.isValid()) return tagC;
        }
        if (m_provider->isDirectory(actualIndex)) return QColor("#3498db");
    } else if (role == Qt::ToolTipRole) {
        std::wstring path = m_provider->getFullPath(actualIndex).toStdWString();
        auto meta = MetadataManager::instance().getMeta(path);
        QString tip = QLatin1String("路径: ") + QString::fromStdWString(path);
        if (!meta.note.empty()) tip += QLatin1String("\n备注: ") + QString::fromStdWString(meta.note);
        if (!meta.tags.isEmpty()) tip += QLatin1String("\n标签: ") + meta.tags.join(QLatin1String(", "));
        return tip;
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

Qt::ItemFlags ScanTableModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == 0) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

bool ScanTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole || index.column() != 0) return false;
    
    int row = index.row();
    if (row < 0 || row >= m_filteredIndices.size()) return false;
    
    int actualIndex = m_filteredIndices[row];
    
    QString oldName = m_provider->getName(actualIndex);
    QString newName = value.toString().trimmed();
    if (newName.isEmpty() || newName == oldName) return false;
    
    QString oldPath = m_provider->getFullPath(actualIndex);
    QFileInfo fi(oldPath);
    QString newPath = fi.absolutePath() + QLatin1String("/") + newName;
    
    if (QFile::rename(oldPath, newPath)) {
        return true;
    } else {
        QMessageBox::warning(nullptr, "重命名失败", "无法重命名文件，请检查文件是否被占用或是否有权限。");
        return false;
    }
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
        return m_provider->search(text, state);
    });

    disconnect(&m_filterWatcher, &QFutureWatcher<QVector<int>>::finished, this, nullptr);

    connect(&m_filterWatcher, &QFutureWatcher<QVector<int>>::finished, this, [this, timer]() {
        if (m_filterWatcher.isCanceled()) {
            delete timer;
            return;
        }

        beginResetModel();
        m_filteredIndices = m_filterWatcher.result();
        
        m_actualToRow.clear();
        m_actualToRow.reserve(m_filteredIndices.size());
        for (int i = 0; i < m_filteredIndices.size(); ++i) {
            m_actualToRow[m_filteredIndices[i]] = i;
        }

        m_displayLimit = 0;
        endResetModel();
        if (canFetchMore(QModelIndex())) fetchMore(QModelIndex());

        ScanDialog* dlg = qobject_cast<ScanDialog*>(parent());
        if (dlg) {
            dlg->m_lastSearchMs = timer->elapsed();
        }
        delete timer;

        emit filterFinished(m_filteredIndices.size());
    });

    m_filterWatcher.setFuture(future);
}

bool ScanTableModel::canFetchMore(const QModelIndex& parent) const {
    if (parent.isValid()) return false;
    return m_displayLimit < m_filteredIndices.size();
}

void ScanTableModel::fetchMore(const QModelIndex& parent) {
    if (parent.isValid()) return;
    int remainder = m_filteredIndices.size() - m_displayLimit;
    int itemsToFetch = std::min(100, remainder);

    if (itemsToFetch <= 0) return;

    beginInsertRows(QModelIndex(), m_displayLimit, m_displayLimit + itemsToFetch - 1);
    m_displayLimit += itemsToFetch;
    endInsertRows();
}

void ScanTableModel::sort(int column, Qt::SortOrder order) {
    if (m_filteredIndices.isEmpty()) return;
    
    std::sort(m_filteredIndices.begin(), m_filteredIndices.end(), [&](int a, int b) {
        bool less = false;
        switch (column) {
            case 0: less = QString::compare(m_provider->getName(a), m_provider->getName(b), Qt::CaseInsensitive) < 0; break;
            case 1: less = QString::compare(m_provider->getFullPath(a), m_provider->getFullPath(b), Qt::CaseInsensitive) < 0; break;
            case 2: less = m_provider->getSize(a) < m_provider->getSize(b); break;
            case 3: less = m_provider->getModifyTime(a) < m_provider->getModifyTime(b); break;
            default: return false;
        }
        return (order == Qt::AscendingOrder) ? less : !less;
    });
    
    m_actualToRow.clear();
    for (int i = 0; i < m_filteredIndices.size(); ++i) m_actualToRow[m_filteredIndices[i]] = i;
    
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

Qt::DropActions ScanTableModel::supportedDragActions() const {
    return Qt::CopyAction;
}

QMimeData* ScanTableModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* data = new QMimeData();
    QList<QUrl> urls;
    QSet<int> seen;
    for (const QModelIndex& idx : indexes) {
        if (idx.column() != 0) continue;
        int row = idx.row();
        if (row < 0 || row >= m_filteredIndices.size()) continue;
        int actualIdx = m_filteredIndices[row];
        if (seen.contains(actualIdx)) continue;
        seen.insert(actualIdx);
        QString path = m_provider->getFullPath(actualIdx);
        if (!path.isEmpty()) urls << QUrl::fromLocalFile(path);
    }
    data->setUrls(urls);
    return data;
}

// --- ScanDialog Implementation ---

ScanDialog::ScanDialog(QWidget* parent)
    : FramelessDialog("FERREX-META", parent) 
{
    ScanController::instance().loadConfig();
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
            
            QLabel* brandLabel = new QLabel("FERREX-META");
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

    QFile qssFile(":/qss/scan_dialog.qss");
    if (qssFile.open(QFile::ReadOnly)) {
        this->setStyleSheet(qssFile.readAll());
    } else {
        // Fallback for development if resource is not compiled yet
        QFile devQss("resources/qss/scan_dialog.qss");
        if (devQss.open(QFile::ReadOnly)) {
            this->setStyleSheet(devQss.readAll());
        }
    }

    auto& config = ScanController::instance().config();
    m_viewStack->setCurrentIndex(config.viewMode);
    if (config.viewMode == 0) {
        m_resultView->verticalHeader()->setDefaultSectionSize(32);
    } else {
        m_resultView->verticalHeader()->setDefaultSectionSize(config.iconSize + 10);
    }
    if (config.viewMode == 1) { // 图标模式
        m_iconView->setIconSize(QSize(config.iconSize, config.iconSize));
        m_iconView->setGridSize(QSize(config.iconSize + 14, config.iconSize + 44));
    }
    
    m_resultView->horizontalHeader()->setSortIndicator(config.sortColumn, static_cast<Qt::SortOrder>(config.sortOrder));
    m_tableModel->sort(config.sortColumn, static_cast<Qt::SortOrder>(config.sortOrder));

    if (m_pinBtn) {
        disconnect(m_pinBtn, &QPushButton::toggled, nullptr, nullptr);
        connect(m_pinBtn, &QPushButton::toggled, this, [this](bool checked) {
            m_pinBtn->setIcon(UiHelper::getIcon(checked ? "pin_vertical" : "pin_tilted", 
                                                checked ? QColor("#FF551C") : QColor("#CCCCCC"), 18));
            HWND hwnd = reinterpret_cast<HWND>(winId());
            if (checked) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            } else {
                SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
        });
    }

    connect(&ScanController::instance(), &ScanController::driveProbeFinished, this, [this](const QVector<DriveInfo>& drives) {
            QLayoutItem* item;
            while ((item = m_driveLayout->takeAt(0)) != nullptr) {
                if (item->widget()) item->widget()->deleteLater();
                delete item;
            }
            m_driveButtonMap.clear();

            auto& config = ScanController::instance().config();
            for (const auto& info : drives) {
                if (!info.hasMedia || !info.isNtfs) continue;
                if (config.ignoredDrives.contains(info.letter)) continue;

                QString label = info.label.isEmpty() ? "本地磁盘" : info.label;
                QString btnText = QString("%1 (%2)").arg(info.letter).arg(label);

                QPushButton* btn = new QPushButton(btnText);
                btn->setCheckable(true);
                btn->setFixedHeight(24);
                m_driveButtonMap[info.letter] = btn;

                connect(btn, &QPushButton::clicked, this, [this, letter = info.letter]() {
                    auto& config = ScanController::instance().config();
                    bool isSelected = false;
                    if (config.activeDrives.contains(letter)) {
                        if (config.activeDrives.size() > 1) {
                            config.activeDrives.remove(letter);
                        } else {
                            isSelected = true; // 保持选中
                        }
                    } else {
                        config.activeDrives.insert(letter);
                        isSelected = true;
                    }

                    updateDriveButtonStyles();

                    QStringList activeList;
                    for (const QString& d : config.activeDrives) activeList << d;
                    ScanController::instance().updateActiveDrives(activeList);

                    if (isSelected && !MftReader::instance().isDriveIndexed(letter)) {
                        onStartScan();
                    } else {
                        onTriggerSearch();
                    }
                });

                btn->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(btn, &QPushButton::customContextMenuRequested, this, [this, letter = info.letter](const QPoint& pos) {
                    onDriveContextMenu(letter, pos);
                });

                m_driveLayout->addWidget(btn);
            }
            m_driveLayout->addStretch();
            updateDriveButtonStyles();
    });

    connect(&ScanController::instance(), &ScanController::statusUpdated, this, &ScanDialog::updateStatus);

    QTimer::singleShot(100, this, [this]() {
        updateStatus("正在载入本地快照...");
        QPointer<ScanDialog> weakThis(this);
        (void)QtConcurrent::run([weakThis]() {
            bool ok = MftReader::instance().loadFromCache();
            QMetaObject::invokeMethod(weakThis.data(), [weakThis, ok]() {
                if (!weakThis) return;
                if (ok) {
                    weakThis->updateStatus("就绪");
                    weakThis->m_tableModel->setFilterText("");
                    ScanController::instance().requestDriveProbe(true);
                } else {
                    weakThis->updateStatus("未检测到快照，全自动初始化...");
                    ScanController::instance().requestDriveProbe(true);
                    weakThis->onStartScan();
                }
            });
        });
    });
}

ScanDialog::~ScanDialog() {
}

void ScanDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(m_contentArea);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    auto* driveScroll = new QScrollArea();
    driveScroll->setFixedHeight(45);
    driveScroll->setWidgetResizable(true);
    driveScroll->setFrameShape(QFrame::NoFrame);
    driveScroll->setStyleSheet("background: #252526; border: 1px solid #333; border-radius: 4px;");

    m_driveContainer = new QWidget();
    m_driveContainer->setObjectName("DriveContainer");
    m_driveContainer->setAttribute(Qt::WA_StyledBackground, true);
    m_driveLayout = new QHBoxLayout(m_driveContainer);
    m_driveLayout->setContentsMargins(5, 0, 5, 0);
    m_driveLayout->setSpacing(10);
    driveScroll->setWidget(m_driveContainer);

    auto* topControl = new QHBoxLayout();
    topControl->setContentsMargins(0, 0, 0, 0);
    topControl->addWidget(driveScroll, 1);
    mainLayout->addLayout(topControl);

    auto* optionRow = new QHBoxLayout();
    optionRow->setContentsMargins(0, 0, 0, 0);
    optionRow->setSpacing(15);
    
    m_checkRegex = new QCheckBox("正则");
    m_checkCase = new QCheckBox("大小写");
    m_checkHidden = new QCheckBox("隐藏");
    m_checkSystem = new QCheckBox("系统");
    for (auto* cb : {m_checkRegex, m_checkCase, m_checkHidden, m_checkSystem}) {
        cb->setChecked(cb != m_checkCase && cb != m_checkHidden && cb != m_checkSystem);
        connect(cb, &QCheckBox::toggled, this, &ScanDialog::onFilterOptionChanged);
        optionRow->addWidget(cb);
    }
    optionRow->addStretch();
    mainLayout->addLayout(optionRow);

    auto* searchContainer = new QWidget();
    searchContainer->setObjectName("SearchContainer");
    searchContainer->setAttribute(Qt::WA_StyledBackground, true);
    auto* searchVLayout = new QVBoxLayout(searchContainer);
    searchVLayout->setContentsMargins(0, 0, 0, 0);
    searchVLayout->setSpacing(10);

    auto* searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(0, 0, 0, 0); 
    searchRow->setSpacing(10); 

    m_searchEdit = new QLineEdit();
    m_searchEdit->setObjectName("mainSearchEdit");
    m_searchEdit->setPlaceholderText("输入文件名 / 关键词...");
    m_searchEdit->setFixedHeight(36);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->installEventFilter(this);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() {
        if (!m_searchThrottleTimer) {
            m_searchThrottleTimer = new QTimer(this);
            m_searchThrottleTimer->setSingleShot(true);
            m_searchThrottleTimer->setInterval(200);
            connect(m_searchThrottleTimer, &QTimer::timeout, this, &ScanDialog::onTriggerSearch);
        }
        m_searchThrottleTimer->start();
    });
    searchRow->addWidget(m_searchEdit, 1);

    m_extEdit = new QLineEdit();
    m_extEdit->setObjectName("extSearchEdit");
    m_extEdit->setPlaceholderText("后缀");
    m_extEdit->setFixedWidth(120); 
    m_extEdit->setFixedHeight(36);
    m_extEdit->installEventFilter(this);
    connect(m_extEdit, &QLineEdit::returnPressed, this, &ScanDialog::onTriggerSearch);
    searchRow->addWidget(m_extEdit);

    m_searchBtn = new QPushButton("搜索");
    m_searchBtn->setObjectName("searchIconButton");
    m_searchBtn->setFixedWidth(80);
    m_searchBtn->setFixedHeight(36); 
    m_searchBtn->setCursor(Qt::PointingHandCursor);
    m_searchBtn->setIcon(UiHelper::getIcon("search", QColor("#000000"), 18));
    m_searchBtn->setIconSize(QSize(18, 18));
    connect(m_searchBtn, &QPushButton::clicked, this, &ScanDialog::onTriggerSearch);
    searchRow->addWidget(m_searchBtn);

    m_prevBtn = new QPushButton("上一页");
    m_prevBtn->setObjectName("PageBtn");
    m_prevBtn->setFixedWidth(60);
    m_prevBtn->setFixedHeight(36);
    m_prevBtn->setCursor(Qt::PointingHandCursor);
    
    m_pageLabel = new QLabel("第 1 页");
    m_pageLabel->setStyleSheet("color: #7A8F9E; font-size: 11px; margin: 0 5px;");

    m_nextBtn = new QPushButton("下一页");
    m_nextBtn->setObjectName("PageBtn");
    m_nextBtn->setFixedWidth(60);
    m_nextBtn->setFixedHeight(36);
    m_nextBtn->setCursor(Qt::PointingHandCursor);

    searchRow->addWidget(m_prevBtn);
    searchRow->addWidget(m_pageLabel);
    searchRow->addWidget(m_nextBtn);
    searchVLayout->addLayout(searchRow);

    m_progressBar = new QProgressBar();
    m_progressBar->setObjectName("ScanProgressBar");
    m_progressBar->setFixedHeight(2);
    m_progressBar->setTextVisible(false);
    m_progressBar->hide();
    searchVLayout->addWidget(m_progressBar);

    mainLayout->addWidget(searchContainer);

    m_resultView = new QTableView();
    m_resultView->setObjectName("ScanResultView");
    m_resultView->verticalHeader()->setDefaultSectionSize(30);
    m_tableModel = new ScanTableModel(new MftDataProvider(), this);
    m_resultView->setModel(m_tableModel);
    m_resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    
    m_resultView->horizontalHeader()->setStretchLastSection(false); 
    m_resultView->horizontalHeader()->setMinimumSectionSize(60);
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
    
    m_resultView->setDragEnabled(true);
    m_resultView->setDragDropMode(QAbstractItemView::DragOnly);
    m_resultView->setDefaultDropAction(Qt::CopyAction);

    m_resultView->setShowGrid(false);
    m_resultView->setAlternatingRowColors(true);
    
    connect(m_resultView, &QTableView::customContextMenuRequested, this, &ScanDialog::onCustomContextMenu);
    connect(m_resultView, &QTableView::doubleClicked, this, &ScanDialog::onItemDoubleClicked);
    connect(m_resultView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ScanDialog::onSelectionChanged);
    
    m_resultView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    
    
    m_viewStack = new QStackedWidget();
    m_viewStack->setObjectName("ViewStack");
    m_viewStack->addWidget(m_resultView);
    
    m_iconView = new QListView();
    m_iconView->setObjectName("ScanIconView");
    m_iconView->setModel(m_tableModel);
    m_iconView->setViewMode(QListView::IconMode);
    m_iconView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_iconView->setResizeMode(QListView::Adjust);
    m_iconView->setMovement(QListView::Static);
    m_iconView->setSpacing(7);
    m_iconView->setWordWrap(true);
    m_iconView->setTextElideMode(Qt::ElideMiddle);
    m_iconView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_iconView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    
    m_iconView->setDragEnabled(true);
    m_iconView->setDragDropMode(QAbstractItemView::DragOnly);
    m_iconView->setDefaultDropAction(Qt::CopyAction);
    
    connect(m_iconView, &QListView::doubleClicked, this, &ScanDialog::onItemDoubleClicked);
    connect(m_iconView, &QListView::customContextMenuRequested, this, &ScanDialog::onCustomContextMenu);
    connect(m_iconView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ScanDialog::onSelectionChanged);
    
    m_viewStack->addWidget(m_iconView);
    m_viewStack->setCurrentIndex(0);
    
    mainLayout->addWidget(m_viewStack);

    m_prevBtn->hide();
    m_pageLabel->hide();
    m_nextBtn->hide();

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
        m_currentPage = 0;
        updateStatusBar();
    });
}

void ScanDialog::updateDriveButtonStyles() {
    auto& config = ScanController::instance().config();
    auto drives = ScanController::instance().cachedDriveInfos();
    for (auto it = m_driveButtonMap.begin(); it != m_driveButtonMap.end(); ++it) {
        bool isActive = config.activeDrives.contains(it.key());
        bool isDefault = config.defaultDrives.contains(it.key());
        
        QPushButton* btn = it.value();
        btn->setProperty("isActive", isActive);
        btn->setProperty("isDefault", isDefault);
        
        btn->style()->unpolish(btn);
        btn->style()->polish(btn);
        
        QString label = "";
        for (const auto& info : drives) { if (info.letter == it.key()) { label = info.label; break; } }
        btn->setText(QString("%1%2 (%3)").arg(isDefault ? "★ " : "").arg(it.key()).arg(label.isEmpty() ? "本地磁盘" : label));
    }
}

void ScanDialog::onDriveContextMenu(const QString& drive, const QPoint& /*pos*/) {
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");
    
    auto& config = ScanController::instance().config();
    bool isDefault = config.defaultDrives.contains(drive);
    menu.addAction(isDefault ? "取消默认选项" : "设为默认选项", [this, drive, isDefault]() {
        auto& config = ScanController::instance().config();
        if (isDefault) config.defaultDrives.remove(drive);
        else config.defaultDrives.insert(drive);
        ScanController::instance().saveConfig();
        updateDriveButtonStyles();
    });
    
    menu.addAction("忽略此驱动器", [this, drive]() {
        auto& config = ScanController::instance().config();
        config.ignoredDrives.insert(drive);
        config.activeDrives.remove(drive);
        ScanController::instance().saveConfig();
        ScanController::instance().requestDriveProbe(true);
        onStartScan();
    });
    
    menu.exec(QCursor::pos());
}

void ScanDialog::onIgnoredDriveContextMenu(const QString& drive, const QPoint& pos) {
    Q_UNUSED(pos);
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");
    menu.addAction("恢复驱动器", [this, drive]() {
        auto& config = ScanController::instance().config();
        config.ignoredDrives.remove(drive);
        ScanController::instance().saveConfig();
        ScanController::instance().requestDriveProbe(true);
    });
    menu.exec(QCursor::pos());
}

void ScanDialog::onCustomContextMenu(const QPoint& pos) {
    QAbstractItemView* activeView = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_resultView) : static_cast<QAbstractItemView*>(m_iconView);
    
    QModelIndex indexAtPos = activeView->indexAt(pos);
    QModelIndexList selectedRows;

    if (indexAtPos.isValid()) {
        if (!activeView->selectionModel()->isSelected(indexAtPos)) {
            activeView->selectionModel()->select(indexAtPos, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        
        auto allSelected = activeView->selectionModel()->selectedIndexes();
        for (const auto& idx : allSelected) {
            if (idx.column() == 0) selectedRows.append(idx);
        }
    } else {
        selectedRows.clear();
    }
    
    QMenu menu(this);
    menu.setStyleSheet("QMenu { background: #1A1A1A; color: #CCC; border: 1px solid #333; } QMenu::item:selected { background: #232D37; color: #FFF; }");

    if (!selectedRows.isEmpty()) {
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
        menu.addSeparator();
        
        if (count == 1) {
            std::wstring path = m_tableModel->data(m_tableModel->index(selectedRows.first().row(), 1)).toString().toStdWString();
            auto meta = MetadataManager::instance().getMeta(path);

            QMenu* ratingMenu = menu.addMenu("评分");
            for (int i = 0; i <= 5; ++i) {
                QString star = (i == 0) ? "无评分" : QString(i, QChar(0x2605));
                QAction* act = ratingMenu->addAction(star, [this, path, i]() {
                    MetadataManager::instance().setRating(path, i);
                    m_tableModel->triggerSearch();
                });
                if (meta.rating == i) act->setCheckable(true), act->setChecked(true);
            }

            QMenu* labelMenu = menu.addMenu("标记颜色");
            
            QString ext = QFileInfo(QString::fromStdWString(path)).suffix().toLower();
            if (UiHelper::isGraphicsFile(ext)) {
                labelMenu->addAction("解析颜色...", [this, path]() {
                    QPointer<ScanDialog> weakThis(this);
                    (void)QtConcurrent::run([weakThis, path]() {
                        auto palette = UiHelper::extractPalette(QString::fromStdWString(path));
                        if (palette.isEmpty()) return;
                        
                        QColor dominant = UiHelper::quantizeColor(palette.first().first);
                        QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, dominant, palette]() {
                            if (weakThis) {
                                MetadataManager::instance().setColor(path, dominant.name().toUpper().toStdWString());
                                MetadataManager::instance().setPalettes(path, palette);
                                weakThis->m_tableModel->triggerSearch();
                            }
                        });
                    });
                });
                labelMenu->addSeparator();
            }

            struct ColorItem { QString value; QString label; QColor preview; };
            QList<ColorItem> colorItems = {
                {"", "默认", QColor("#888780")},
                {"#E04040", "红色", QColor("#E24B4A")},
                {"#E09020", "橙色", QColor("#EF9F27")},
                {"#F0C070", "黄色", QColor("#FAC775")},
                {"#609020", "绿色", QColor("#639922")},
                {"#109070", "青色", QColor("#1D9E75")},
                {"#3080D0", "蓝色", QColor("#378ADD")},
                {"#7070D0", "紫色", QColor("#7F77DD")},
                {"#505050", "灰色", QColor("#5F5E5A")}
            };
            for (const auto& ci : colorItems) {
                QAction* act = labelMenu->addAction(ci.label);
                connect(act, &QAction::triggered, this, [this, path, value = ci.value]() {
                    MetadataManager::instance().setColor(path, value.toStdWString());
                    m_tableModel->triggerSearch();
                });
                if (meta.color == ci.value.toStdWString()) {
                    act->setCheckable(true);
                    act->setChecked(true);
                }
                QPixmap pix(12, 12); pix.fill(Qt::transparent);
                QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing);
                p.setBrush(ci.preview); p.setPen(Qt::NoPen);
                p.drawEllipse(0, 0, 12, 12);
                act->setIcon(QIcon(pix));
            }

            menu.addAction(meta.pinned ? "取消置顶" : "置顶文件", [this, path, meta]() {
                MetadataManager::instance().setPinned(path, !meta.pinned);
                m_tableModel->triggerSearch();
            });

            menu.addAction("编辑标签...", [this, path, meta]() {
                bool ok;
                QString text = QInputDialog::getText(this, "编辑标签", "标签 (逗号分隔):", QLineEdit::Normal, meta.tags.join(","), &ok);
                if (ok) {
                    MetadataManager::instance().setTags(path, text.split(",", Qt::SkipEmptyParts));
                    m_tableModel->triggerSearch();
                }
            });

            menu.addAction("编辑备注...", [this, path, meta]() {
                bool ok;
                QString text = QInputDialog::getMultiLineText(this, "编辑备注", "备注内容:", QString::fromStdWString(meta.note), &ok);
                if (ok) {
                    MetadataManager::instance().setNote(path, text.toStdWString());
                    m_tableModel->triggerSearch();
                }
            });

            menu.addAction(meta.encrypted ? "解密文件" : "加密文件", [this, path, meta]() {
                MetadataManager::instance().setEncrypted(path, !meta.encrypted);
                m_tableModel->triggerSearch();
            });

            menu.addSeparator();
        }
        
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

        menu.addSeparator();
    }

    
    QMenu* viewMenu = menu.addMenu("视图(V)");
    QActionGroup* viewGroup = new QActionGroup(this);
    
    auto addViewAction = [this, viewMenu, viewGroup](const QString& text, const QString& shortcut, int stackIdx, int iconSize) {
        QAction* act = viewMenu->addAction(text);
        act->setShortcut(QKeySequence(shortcut));
        act->setCheckable(true);
        viewGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, stackIdx, iconSize]() {
            auto& config = ScanController::instance().config();
            m_viewStack->setCurrentIndex(stackIdx);
            config.viewMode = stackIdx;
            if (stackIdx == 1) { // 图标模式
                m_iconView->setIconSize(QSize(iconSize, iconSize));
                m_iconView->setGridSize(QSize(iconSize + 14, iconSize + 44));
                config.iconSize = iconSize;
            }
            if (stackIdx == 0) { // 详情模式
                m_resultView->verticalHeader()->setDefaultSectionSize(32);
            } else {
                m_resultView->verticalHeader()->setDefaultSectionSize(iconSize + 10);
            }
            ScanController::instance().saveConfig();
        });
        return act;
    };

    QAction* xLargeAction = addViewAction("超大图标(X)", "Ctrl+Shift+1", 1, 192);
    QAction* largeAction = addViewAction("大图标(L)", "Ctrl+Shift+2", 1, 128);
    QAction* mediumAction = addViewAction("中图标(M)", "Ctrl+Shift+3", 1, 64);
    
    viewMenu->addSeparator();
    
    QAction* detailsAction = addViewAction("详情(D)", "Ctrl+Shift+6", 0, 0);
    
    if (m_viewStack->currentIndex() == 0) detailsAction->setChecked(true);
    else {
        int currentSize = m_iconView->iconSize().width();
        if (currentSize == 192) xLargeAction->setChecked(true);
        else if (currentSize == 128) largeAction->setChecked(true);
        else mediumAction->setChecked(true);
    }
    
    QMenu* sortMenu = menu.addMenu("排序(S)");
    QStringList sortOptions = {"名称", "路径", "大小", "修改日期"};
    for (int i = 0; i < sortOptions.size(); ++i) {
        QAction* act = sortMenu->addAction(sortOptions[i]);
        connect(act, &QAction::triggered, this, [this, i]() {
            auto& config = ScanController::instance().config();
            Qt::SortOrder order = m_resultView->horizontalHeader()->sortIndicatorOrder();
            m_resultView->sortByColumn(i, order);
            config.sortColumn = i;
            config.sortOrder = static_cast<int>(order);
            ScanController::instance().saveConfig();
        });
    }
    sortMenu->addSeparator();
    QAction* ascAction = sortMenu->addAction("升序(A)");
    QAction* descAction = sortMenu->addAction("降序(D)");
    connect(ascAction, &QAction::triggered, this, [this]() { 
        auto& config = ScanController::instance().config();
        m_resultView->sortByColumn(m_resultView->horizontalHeader()->sortIndicatorSection(), Qt::AscendingOrder); 
        config.sortOrder = 0;
        ScanController::instance().saveConfig();
    });
    connect(descAction, &QAction::triggered, this, [this]() { 
        auto& config = ScanController::instance().config();
        m_resultView->sortByColumn(m_resultView->horizontalHeader()->sortIndicatorSection(), Qt::DescendingOrder); 
        config.sortOrder = 1;
        ScanController::instance().saveConfig();
    });

    QAction* refreshAction = menu.addAction("刷新(R)");
    refreshAction->setShortcut(QKeySequence(Qt::Key_F5));
    connect(refreshAction, &QAction::triggered, this, &ScanDialog::onTriggerSearch);

    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender());
    if (view) menu.exec(view->viewport()->mapToGlobal(pos));
}

void ScanDialog::onItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    
    QString path = m_tableModel->data(m_tableModel->index(index.row(), 1)).toString();
    ShellExecuteW(NULL, L"open", reinterpret_cast<const wchar_t*>(path.utf16()), NULL, NULL, SW_SHOWNORMAL);
}

void ScanDialog::onSelectionChanged() {
    auto view = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_resultView) : static_cast<QAbstractItemView*>(m_iconView);
    auto selectedRows = view->selectionModel()->selectedRows();
    if (selectedRows.isEmpty()) { m_selectionLabel->clear(); return; }
    
    int64_t totalSize = 0;
    auto& reader = MftReader::instance();
    for (const auto& index : selectedRows) {
        int actualIdx = m_tableModel->data(index, Qt::UserRole).toInt();
        if (!reader.isDirectory(actualIdx)) totalSize += reader.getSize(actualIdx);
    }
    m_selectionLabel->setText(QString("已选择 %1 项 | 合计大小: %2").arg(selectedRows.size()).arg(formatSize(totalSize)));
}

void ScanDialog::onStartScan() {
    auto& config = ScanController::instance().config();
    QStringList selectedDrives;
    for (const auto& d : config.activeDrives) selectedDrives << (d + QLatin1String("\\"));
    if (selectedDrives.isEmpty()) { onTriggerSearch(); return; }

    ScanController::instance().requestScan(selectedDrives);
    connect(&ScanController::instance(), &ScanController::scanFinished, this, &ScanDialog::onTriggerSearch, Qt::UniqueConnection);
}

void ScanDialog::onTriggerSearch() {
    auto& config = ScanController::instance().config();
    QString q = m_searchEdit->text().trimmed();
    QString e = m_extEdit->text().trimmed();
    
    QTimer::singleShot(10, this, [this, q, e]() {
        auto& config = ScanController::instance().config();
        bool changed = false;
        if (!q.isEmpty() && (config.queryHistory.isEmpty() || config.queryHistory.first() != q)) {
            config.queryHistory.removeAll(q);
            config.queryHistory.prepend(q);
            if (config.queryHistory.size() > 10) config.queryHistory.removeLast();
            changed = true;
        }
        if (!e.isEmpty() && (config.extHistory.isEmpty() || config.extHistory.first() != e)) {
            config.extHistory.removeAll(e);
            config.extHistory.prepend(e);
            if (config.extHistory.size() > 10) config.extHistory.removeLast();
            changed = true;
        }
        if (changed) ScanController::instance().saveConfig();
    });

    QStringList activeList;
    for (const QString& drive : config.activeDrives) activeList << drive;
    ScanController::instance().updateActiveDrives(activeList);

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
    auto view = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_resultView) : static_cast<QAbstractItemView*>(m_iconView);
    auto selectedRows = view->selectionModel()->selectedRows();
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
        m_statLabelMain->setText(QString("共找到 %1 条项目").arg(formatNumber(totalMatch)));
        m_statLabelTime->setText(QString("耗时 %1 ms").arg(m_lastSearchMs));

        if (m_pageLabel) {
            int totalPages = (totalMatch + m_pageSize - 1) / m_pageSize;
            if (totalPages == 0) totalPages = 1;
            m_pageLabel->setText(QString("第 %1 / %2 页").arg(m_currentPage + 1).arg(totalPages));
            m_prevBtn->setEnabled(m_currentPage > 0);
            m_nextBtn->setEnabled((m_currentPage + 1) < totalPages);
        }
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
    auto view = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_resultView) : static_cast<QAbstractItemView*>(m_iconView);
    auto selection = view->selectionModel()->selectedRows();
    if (selection.isEmpty()) return;
    
    view->edit(selection.first());
}

void ScanDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F2) {
        onRenameTriggered();
        return;
    }
    if (event->key() == Qt::Key_F5) {
        onTriggerSearch();
        return;
    }
    if (event->key() == Qt::Key_A && event->modifiers() == Qt::ControlModifier) { 
        auto view = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_resultView) : static_cast<QAbstractItemView*>(m_iconView);
        view->selectAll(); 
        return; 
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (m_searchEdit->hasFocus() || m_extEdit->hasFocus()) {
            onTriggerSearch();
        } else {
            auto view = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_resultView) : static_cast<QAbstractItemView*>(m_iconView);
            auto index = view->currentIndex();
            if (index.isValid()) onItemDoubleClicked(index);
        }
        return;
    }
    handleMetadataShortcut(event);
    FramelessDialog::keyPressEvent(event);
}

void ScanDialog::handleMetadataShortcut(QKeyEvent* event) {
    auto view = (m_viewStack->currentIndex() == 0) ? static_cast<QAbstractItemView*>(m_resultView) : static_cast<QAbstractItemView*>(m_iconView);
    auto selection = view->selectionModel()->selectedRows();
    if (selection.isEmpty()) return;
    
    std::wstring path = m_tableModel->data(m_tableModel->index(selection.first().row(), 1)).toString().toStdWString();
    auto meta = MetadataManager::instance().getMeta(path);

    if (event->modifiers() == Qt::ControlModifier && event->key() >= Qt::Key_0 && event->key() <= Qt::Key_5) {
        int rating = event->key() - Qt::Key_0;
        MetadataManager::instance().setRating(path, rating);
        m_tableModel->triggerSearch();
        return;
    }

    if (event->modifiers() == Qt::AltModifier) {
        if (event->key() == Qt::Key_P) { // 置顶
            MetadataManager::instance().setPinned(path, !meta.pinned);
            m_tableModel->triggerSearch();
        } else if (event->key() == Qt::Key_L) { // 加密
            MetadataManager::instance().setEncrypted(path, !meta.encrypted);
            m_tableModel->triggerSearch();
        } else if (event->key() == Qt::Key_T) { // 标签
            bool ok;
            QString text = QInputDialog::getText(this, "编辑标签", "标签 (逗号分隔):", QLineEdit::Normal, meta.tags.join(","), &ok);
            if (ok) {
                MetadataManager::instance().setTags(path, text.split(",", Qt::SkipEmptyParts));
                m_tableModel->triggerSearch();
            }
        } else if (event->key() == Qt::Key_N) { // 备注
            bool ok;
            QString text = QInputDialog::getMultiLineText(this, "编辑备注", "备注内容:", QString::fromStdWString(meta.note), &ok);
            if (ok) {
                MetadataManager::instance().setNote(path, text.toStdWString());
                m_tableModel->triggerSearch();
            }
        }
    }
}

bool ScanDialog::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == m_searchEdit || watched == m_extEdit) && event->type() == QEvent::MouseButtonDblClick) {
        bool isQuery = (watched == m_searchEdit);
        auto& config = ScanController::instance().config();
        const QStringList& history = isQuery ? config.queryHistory : config.extHistory;
        
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
                auto& config = ScanController::instance().config();
                if (isQuery) config.queryHistory.clear();
                else config.extHistory.clear();
                ScanController::instance().saveConfig();
            });
            
            menu.exec(static_cast<QWidget*>(watched)->mapToGlobal(QPoint(0, static_cast<QWidget*>(watched)->height())));
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

} // namespace ArcMeta
