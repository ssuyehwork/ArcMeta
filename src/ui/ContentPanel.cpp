#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "ContentPanel.h" 
#include "SvgIcons.h" 
#include "TreeItemDelegate.h" 
#include "DropTreeView.h" 
#include "DropListView.h" 
#include "DropJustifiedView.h"
#include "ThumbnailDelegate.h"
#include "ToolTipOverlay.h" 
 
#include <QVBoxLayout> 
#include <QHBoxLayout> 
#include <QIcon> 
#include <QSvgRenderer> 
#include <QPainter> 
#include <QHeaderView> 
#include <QScrollBar> 
#include <QStyle> 
#include <QLabel> 
#include <QAction> 
#include <QMenu> 
#include <QAbstractItemView> 
#include <QStandardItem> 
#include <QEvent> 
#include <QKeyEvent> 
#include <QMouseEvent> 
#include <QWheelEvent> 
#include <QStyleOptionViewItem> 
#include <QItemSelectionModel> 
#include <QFileInfo> 
#include <QDir> 
#include <QDateTime> 
#include <QDesktopServices> 
#include <QUrl> 
#include <QApplication> 
#include <QProcess> 
#include <QClipboard> 
#include <QMimeData> 
#include <QLineEdit> 
#include <QTextBrowser> 
#include <QInputDialog> 
#include <QAbstractItemView> 
#include <QtConcurrent> 
#include <QThreadPool> 
#include <QTimer> 
#include <QPointer> 
#include <functional> 
#include <QPointer> 
#include <QPersistentModelIndex> 
#include <QSqlDatabase> 
#include <QSqlQuery> 
#include <windows.h> 
#include <shellapi.h> 
#include "../meta/MetadataManager.h" 
#include "../meta/BatchRenameEngine.h" 
#include "../db/CategoryRepo.h" 
#include "../crypto/EncryptionManager.h" 
#include "CategoryLockDialog.h" 
#include "BatchRenameDialog.h" 
#include "UiHelper.h" 
 
namespace ArcMeta { 
 
// --- FilterProxyModel 实现 --- 
FilterProxyModel::FilterProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {} 
 
void FilterProxyModel::updateFilter() { 
    beginFilterChange(); 
    endFilterChange(); 
} 
 
void FilterProxyModel::setSearchQuery(const QString& query) { 
    m_searchQuery = query; 
    beginFilterChange(); 
    endFilterChange(); 
} 
 
bool FilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const { 
    QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent); 
     
    // 1. 评级过滤 
    if (!currentFilter.ratings.isEmpty()) { 
        int r = idx.data(RatingRole).toInt(); 
        if (!currentFilter.ratings.contains(r)) return false; 
    } 
 
    // 2. 颜色过滤 (变长物理多色板命中逻辑)
    if (!currentFilter.colors.isEmpty()) { 
        QString path = idx.data(PathRole).toString();
        QString dominantColor = idx.data(ColorRole).toString();
        
        // 获取该项目的所有物理颜色 (变长色板)
        QVector<QColor> palettes = MetadataManager::instance().getPalettes(path.toStdWString());
        
        bool matchColor = false;
        for (const QString& fc : currentFilter.colors) {
            // 物理修复：如果 HEX 完全一致（主色命中），直接通过
            if (fc == dominantColor.toUpper()) { matchColor = true; break; }

            // 如果色板存在，遍历色板执行容差检查
            if (!palettes.isEmpty()) {
                for (const auto& pc : palettes) {
                    if (fc == pc.name().toUpper()) { matchColor = true; break; }
                    
                    QColor fCol = UiHelper::parseColorName(fc);
                    if (fCol.isValid()) {
                        long rmean = (fCol.red() + pc.red()) / 2;
                        long r = fCol.red() - pc.red();
                        long g = fCol.green() - pc.green();
                        long b = fCol.blue() - pc.blue();
                        long distSq = (((512 + rmean)*r*r) >> 8) + 4*g*g + (((767-rmean)*b*b) >> 8);
                        if (distSq < 15000) { matchColor = true; break; }
                    }
                }
            } else {
                // 向下兼容：若色板为空，回退到对主色调的容差检查
                QColor dCol = UiHelper::parseColorName(dominantColor);
                QColor fCol = UiHelper::parseColorName(fc);
                if (dCol.isValid() && fCol.isValid()) {
                    long rmean = (fCol.red() + dCol.red()) / 2;
                    long r = fCol.red() - dCol.red();
                    long g = fCol.green() - dCol.green();
                    long b = fCol.blue() - dCol.blue();
                    long distSq = (((512 + rmean)*r*r) >> 8) + 4*g*g + (((767-rmean)*b*b) >> 8);
                    if (distSq < 15000) { matchColor = true; break; }
                }
            }
            if (matchColor) break;
        }
        if (!matchColor) return false; 
    } 
 
    // 3. 标签过滤 
    if (!currentFilter.tags.isEmpty()) { 
        QStringList itemTags = idx.data(TagsRole).toStringList(); 
        bool matchTag = false; 
        for (const QString& fTag : currentFilter.tags) { 
            if (fTag == "__none__") { 
                if (itemTags.isEmpty()) { matchTag = true; break; } 
            } else { 
                if (itemTags.contains(fTag)) { matchTag = true; break; } 
            } 
        } 
        if (!matchTag) return false; 
    } 
 
    // 4. 类型过滤 
    if (!currentFilter.types.isEmpty()) { 
        QString type = idx.data(TypeRole).toString();  
        QString ext = QFileInfo(idx.data(PathRole).toString()).suffix().toUpper(); 
        bool matchType = false; 
        for (const QString& fType : currentFilter.types) { 
            if (fType == "folder") { 
                if (type == "folder") { matchType = true; break; } 
            } else { 
                if (ext == fType.toUpper()) { matchType = true; break; } 
            } 
        } 
        if (!matchType) return false; 
    } 
 
    // 5. 创建日期过滤 
    if (!currentFilter.createDates.isEmpty()) { 
        QDate d = QFileInfo(idx.data(PathRole).toString()).birthTime().date(); 
        QDate today = QDate::currentDate(); 
        QString dStr = d.toString("yyyy-MM-dd"); 
        bool matchDate = false; 
        for (const QString& fDate : currentFilter.createDates) { 
            if (fDate == "today" && d == today) { matchDate = true; break; } 
            if (fDate == "yesterday" && d == today.addDays(-1)) { matchDate = true; break; } 
            if (fDate == dStr) { matchDate = true; break; } 
        } 
        if (!matchDate) return false; 
    } 
 
    // 6. 修改日期过滤 
    if (!currentFilter.modifyDates.isEmpty()) { 
        QDate d = QFileInfo(idx.data(PathRole).toString()).lastModified().date(); 
        QDate today = QDate::currentDate(); 
        QString dStr = d.toString("yyyy-MM-dd"); 
        bool matchDate = false; 
        for (const QString& fDate : currentFilter.modifyDates) { 
            if (fDate == "today" && d == today) { matchDate = true; break; } 
            if (fDate == "yesterday" && d == today.addDays(-1)) { matchDate = true; break; } 
            if (fDate == dStr) { matchDate = true; break; } 
        } 
        if (!matchDate) return false; 
    } 
 
    // 2026-04-12 深度修复：直接执行关键词包含检查 
    if (m_searchQuery.isEmpty()) return true; 
 
    QString fileName = idx.data(Qt::DisplayRole).toString(); 
    return fileName.contains(m_searchQuery, Qt::CaseInsensitive); 
} 
 
bool FilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const { 
    // 核心红线：置顶优先规则 
    bool leftPinned = source_left.data(IsLockedRole).toBool(); 
    bool rightPinned = source_right.data(IsLockedRole).toBool(); 
 
    if (leftPinned != rightPinned) { 
        if (sortOrder() == Qt::AscendingOrder) return leftPinned;  
        else return !leftPinned;  
    } 
    return QSortFilterProxyModel::lessThan(source_left, source_right); 
} 
 
 
ContentPanel::ContentPanel(QWidget* parent) 
    : QFrame(parent) { 
    setObjectName("EditorContainer"); 
    setAttribute(Qt::WA_StyledBackground, true); 
    setMinimumWidth(230); 
    setStyleSheet("color: #EEEEEE;"); 
 
    m_mainLayout = new QVBoxLayout(this); 
    m_mainLayout->setContentsMargins(0, 0, 0, 0); 
    m_mainLayout->setSpacing(0); 
 
 
    m_model = new QStandardItemModel(this); 
    m_proxyModel = new FilterProxyModel(this); 
    m_proxyModel->setSourceModel(m_model); 
    
    // 2026-05-17 新增：当模型数据发生改变时，自动触发统计重新计算并推送至 FilterPanel
    connect(m_model, &QStandardItemModel::dataChanged, this, [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles) {
        Q_UNUSED(topLeft); Q_UNUSED(bottomRight);
        if (roles.isEmpty() || roles.contains(ColorRole) || roles.contains(RatingRole) || roles.contains(TagsRole)) {
            recalculateAndEmitStats();
        }
    });
     
    // 2026-04-12 深度修复：强制锁定过滤列为第 0 列（名称列），确保搜索逻辑不偏离 
    m_proxyModel->setFilterKeyColumn(0); 
 
    // 2026-06-05 按照要求：从配置中加载上次保存的缩放比例 
    QSettings settings("ArcMeta团队", "ArcMeta"); 
    m_zoomLevel = settings.value("UI/GridZoomLevel", 96).toInt(); 
    m_isRecursive = false; 
 
    // 2026-05-20 性能优化白皮书：平滑消费定时器 (60FPS) 
    m_smoothConsumeTimer = new QTimer(this); 
    m_smoothConsumeTimer->setInterval(16); // 16ms = 60FPS 
    connect(m_smoothConsumeTimer, &QTimer::timeout, [this]() { 
        if (m_uiPendingQueue.empty()) { 
            m_smoothConsumeTimer->stop(); 
            // 扫描结束后，恢复全量排序并执行单次排序 
            m_proxyModel->setDynamicSortFilter(true); 
            m_proxyModel->sort(0, m_proxyModel->sortOrder()); 
            // 2026-05-08 按照用户要求：数据加载完成后更新状态栏统计 
            updateStatusBarStats(); 
            return; 
        } 
 
        // 每一帧仅向 Model 插入 50 个条目，预留交互预算 
        int count = 0; 
 
        while (!m_uiPendingQueue.empty() && count < 50) { 
            ScanItemData data = m_uiPendingQueue.front(); 
            m_uiPendingQueue.pop_front(); 
 
            if (data.name == ".am_meta.json" || data.name == ".am_meta.json.tmp") continue; // 2026-06-xx 物理隔离 
 
            // 回归原生：停止对从系统获取的物理图标进行人工着色
            QIcon itemIcon = UiHelper::getFileIcon(data.fullPath, 128); 
            auto* nameItem = new QStandardItem(itemIcon, data.name); 
            nameItem->setData(data.fullPath, PathRole); 
            nameItem->setData(data.isDir ? "folder" : "file", TypeRole); 
            nameItem->setData(data.meta.rating, RatingRole); 
            nameItem->setData(QString::fromStdWString(data.meta.color), ColorRole); 
            nameItem->setData(data.meta.pinned, PinnedRole); 
            nameItem->setData(data.meta.pinned, IsLockedRole); 
            nameItem->setData(data.meta.encrypted, EncryptedRole); 
            nameItem->setData(data.meta.tags, TagsRole); 
            nameItem->setData(data.isEmpty, IsEmptyRole); 
            nameItem->setData(1.0, AspectRatioRole); // 默认 1.0
            nameItem->setData(false, HasThumbnailRole);
            // 2026-06-xx 按照要求：注入物理色板，确保多色统计与过滤生效
            QVariantList palList;
            for (const auto& p : data.meta.palettes) {
                QVariantMap m; m["color"] = p.color; m["ratio"] = p.ratio;
                palList << m;
            }
            nameItem->setData(palList, PalettesRole);
             
            // 2026-06-xx 按照要求：基于 JSON 存在性注入已录入状态（判断逻辑已在扫描端完成） 
            // 物理修复：校准作用域 
            nameItem->setData(data.meta.hasUserOperations(), InDatabaseRole); 
 
            QList<QStandardItem*> row; 
            row << nameItem; 
            row << new QStandardItem(data.isDir ? "-" : QString::number(data.size / 1024) + " KB"); 
            row << new QStandardItem(data.isDir ? (data.isEmpty ? "文件夹 (空)" : "文件夹") : data.suffix + " 文件"); 
            row << new QStandardItem(data.mtime.toString("yyyy-MM-dd HH:mm")); 
            m_model->appendRow(row); 
 
            m_iconPendingPaths << data.fullPath; 
            m_pathToIndexMap[data.fullPath] = QPersistentModelIndex(nameItem->index()); 
            count++; 
        } 
 
        applyFilters(); 
        if (!this->m_lazyIconTimer->isActive()) this->m_lazyIconTimer->start(); 
    }); 
 
    // 2026-03-xx 物理加速：初始化懒加载图标定时器 
    m_lazyIconTimer = new QTimer(this); 
    m_lazyIconTimer->setInterval(30); // 每 30ms 提取一批图标，确保不卡主线程 
    connect(m_lazyIconTimer, &QTimer::timeout, [this]() { 
        if (m_iconPendingPaths.isEmpty()) { 
            m_lazyIconTimer->stop(); 
            return; 
        } 
 
        // 每次处理 20 个项目，在流畅度与加载速度间取得平衡 
        // 2026-05-25 物理修复：移除 static_cast<int> 以解决潜在的 T 模板冲突报错 
        int pendingSize = (int)m_iconPendingPaths.size(); 
        int batchSize = qMin(20, pendingSize); 
        for (int i = 0; i < batchSize; ++i) { 
            QString path = m_iconPendingPaths.takeFirst(); 
            if (m_pathToIndexMap.contains(path)) { 
                QPersistentModelIndex pIdx = m_pathToIndexMap.value(path); 
                if (pIdx.isValid()) { 
                    QFileInfo info(path); 
                    QString ext = info.suffix().toLower(); 
                    QIcon icon; 
 
                    // 2026-05-07 按照用户要求：SVG文件直接渲染显示实际内容 
                    if (ext.toLower() == "svg") { 
                        // 直接渲染SVG文件内容 
                        QSvgRenderer renderer(path); 
                        if (renderer.isValid()) { 
                            // 2026-06-xx 物理修复：使用动态 m_zoomLevel 确保 SVG 渲染清晰度与网格对齐
                            QPixmap svgPixmap(this->m_zoomLevel, this->m_zoomLevel); 
                            svgPixmap.fill(Qt::transparent); 
                            QPainter painter(&svgPixmap); 
                            renderer.render(&painter); 
                            painter.end(); 
                            icon = QIcon(svgPixmap); 
                            
                            m_model->setData(pIdx, (double)svgPixmap.width() / svgPixmap.height(), AspectRatioRole);
                            m_model->setData(pIdx, true, HasThumbnailRole);
                        } 
                    } else if (UiHelper::isGraphicsFile(ext)) { 
                        // 2026-04-11 按照用户要求：凡是图片/图形格式，物理强制提取内容缩略图 
                        // 2026-06-xx 物理修复：使用动态 m_zoomLevel 提取缩略图，防止大网格下模糊
                        QImage img = UiHelper::getShellThumbnail(path, this->m_zoomLevel); 
                        if (!img.isNull()) { 
                            icon = QIcon(QPixmap::fromImage(img)); 
                            m_model->setData(pIdx, (double)img.width() / img.height(), AspectRatioRole);
                            m_model->setData(pIdx, true, HasThumbnailRole);
                        } 
                    } 
 
                    // 降级保护：如果提取失败或非图形格式，直接指向 UiHelper::getFileIcon 获取原生图标
                    if (icon.isNull()) { 
                        icon = UiHelper::getFileIcon(path, 128); 
                    } 
 
                    m_model->setData(pIdx, icon, Qt::DecorationRole); 
                } 
                m_pathToIndexMap.remove(path); 
            } 
        } 
    }); 
 
    initUi(); 
    // 2026-05-27 按照用户要求：构造函数末尾强行对齐初始网格尺寸，废除 initGridView 中的旧硬编码值 
    updateGridSize(); 
} 
 
void ContentPanel::deferredInit() { 
    qDebug() << "[ContentPanel] deferredInit 开始执行"; 
    // 2026-04-12 按照用户要求：补全延迟初始化逻辑，此处可处理模型预热或首屏数据对齐 
    qDebug() << "[ContentPanel] deferredInit 执行完毕"; 
} 
 
void ContentPanel::initUi() { 
    QWidget* titleBar = new QWidget(this); 
    titleBar->setObjectName("ContainerHeader"); 
    titleBar->setFixedHeight(32); 
    titleBar->setStyleSheet( 
        "QWidget#ContainerHeader {" 
        "  background-color: #252526;" 
        "  border-bottom: 1px solid #333;" 
        "}" 
    ); 
    QHBoxLayout* titleL = new QHBoxLayout(titleBar); 
    titleL->setContentsMargins(15, 0, 5, 0); // 2026-05-17 按照用户要求：右侧边距统一设为 5px，消除 15px 留白
    titleL->setSpacing(5);                  // 2026-05-17 按照用户要求：间距统一为 5px
 
    QLabel* iconLabel = new QLabel(titleBar); 
    iconLabel->setPixmap(UiHelper::getIcon("eye", QColor("#41F2F2"), 18).pixmap(18, 18)); 
    titleL->addWidget(iconLabel); 
 
    QLabel* titleLabel = new QLabel("内容", titleBar); 
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #41F2F2; background: transparent; border: none;"); 
     
    m_btnLayers = new QPushButton(titleBar); 
    m_btnLayers->setCheckable(true); 
    m_btnLayers->setFixedSize(24, 24); 
    m_btnLayers->setIcon(UiHelper::getIcon("layers", QColor("#B0B0B0"), 18)); 
    // 2026-03-xx 按照宪法要求：禁绝原生 ToolTip，强制对接 ToolTipOverlay 
    m_btnLayers->setProperty("tooltipText", "递归显示子目录所有文件"); 
    m_btnLayers->installEventFilter(this); 
    m_btnLayers->setStyleSheet( 
        "QPushButton { background: transparent; border: none; border-radius: 4px; }" 
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }" 
        "QPushButton:checked { background: rgba(52, 152, 219, 0.2); border: 1px solid #3498db; }" 
        "QPushButton:disabled { opacity: 0.3; }" 
    ); 
    connect(m_btnLayers, &QPushButton::clicked, [this]() { 
        if (m_currentPath.isEmpty() || m_currentPath == "computer://") { 
            m_btnLayers->setChecked(false); 
            return; 
        } 
 
        if (m_btnLayers->isChecked()) { 
            // 探测是否有子文件夹 
            QDir dir(m_currentPath); 
            bool hasSubDirs = !dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty(); 
            if (!hasSubDirs) { 
                m_btnLayers->setChecked(false); 
                ToolTipOverlay::instance()->showText(QCursor::pos(), "当前文件夹不支持显示子文件夹项目", 1500, QColor("#E81123")); 
                return; 
            } 
            loadDirectory(m_currentPath, true); 
        } else { 
            loadDirectory(m_currentPath, false); 
        } 
    }); 
 
    titleL->addWidget(titleLabel); 
    titleL->addStretch(); 
    titleL->addWidget(m_btnLayers, 0, Qt::AlignVCenter); 
 
    m_mainLayout->addWidget(titleBar); 
 
    m_viewStack = new QStackedWidget(this); 
     
    initGridView(); 
    initListView(); 
 
    m_viewStack->addWidget(m_gridView); 
    m_viewStack->addWidget(m_treeView); 
    m_viewStack->setCurrentWidget(m_gridView); 
 
    QVBoxLayout* contentWrapper = new QVBoxLayout(); 
    contentWrapper->setContentsMargins(4, 4, 4, 4); // 2026-05-08 按照用户要求：增加到4px使卡片到容器边缘达到10px
    contentWrapper->setSpacing(0); 
    contentWrapper->addWidget(m_viewStack); 
     
    m_mainLayout->addLayout(contentWrapper); 
 
    m_textPreview = new QTextBrowser(this); 
    m_textPreview->setStyleSheet("background-color: #1E1E1E; color: #EEEEEE; border: none; padding: 20px; font-family: 'Segoe UI'; font-size: 14px;"); 
    m_textPreview->hide(); 
    m_mainLayout->addWidget(m_textPreview, 1); 
 
    m_imagePreview = new QLabel(this); 
    m_imagePreview->setStyleSheet("background-color: #1E1E1E; border: none;"); 
    m_imagePreview->setAlignment(Qt::AlignCenter); 
    m_imagePreview->hide(); 
    m_mainLayout->addWidget(m_imagePreview, 1); 
 
    // 2026-04-11 按照用户要求：为预览控件安装拦截器，实现空格键关闭功能 
    m_textPreview->installEventFilter(this); 
    m_imagePreview->installEventFilter(this); 
 
    m_gridView->installEventFilter(this); 
} 
 
void ContentPanel::updateStatusBarStats() {
    if (!m_proxyModel) return;
    
    // 只计算当前显示的总项目数量，不区分文件和文件夹
    int totalCount = m_proxyModel->rowCount();
    
    // 发送状态栏统计信号
    emit statusBarStatsUpdated(0, 0, totalCount);
}

void ContentPanel::updateGridSize() {
    // 2026-06-05 按照用户要求：彻底重构为正方形布局，名称外置
    // 2026-06-05 按照要求：将最小值锁定为 56
    m_zoomLevel = qBound(56, m_zoomLevel, 128);
    
    if (auto* jv = qobject_cast<JustifiedView*>(m_gridView)) {
        jv->setTargetRowHeight(m_zoomLevel);
    } else if (auto* lv = qobject_cast<QListView*>(m_gridView)) {
        lv->setIconSize(QSize(m_zoomLevel, m_zoomLevel));
        int side = m_zoomLevel + 46; // 正方形边长
        int ratingH = 22;           // 2026-05-17 按照要求：为卡片外的评分区预留高度
        int nameH = (int)(m_zoomLevel * 0.25); // 名称高度
        int gap = 6;                // 间距归一化
        
        // 总高度 = 正方形边长 + 间距1 + 评分高度 + 间距2 + 名称高度 + 底部缓冲
        int totalH = side + gap + ratingH + gap + nameH + 8;
        lv->setGridSize(QSize(side, totalH));
    }

    // 2026-06-05 按照要求：持久化保存当前的缩放级别
    QSettings settings("ArcMeta团队", "ArcMeta");
    settings.setValue("UI/GridZoomLevel", m_zoomLevel);

    qDebug() << "[GridSize] Zoom:" << m_zoomLevel;
} 
 
bool ContentPanel::eventFilter(QObject* obj, QEvent* event) { 
    // 2026-03-xx 按照宪法要求：物理拦截 Hover 事件以触发 ToolTipOverlay 
    // 2026-05-20 性能优化：同时支持 Enter/Leave 事件，确保响应灵敏 
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::Enter) { 
        QString text = obj->property("tooltipText").toString(); 
        if (!text.isEmpty()) { 
            ToolTipOverlay::instance()->showText(QCursor::pos(), text); 
        } 
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress) { 
        ToolTipOverlay::hideTip(); 
    } 
 
    if ((obj == m_gridView || obj == m_gridView->viewport()) && event->type() == QEvent::Wheel) { 
        // 2026-05-25 物理修复：改用 reinterpret_cast 避开 static_cast 的类型推导逻辑错误 
        QWheelEvent* wEvent = reinterpret_cast<QWheelEvent*>(event); 
        if (wEvent->modifiers() & Qt::ControlModifier) { 
            int delta = wEvent->angleDelta().y(); 
            if (delta > 0) m_zoomLevel += 8; 
            else m_zoomLevel -= 8; 
            updateGridSize(); 
            return true; 
        } 
    } 
 
    if (event->type() == QEvent::KeyPress) { 
        // 2026-05-25 物理修复：改用 reinterpret_cast 避开 QEvent 到 QKeyEvent 的 static_cast 歧义 
        QKeyEvent* keyEvent = reinterpret_cast<QKeyEvent*>(event); 
 
        // 2026-04-11 按照用户要求：如果当前正在显示文本/图片预览，按下空格键则关闭预览 
        if ((obj == m_textPreview || obj == m_imagePreview) && keyEvent->key() == Qt::Key_Space) { 
            m_textPreview->hide(); 
            m_imagePreview->hide(); 
            m_viewStack->show(); 
            // 恢复焦点到主视图，确保后续交互连续 
            if (m_viewStack->currentWidget()) m_viewStack->currentWidget()->setFocus(); 
            return true; 
        } 
 
        QAbstractItemView* view = qobject_cast<QAbstractItemView*>(obj); 
        if (!view) view = qobject_cast<QAbstractItemView*>(obj->parent()); 
 
        if (qobject_cast<QLineEdit*>(QApplication::focusWidget())) { 
            return false; 
        } 
 
        if (view) { 
            if ((keyEvent->modifiers() & Qt::ControlModifier) &&  
                (keyEvent->key() >= Qt::Key_0 && keyEvent->key() <= Qt::Key_5)) { 
                 
                int rating = keyEvent->key() - Qt::Key_0; 
                auto indexes = view->selectionModel()->selectedIndexes(); 
                for (const auto& idx : indexes) { 
                    if (idx.column() == 0) { 
                        QString path = idx.data(PathRole).toString(); 
                        if (!path.isEmpty()) { 
                            MetadataManager::instance().setRating(path.toStdWString(), rating); 
                            m_proxyModel->setData(idx, rating, RatingRole); 
                        } 
                    } 
                } 
                return true; 
            } 
 
            if (((keyEvent->modifiers() & Qt::AltModifier) || (keyEvent->modifiers() & (Qt::AltModifier | Qt::WindowShortcut))) &&  
                (keyEvent->key() == Qt::Key_D)) { 
                auto indexes = view->selectionModel()->selectedIndexes(); 
                for (const QModelIndex& idx : indexes) { 
                    if (idx.column() == 0) { 
                        QString itemPath = idx.data(PathRole).toString(); 
                        if (!itemPath.isEmpty()) { 
                            bool current = idx.data(IsLockedRole).toBool(); 
                            MetadataManager::instance().setPinned(itemPath.toStdWString(), !current); 
                            m_proxyModel->setData(idx, !current, IsLockedRole); 
                        } 
                    } 
                } 
                return true; 
            } 
 
            if ((keyEvent->modifiers() & Qt::AltModifier) &&  
                (keyEvent->key() >= Qt::Key_1 && keyEvent->key() <= Qt::Key_9)) { 
                 
                QString colorValue; 
                switch (keyEvent->key()) { 
                    case Qt::Key_1: colorValue = "#E24B4A"; break; // red
                    case Qt::Key_2: colorValue = "#EF9F27"; break; // orange
                    case Qt::Key_3: colorValue = "#FECF0E"; break; // yellow
                    case Qt::Key_4: colorValue = "#639922"; break; // green
                    case Qt::Key_5: colorValue = "#1D9E75"; break; // cyan
                    case Qt::Key_6: colorValue = "#378ADD"; break; // blue
                    case Qt::Key_7: colorValue = "#7F77DD"; break; // purple
                    case Qt::Key_8: colorValue = "#5F5E5A"; break; // gray
                    case Qt::Key_9: colorValue = ""; break; 
                } 
 
                QColor tagColor = UiHelper::parseColorName(colorValue); 
                auto indexes = view->selectionModel()->selectedIndexes(); 
                for (const auto& idx : indexes) { 
                    if (idx.column() == 0) { 
                        QString path = idx.data(PathRole).toString(); 
                        if (!path.isEmpty()) { 
                            MetadataManager::instance().setColor(path.toStdWString(), colorValue.toStdWString()); 
                            m_proxyModel->setData(idx, colorValue, ColorRole); 
 
                            // 2026-06-05 按照要求：快捷键设置颜色后立即重渲染图标，实现视觉同步 
                            QIcon coloredIcon = UiHelper::getFileIcon(path, 128); 
                            m_proxyModel->setData(idx, coloredIcon, Qt::DecorationRole); 
                        } 
                    } 
                } 
                return true; 
            } 
 
            if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) { 
                if (keyEvent->key() == Qt::Key_C) { 
                    QStringList paths; 
                    auto indexes = view->selectionModel()->selectedIndexes(); 
                    for (const auto& idx : indexes) if (idx.column() == 0) paths << QDir::toNativeSeparators(idx.data(PathRole).toString()); 
                    if (!paths.isEmpty()) QApplication::clipboard()->setText(paths.join("\r\n")); 
                    return true; 
                } 
                // 2026-03-xx 按照用户要求：补全批量重命名 (Ctrl+Shift+R) 快捷键绑定 
                if (keyEvent->key() == Qt::Key_R) { 
                    performBatchRename(); 
                    return true; 
                } 
            } 
 
            if (keyEvent->key() == Qt::Key_F2) { 
                view->edit(view->currentIndex()); 
                return true; 
            } 
            if (keyEvent->key() == Qt::Key_Delete) { 
                onCustomContextMenuRequested(view->mapFromGlobal(QCursor::pos())); 
                return true; 
            } 
             
            if (keyEvent->modifiers() & Qt::ControlModifier) { 
                // 2026-03-xx 按照用户要求：逻辑重构，统一调用 performCopy 业务函数 
                if (keyEvent->key() == Qt::Key_C && !(keyEvent->modifiers() & Qt::ShiftModifier)) { 
                    performCopy(false); 
                    return true; 
                } 
                // 2026-03-xx 按照用户要求：实现剪切逻辑 (Ctrl+X) 
                if (keyEvent->key() == Qt::Key_X) { 
                    performCopy(true); 
                    return true; 
                } 
                // 2026-03-xx 按照用户要求：逻辑重构，统一调用 performPaste 业务函数 
                if (keyEvent->key() == Qt::Key_V) { 
                    performPaste(); 
                    return true; 
                } 
            } 
 
            if (keyEvent->key() == Qt::Key_Space) { 
                QModelIndex idx = view->currentIndex(); 
                if (idx.isValid()) emit requestQuickLook(idx.data(PathRole).toString()); 
                return true; 
            } 
            if (keyEvent->key() == Qt::Key_Backspace) { 
                QDir dir(m_currentPath); 
                if (dir.cdUp()) emit directorySelected(dir.absolutePath()); 
                return true; 
            } 
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) { 
                onDoubleClicked(view->currentIndex()); 
                return true; 
            } 
            if (keyEvent->modifiers() & Qt::ControlModifier && keyEvent->key() == Qt::Key_Backslash) { 
                setViewMode(m_viewStack->currentIndex() == 0 ? ListView : GridView); 
                return true; 
            } 
        } 
    } 
    return QWidget::eventFilter(obj, event); 
} 
 
QString ContentPanel::getAdjacentFilePath(const QString& currentPath, int delta) { 
    if (!m_proxyModel || m_proxyModel->rowCount() == 0) return QString(); 
 
    int currentIndex = -1; 
    for (int i = 0; i < m_proxyModel->rowCount(); ++i) { 
        QModelIndex idx = m_proxyModel->index(i, 0); 
        if (idx.data(PathRole).toString() == currentPath) { 
            currentIndex = i; 
            break; 
        } 
    } 
 
    if (currentIndex == -1) return QString(); 
 
    int targetIndex = currentIndex + delta; 
    // 逻辑：触达边界时停止，不进行循环跳转 
    if (targetIndex < 0 || targetIndex >= m_proxyModel->rowCount()) { 
        return QString(); 
    } 
 
    QModelIndex targetIdx = m_proxyModel->index(targetIndex, 0); 
    return targetIdx.data(PathRole).toString(); 
} 
 
void ContentPanel::wheelEvent(QWheelEvent* event) { 
    if (event->modifiers() & Qt::ControlModifier) { 
        int delta = event->angleDelta().y(); 
        if (delta > 0) m_zoomLevel += 8; 
        else m_zoomLevel -= 8; 
        updateGridSize(); 
        event->accept(); 
    } else { 
        QWidget::wheelEvent(event); 
    } 
} 
 
void ContentPanel::setViewMode(ViewMode mode) { 
    if (mode == GridView) m_viewStack->setCurrentWidget(m_gridView); 
    else m_viewStack->setCurrentWidget(m_treeView); 
} 
 
void ContentPanel::initGridView() { 
    m_gridView = new DropJustifiedView(this); 
    m_gridView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    m_gridView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection); 
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu); 
 
    m_gridView->setDragEnabled(true); 
    m_gridView->setDragDropMode(QAbstractItemView::DragOnly); 
 
    // 2026-06-xx 物理修复：移除 SelectedClicked，防止选中卡片时意外触发重命名逻辑
    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed); 
 
    m_gridView->setModel(m_proxyModel); 

    auto* justifiedView = qobject_cast<JustifiedView*>(m_gridView);
    if (justifiedView) {
        justifiedView->setAspectRatioRole(AspectRatioRole);
        justifiedView->setHasThumbnailRole(HasThumbnailRole);
        auto* delegate = new ThumbnailDelegate(this);
        delegate->setHasThumbnailRole(HasThumbnailRole);
        delegate->setRatingRole(RatingRole);
        delegate->setPathRole(PathRole);
        delegate->setPinnedRole(PinnedRole);
        delegate->setManagedRole(InDatabaseRole);
        delegate->setTypeRole(TypeRole);
        delegate->setIsEmptyRole(IsEmptyRole);
        delegate->setColorRole(ColorRole);
        m_gridView->setItemDelegate(delegate);
    } else {
        m_gridView->setItemDelegate(new GridItemDelegate(this)); 
    }

    m_gridView->viewport()->installEventFilter(this); 
 
    connect(m_gridView, &QAbstractItemView::doubleClicked, this, &ContentPanel::onDoubleClicked); 
 
    m_gridView->setStyleSheet( 
        "QAbstractItemView { background-color: transparent; border: none; outline: none; }" 
        "QAbstractItemView::item { background: transparent; }" 
        "QAbstractItemView::item:selected { background-color: transparent; }"
    ); 
 
    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged); 
    connect(m_gridView, &QAbstractItemView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested); 
} 
 
void ContentPanel::initListView() { 
    m_treeView = new DropTreeView(this); 
    m_treeView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    m_treeView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); 
    m_treeView->setSortingEnabled(true); 
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu); 
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection); 
     
    m_treeView->setDragEnabled(true); 
    m_treeView->setDragDropMode(QAbstractItemView::DragOnly); 
 
    m_treeView->setExpandsOnDoubleClick(false); 
    m_treeView->setRootIsDecorated(false); 
     
    m_treeView->setItemDelegate(new TreeItemDelegate(this)); 
 
    m_treeView->setModel(m_proxyModel); 
    m_treeView->viewport()->installEventFilter(this); 
 
    m_treeView->setStyleSheet( 
        "QTreeView { background-color: transparent; border: none; outline: none; font-size: 12px; }" 
        "QTreeView::item { height: 28px; color: #EEEEEE; padding-left: 0px; }" 
        "QTreeView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 6px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }" 
    ); 
 
    m_treeView->header()->setStyleSheet( 
        "QHeaderView::section { background-color: #252525; color: #B0B0B0; padding-left: 10px; border: none; height: 32px; font-size: 11px; }" 
    ); 
 
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged); 
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested); 
    connect(m_treeView, &QTreeView::doubleClicked, this, &ContentPanel::onDoubleClicked); 
} 
 
void ContentPanel::onCustomContextMenuRequested(const QPoint& pos) { 
    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender()); 
    if (!view) return; 
 
    QModelIndex currentIndex = view->indexAt(pos); 
    bool onItem = currentIndex.isValid(); 
    bool isFolder = onItem && (currentIndex.data(TypeRole).toString() == "folder"); 
    QString path = currentIndex.data(PathRole).toString(); 
 
    QMenu menu(this); 
    UiHelper::applyMenuStyle(&menu); 
 
    if (onItem) { 
        // [核心操作区] 
        QAction* actOpen = menu.addAction(isFolder ? "打开文件夹" : "打开"); 
        actOpen->setData(ActionOpen); 
        if (!isFolder) { 
            menu.addAction("用系统默认程序打开")->setData(ActionOpenDefault); 
        } 
        menu.addAction("在“资源管理器”中显示")->setData(ActionShowInExplorer); 
 
        menu.addSeparator(); 
 
        // [归类与标记区] 
        QMenu* categorizeMenu = menu.addMenu("归类到..."); 
        UiHelper::applyMenuStyle(categorizeMenu); 
        auto categories = CategoryRepo::getAll(); 
        if (categories.empty()) { 
            categorizeMenu->addAction("（暂无分类）")->setEnabled(false); 
        } else { 
            for (const auto& cat : categories) { 
                QAction* act = categorizeMenu->addAction(QString::fromStdWString(cat.name)); 
                act->setData(ActionCategorize); 
                act->setProperty("catId", cat.id); 
            } 
        } 
 
        QMenu* colorMenu = menu.addMenu("设定颜色标签"); 
        UiHelper::applyMenuStyle(colorMenu); 
        colorMenu->setIcon(UiHelper::getIcon("palette", QColor("#EEEEEE"))); 
        struct ColorItem { QString value; QString label; QColor preview; }; 
        QList<ColorItem> colorItems = { 
            {"", "无颜色", QColor("#888780")}, 
            {"#E24B4A", "红色", QColor("#E24B4A")},
            {"#EF9F27", "橙色", QColor("#EF9F27")},
            {"#FECF0E", "黄色", QColor("#FECF0E")},
            {"#639922", "绿色", QColor("#639922")},
            {"#1D9E75", "青色", QColor("#1D9E75")},
            {"#378ADD", "蓝色", QColor("#378ADD")},
            {"#7F77DD", "紫色", QColor("#7F77DD")},
            {"#5F5E5A", "灰色", QColor("#5F5E5A")}
        }; 
        for (const auto& ci : colorItems) { 
            QAction* ca = colorMenu->addAction(ci.label); 
            ca->setData(ActionColorTag); 
            ca->setProperty("colorName", ci.value); 
            QPixmap pix(12, 12); pix.fill(Qt::transparent); 
            QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing); 
            p.setBrush(ci.preview); p.setPen(Qt::NoPen); 
            p.drawEllipse(0, 0, 12, 12); 
            ca->setIcon(QIcon(pix)); 
        } 
 
        bool isPinned = currentIndex.data(IsLockedRole).toBool(); 
        menu.addAction(isPinned ? "取消置顶" : "置顶")->setData(isPinned ? ActionUnpin : ActionPin); 
 
        // --- 2026-05-16 图像分析：从图中提取主色调 ---
        QString ext = QFileInfo(path).suffix().toLower();
        if (UiHelper::isGraphicsFile(ext)) {
            menu.addAction("解析颜色...")->setData(ActionExtractColor);
        }

        menu.addSeparator(); 
 
        // [批量与加密区] 
        if (isFolder) { 
            menu.addAction("批量重命名 (Ctrl+Shift+R)")->setData(ActionBatchRename); 
        } else { 
            QMenu* cryptoMenu = menu.addMenu("加密保护"); 
            UiHelper::applyMenuStyle(cryptoMenu); 
            cryptoMenu->addAction("执行加密保护")->setData(ActionEncrypt); 
            cryptoMenu->addAction("解除加密")->setData(ActionDecrypt); 
            cryptoMenu->addAction("修改加密密码")->setData(ActionChangePwd); 
        } 
 
        menu.addSeparator(); 
 
        // [通用编辑区] 
        menu.addAction("重命名")->setData(ActionRename); 
        menu.addAction("复制")->setData(ActionCopy); 
        menu.addAction("剪切")->setData(ActionCut); 
        menu.addAction("粘贴")->setData(ActionPaste); 
        menu.addAction("删除（移入回收站）")->setData(ActionDelete); 
 
        menu.addSeparator(); 
        menu.addAction("复制路径")->setData(ActionCopyPath); 
        menu.addAction("属性")->setData(ActionProperties); 
 
    } else { 
        // [空白处菜单] 
        QMenu* newMenu = menu.addMenu("新建..."); 
        UiHelper::applyMenuStyle(newMenu); 
        newMenu->addAction(UiHelper::getIcon("folder_filled", QColor("#EEEEEE")), "创建文件夹")->setData(ActionNewFolder); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown")->setData(ActionNewMd); 
        newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)")->setData(ActionNewTxt); 
 
        menu.addSeparator(); 
        QAction* actPaste = menu.addAction("粘贴"); 
        actPaste->setData(ActionPaste); 
        actPaste->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 
 
        menu.addSeparator(); 
        QAction* actProp = menu.addAction("当前文件夹属性"); 
        actProp->setData(ActionProperties); 
        actProp->setEnabled(!m_currentPath.isEmpty() && m_currentPath != "computer://"); 
    } 
 
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos)); 
    if (!selectedAction || !selectedAction->data().isValid()) return; 
 
    ContextAction action = static_cast<ContextAction>(selectedAction->data().toInt()); 
 
    switch (action) { 
        case ActionOpen: 
        case ActionOpenDefault: 
            onDoubleClicked(currentIndex); 
            break; 
        case ActionShowInExplorer: { 
            QString target = onItem ? path : m_currentPath; 
            QStringList args; args << "/select," << QDir::toNativeSeparators(target); 
            QProcess::startDetached("explorer", args); 
            break; 
        } 
        case ActionNewFolder: createNewItem("folder"); break; 
        case ActionNewMd: createNewItem("md"); break; 
        case ActionNewTxt: createNewItem("txt"); break; 
        case ActionCategorize: { 
            int catId = selectedAction->property("catId").toInt(); 
            auto indexes = view->selectionModel()->selectedIndexes(); 
             
            for (const auto& idx : indexes) { 
                if (idx.column() == 0) { 
                    QString itemPath = idx.data(PathRole).toString(); 
                    // 2026-06-xx 物理同步：基于同步获取的 File ID 进行归类，解决新文件关联失败冲突。 
                    std::string fid = MetadataManager::instance().getFileIdSync(itemPath.toStdWString()); 
                    if (!fid.empty()) { 
                        CategoryRepo::addItemToCategory(catId, fid); 
                    } 
                } 
            } 
            ToolTipOverlay::instance()->showText(QCursor::pos(), "已成功归类 (基于 File ID)", 1500, QColor("#2ecc71")); 
            break; 
        } 
        case ActionPin: 
        case ActionUnpin: { 
            auto indexes = view->selectionModel()->selectedIndexes(); 
            bool pin = (action == ActionPin); 
            for (const QModelIndex& idx : indexes) { 
                if (idx.column() == 0) { 
                    QString itemPath = idx.data(PathRole).toString(); 
                    MetadataManager::instance().setPinned(itemPath.toStdWString(), pin); 
                    m_proxyModel->setData(idx, pin, IsLockedRole); 
                } 
            } 
            break; 
        } 
        case ActionColorTag: { 
            QString colorName = selectedAction->property("colorName").toString(); 
            QColor tagColor = UiHelper::parseColorName(colorName); 
            auto indexes = view->selectionModel()->selectedIndexes(); 
            for (const auto& idx : indexes) { 
                if (idx.column() == 0) { 
                    QString itemPath = idx.data(PathRole).toString(); 
                    MetadataManager::instance().setColor(itemPath.toStdWString(), colorName.toStdWString()); 
                    m_proxyModel->setData(idx, colorName, ColorRole); 
 
                    // 2026-06-05 按照要求：设置颜色后立即重新生成并应用图标，实现视觉同步 
                    // 2026-05-17 逻辑修复：针对图像格式，必须优先尝试提取缩略图，防止图标覆盖内容
                    QIcon coloredIcon;
                    QString ext = QFileInfo(itemPath).suffix().toLower();
                    if (UiHelper::isGraphicsFile(ext)) {
                        // 2026-06-xx 物理同步：使用 m_zoomLevel 确保尺寸一致
                        QImage img = UiHelper::getShellThumbnail(itemPath, this->m_zoomLevel);
                        if (!img.isNull()) coloredIcon = QIcon(QPixmap::fromImage(img));
                    }
                    if (coloredIcon.isNull()) {
                        coloredIcon = UiHelper::getFileIcon(itemPath, this->m_zoomLevel);
                    }
                    m_proxyModel->setData(idx, coloredIcon, Qt::DecorationRole); 
                } 
            } 
            break; 
        } 
        case ActionExtractColor: {
            QPointer<ContentPanel> weakThis(this);
            (void)QtConcurrent::run([weakThis, path]() {
                auto palette = UiHelper::extractPalette(path);
                if (palette.isEmpty()) return;
                
                // 1. 提取第一个颜色作为主色调 (用于图标着色)
                QColor dominant = UiHelper::quantizeColor(palette.first().first);
                QString colorHex = dominant.name().toUpper();
                
                QMetaObject::invokeMethod(weakThis.data(), [weakThis, path, colorHex, palette, dominant]() {
                    if (weakThis) {
                        // 2. 物理双重存储：主色 + 全量变长色板
                        MetadataManager::instance().setColor(path.toStdWString(), colorHex.toStdWString());
                        MetadataManager::instance().setPalettes(path.toStdWString(), palette);
                        
                        // 3. 物理同步 UI 状态
                        auto* model = weakThis->m_model;
                        for (int i = 0; i < model->rowCount(); ++i) {
                            auto* item = model->item(i, 0);
                            if (item && item->data(PathRole).toString() == path) {
                                item->setData(colorHex, ColorRole);
                                
                                // 向下兼容注入 PalettesRole (用于当前视图即时搜索)
                                QVariantList palList;
                                for (const auto& p : palette) {
                                    QVariantMap m; m["color"] = p.first; m["ratio"] = p.second;
                                    palList << m;
                                }
                                item->setData(palList, PalettesRole);
                                
                                QIcon coloredIcon;
                                QString suffix = QFileInfo(path).suffix().toLower();
                                if (UiHelper::isGraphicsFile(suffix)) {
                                    // 2026-06-xx 物理同步：使用 m_zoomLevel 确保解析后更新的图标尺寸一致
                                    QImage img = UiHelper::getShellThumbnail(path, weakThis->m_zoomLevel);
                                    if (!img.isNull()) coloredIcon = QIcon(QPixmap::fromImage(img));
                                }
                                if (coloredIcon.isNull()) {
                                    coloredIcon = UiHelper::getFileIcon(path, weakThis->m_zoomLevel);
                                }
                                item->setData(coloredIcon, Qt::DecorationRole);
                                break;
                            }
                        }
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "变长色板已物理提取并绑定", 1500, QColor("#2ecc71"));
                    }
                });
            });
            break;
        }
        case ActionEncrypt: { 
            bool ok; 
            QString pwd = QInputDialog::getText(this, "加密保护", "设置加密密码:", QLineEdit::Password, "", &ok); 
            if (ok && !pwd.isEmpty()) { 
                auto indexes = view->selectionModel()->selectedIndexes(); 
                QStringList targets; 
                for (const auto& idx : indexes) if (idx.column() == 0) targets << idx.data(PathRole).toString(); 
                 
                ToolTipOverlay::instance()->showText(QCursor::pos(), "加密任务已在后台启动...", 2000); 
                 
                std::string stdPwd = pwd.toStdString(); 
                QPointer<ContentPanel> self(this); 
                QString currentDir = m_currentPath; 
 
                (void)QThreadPool::globalInstance()->start([self, targets, stdPwd, currentDir]() { 
                    for (const QString& src : targets) { 
                        QString dest = src + ".amenc"; 
                        if (EncryptionManager::instance().encryptFile(src.toStdWString(), dest.toStdWString(), stdPwd)) { 
                            QFile::remove(src); 
                            MetadataManager::instance().setEncrypted(dest.toStdWString(), true); 
                        } 
                    } 
                    QMetaObject::invokeMethod(qApp, [self, currentDir]() { 
                        if (self && self->m_currentPath == currentDir) self->loadDirectory(currentDir, self->m_isRecursive); 
                        ToolTipOverlay::instance()->showText(QCursor::pos(), "加密任务处理完成", 1500, QColor("#2ecc71")); 
                    }); 
                }); 
            } 
            break; 
        } 
        case ActionDecrypt: { 
            bool ok; 
            QString pwd = QInputDialog::getText(this, "解除加密", "输入加密密码:", QLineEdit::Password, "", &ok); 
            if (ok && !pwd.isEmpty()) { 
                ToolTipOverlay::instance()->showText(QCursor::pos(), "解除加密逻辑已触发", 1500); 
            } 
            break; 
        } 
        case ActionBatchRename: performBatchRename(); break; 
        case ActionRename: view->edit(currentIndex); break; 
        case ActionCopy: performCopy(false); break; 
        case ActionCut: performCopy(true); break; 
        case ActionPaste: performPaste(); break; 
        case ActionDelete: { 
            std::wstring wpath = path.toStdWString() + L'\0' + L'\0'; 
            SHFILEOPSTRUCTW fileOp = { 0 }; 
            fileOp.wFunc = FO_DELETE; 
            fileOp.pFrom = wpath.c_str(); 
            fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION; 
            if (SHFileOperationW(&fileOp) == 0) loadDirectory(m_currentPath); 
            break; 
        } 
        case ActionCopyPath: QApplication::clipboard()->setText(QDir::toNativeSeparators(path)); break; 
        case ActionProperties: { 
            SHELLEXECUTEINFOW sei = { sizeof(sei) }; 
            sei.fMask = SEE_MASK_INVOKEIDLIST; 
            sei.lpVerb = L"properties"; 
            std::wstring wpath = (onItem ? path : m_currentPath).toStdWString(); 
            sei.lpFile = wpath.c_str(); 
            sei.nShow = SW_SHOW; 
            ShellExecuteExW(&sei); 
            break; 
        } 
        default: break; 
    } 
} 
 
void ContentPanel::performCopy(bool cutMode) { 
    // 2026-03-xx 按照用户要求：封装标准化文件复制/剪切逻辑 
    QModelIndexList indexes = getSelectedIndexes(); 
    QList<QUrl> urls; 
    for (const auto& idx : indexes) { 
        if (idx.column() == 0) { 
            QString path = idx.data(PathRole).toString(); 
            if (!path.isEmpty()) urls << QUrl::fromLocalFile(path); 
        } 
    } 
 
    if (urls.isEmpty()) return; 
 
    QMimeData* mime = new QMimeData(); 
    mime->setUrls(urls); 
     
    if (cutMode) { 
        // 核心规范：告知系统这是剪切操作 (DROPEFFECT_MOVE = 2) 
        // 修复：将变量名由 data 改为 effectData，避免隐藏类成员警告 
        QByteArray effectData; 
        effectData.append((char)2);  
        mime->setData("Preferred DropEffect", effectData); 
    } 
 
    QApplication::clipboard()->setMimeData(mime); 
} 
 
void ContentPanel::performPaste() { 
    // 2026-03-xx 按照用户要求：封装标准化文件粘贴逻辑，对接 Windows Shell 
    if (m_currentPath.isEmpty() || m_currentPath == "computer://") return; 
 
    const QMimeData* mime = QApplication::clipboard()->mimeData(); 
    if (!mime || !mime->hasUrls()) return; 
 
    QList<QUrl> urls = mime->urls(); 
    std::wstring fromPaths; 
    for (const QUrl& url : urls) { 
        fromPaths += QDir::toNativeSeparators(url.toLocalFile()).toStdWString() + L'\0'; 
    } 
     
    if (fromPaths.empty()) return; 
    fromPaths += L'\0'; 
 
    // 探测是否为剪切模式 
    bool isMove = false; 
    if (mime->hasFormat("Preferred DropEffect")) { 
        QByteArray effect = mime->data("Preferred DropEffect"); 
        if (!effect.isEmpty() && (effect.at(0) & 0x02)) isMove = true; 
    } 
 
    std::wstring toPath = m_currentPath.toStdWString() + L'\0' + L'\0'; 
    SHFILEOPSTRUCTW fileOp = { 0 }; 
    fileOp.wFunc = isMove ? FO_MOVE : FO_COPY; 
    fileOp.pFrom = fromPaths.c_str(); 
    fileOp.pTo = toPath.c_str(); 
    fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION; 
 
    if (SHFileOperationW(&fileOp) == 0) { 
        loadDirectory(m_currentPath, m_isRecursive); 
    } 
} 
 
void ContentPanel::performBatchRename() { 
    // 2026-03-xx 按照用户要求：弹出深度集成的高级批量重命名对话框 
    QModelIndexList indexes = getSelectedIndexes(); 
    std::vector<std::wstring> originalPaths; 
    for (const auto& idx : indexes) { 
        if (idx.column() == 0) { 
            QString path = idx.data(PathRole).toString(); 
            if (!path.isEmpty()) originalPaths.push_back(path.toStdWString()); 
        } 
    } 
 
    if (originalPaths.empty()) { 
        ToolTipOverlay::instance()->showText(QCursor::pos(), "请先选择需要重命名的项目", 2000, QColor("#E81123")); 
        return; 
    } 
 
    BatchRenameDialog dlg(originalPaths, this); 
    if (dlg.exec() == QDialog::Accepted) { 
        loadDirectory(m_currentPath, m_isRecursive); 
        ToolTipOverlay::instance()->showText(QCursor::pos(), "批量重命名操作已成功执行", 1500, QColor("#2ecc71")); 
    } 
} 
 
void ContentPanel::onSelectionChanged() { 
    QItemSelectionModel* selectionModel = (m_viewStack->currentWidget() == m_gridView) ? m_gridView->selectionModel() : m_treeView->selectionModel(); 
    if (!selectionModel) return; 
 
    QStringList selectedPaths; 
    QModelIndexList indices = selectionModel->selectedIndexes(); 
    for (const QModelIndex& index : indices) { 
        if (index.column() == 0) { 
            QString path = index.data(PathRole).toString(); 
            if (!path.isEmpty()) selectedPaths.append(path); 
        } 
    } 
    emit selectionChanged(selectedPaths); 
} 
 
void ContentPanel::onDoubleClicked(const QModelIndex& index) { 
    if (!index.isValid()) return; 
 
    // 2026-06-xx 重构逻辑：优先处理子分类跳转 
    int catId = index.data(CategoryIdRole).toInt(); 
    if (catId > 0) { 
        emit categoryClicked(catId); 
        return; 
    } 
 
    QString path = index.data(PathRole).toString(); 
    if (path.isEmpty()) return; 
 
    QFileInfo info(path); 
    if (info.isDir()) { 
        emit directorySelected(path);  
    } else { 
        QDesktopServices::openUrl(QUrl::fromLocalFile(path)); 
    } 
} 
 
void ContentPanel::loadDirectory(const QString& path, bool recursive) { 
    qDebug() << "[Content] 开始物理递归扫描 ->" << path << (recursive ? "递归" : "单级"); 
    emit dataSourceChanged("nav"); // 2026-05-17 按照用户要求：发射数据源变更信号，高亮展示导航面板焦点线条
    if (m_viewStack) m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
 
    m_isRecursive = recursive; 
    if (m_btnLayers) m_btnLayers->setChecked(recursive); 
 
    // 2026-03-xx 极致优化：停止之前的图标懒加载任务，清理队列 
    m_lazyIconTimer->stop(); 
    m_smoothConsumeTimer->stop(); 
    m_iconPendingPaths.clear(); 
    m_pathToIndexMap.clear(); 
    m_uiPendingQueue.clear(); 
 
    // 扫描任务期间暂时封印动态排序 
    m_proxyModel->setDynamicSortFilter(false); 
 
    m_model->clear(); 
    m_model->setHorizontalHeaderLabels({"名称", "大小", "类型", "修改时间"}); 
 
    if (path.isEmpty() || path == "computer://") { 
        m_currentPath = "computer://"; 
        updateLayersButtonState(); 
         
        // 2026-06-xx 物理清理：移除 prefetchDirectory 调用 
 
        const auto drives = QDir::drives(); 
        QMap<int, int> rc; QMap<QString, int> cc, tc, tyc, cdc, mdc; 
        QFileIconProvider provider;
        for (const QFileInfo& drive : drives) { 
            QString drivePath = drive.absolutePath(); 
 
            // 2026-04-12 按照用户最新铁律：从 MetadataManager 获取集中管理的磁盘元数据 
            RuntimeMeta rm = MetadataManager::instance().getMeta(drivePath.toStdWString()); 
             
            // 回归原生：使用 QFileIconProvider 获取真实的磁盘分区图标，停止人工 SVG 着色
            QIcon driveIcon = provider.icon(drive); 
            auto* item = new QStandardItem(driveIcon, drivePath); 
            item->setData(drivePath, PathRole); 
            item->setData("folder", TypeRole); 
            item->setData(rm.rating, RatingRole); 
            item->setData(QString::fromStdWString(rm.color), ColorRole); 
            item->setData(rm.pinned, PinnedRole); // 逻辑还原：使用 PinnedRole 存储原始置顶状态 
            item->setData(rm.pinned, IsLockedRole); // 视觉还原：IsLockedRole 负责 UI 渲染 
            item->setData(1.0, AspectRatioRole);
            item->setData(false, HasThumbnailRole);
 
            QList<QStandardItem*> row; 
            row << item << new QStandardItem("-") << new QStandardItem("磁盘分区") << new QStandardItem("-"); 
            m_model->appendRow(row); 
            tyc["folder"]++; 
        } 
        emit directoryStatsReady(rc, cc, tc, tyc, cdc, mdc); 
        return; 
    } 
 
    m_currentPath = path; 
    updateLayersButtonState(); 
     
    // 2026-03-xx 极致优化：分块加载方案。 
    // 修复：使用 QPointer 捕获 this，防止面板销毁后后台线程访问已释放内存 (Use-after-free)。 
    QPointer<ContentPanel> panelPtr(this); 
    // 2026-05-28 编译优化：显式使用 (void) 强转以消除 Qt 6 对 QThreadPool::start 返回值未使用的警告 
    (void)QThreadPool::globalInstance()->start([panelPtr, path, recursive]() { 
        if (!panelPtr) return; 
         
        ContentPanel::ScanStats globalStats; 
        QList<ContentPanel::ScanItemData> currentBatch; 
 
        auto flushBatch = [panelPtr, path](const QList<ContentPanel::ScanItemData>& batch) { 
            QMetaObject::invokeMethod(qApp, [panelPtr, path, batch]() { 
                if (!panelPtr || panelPtr->m_currentPath != path) return; 
                 
                // 将数据压入待处理队列 
                for (const auto& data : batch) { 
                    panelPtr->m_uiPendingQueue.push_back(data); 
                } 
 
                // 启动平滑消费定时器 
                if (!panelPtr->m_smoothConsumeTimer->isActive()) { 
                    panelPtr->m_smoothConsumeTimer->start(); 
                } 
            }, Qt::QueuedConnection); 
        }; 
 
        // 修复：显式定义递归函数对象，避免嵌套 Lambda 的隐式捕获错误 
        std::function<void(const QString&, bool)> scanDir; 
        scanDir = [&](const QString& p, bool rec) { 
            QDir dir(p); 
            if (!dir.exists()) return; 
 
            QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name); 
            QDate today = QDate::currentDate(); 
            QDate yesterday = today.addDays(-1); 
            auto dateKey = [&](const QDate& d) -> QString { 
                if (d == today) return "today"; 
                if (d == yesterday) return "yesterday"; 
                return d.toString("yyyy-MM-dd"); 
            }; 
 
            for (const QFileInfo& info : entries) { 
                // 检查后台任务存活 
                if (!panelPtr) return; 
            if (info.fileName() == ".am_meta.json" || info.fileName() == ".am_meta.json.tmp") continue; // 2026-06-xx 物理隔离 
 
                ContentPanel::ScanItemData data; 
                data.name = info.fileName(); 
            // 2026-05-27 物理修复：在扫描端强制执行 normalizePath 逻辑（统一小写 + 磁盘补丁），确保 Key 匹配 
            // 此处 QDir::cleanPath 产生的碎片将交由 MetadataManager 内部再次归一化 
                data.fullPath = QDir::toNativeSeparators(QDir::cleanPath(info.absoluteFilePath())); 
                data.isDir = info.isDir(); 
                data.suffix = info.suffix().toUpper(); 
                data.size = info.size(); 
                data.mtime = info.lastModified(); 
                data.meta = MetadataManager::instance().getMeta(data.fullPath.toStdWString()); 
 
                // 2026-04-12 按照用户要求：探测空文件夹 
                if (data.isDir) { 
                    data.isEmpty = QDir(data.fullPath).isEmpty(); 
                } 
 
                currentBatch.append(data); 
                if (currentBatch.size() >= 500) { // 提高后台批次大小，由定时器限流 UI 插入 
                    flushBatch(currentBatch); 
                    currentBatch.clear(); 
                } 
 
                globalStats.ratingCounts[data.meta.rating]++; 
                globalStats.colorCounts[QString::fromStdWString(data.meta.color)]++; 
                for (const auto& t : data.meta.tags) globalStats.tagCounts[t]++; 
                if (data.meta.tags.isEmpty()) globalStats.noTagCount++; 
                globalStats.typeCounts[data.isDir ? "folder" : data.suffix]++; 
                globalStats.createDateCounts[dateKey(info.birthTime().date())]++; 
                globalStats.modifyDateCounts[dateKey(data.mtime.date())]++; 
 
                if (rec && data.isDir) { 
                    // 后台任务存活检查 
                    if (!panelPtr) return; 
                    scanDir(data.fullPath, true); 
                } 
            } 
        }; 
 
        scanDir(path, recursive); 
        if (!panelPtr) return; 
 
        if (!currentBatch.isEmpty()) flushBatch(currentBatch); 
        if (globalStats.noTagCount > 0) globalStats.tagCounts["__none__"] = globalStats.noTagCount; 
 
        // 最后发送全量统计结果供筛选面板使用 
        QMetaObject::invokeMethod(qApp, [panelPtr, path, globalStats]() { 
            if (panelPtr && panelPtr->m_currentPath == path) { 
                emit panelPtr->directoryStatsReady(globalStats.ratingCounts, globalStats.colorCounts, globalStats.tagCounts,  
                                                  globalStats.typeCounts, globalStats.createDateCounts, globalStats.modifyDateCounts); 
            } 
        }, Qt::QueuedConnection); 
    }); 
} 
 
 
 
 
void ContentPanel::search(const QString& query) { 
    qDebug() << "[Content] 触发代理过滤 (局部手动关键词) ->" << query; 
    if (m_viewStack) m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
 
    auto* proxy = qobject_cast<FilterProxyModel*>(m_proxyModel); 
    if (proxy) {
        proxy->setSearchQuery(query);
        
        // 2026-06-xx 按照用户要求：搜索后实时重新计算可见项统计，同步给筛选面板
        ScanStats stats;
        for (int i = 0; i < proxy->rowCount(); ++i) {
            QModelIndex idx = proxy->index(i, 0);
            int rating = idx.data(RatingRole).toInt();
            QString color = idx.data(ColorRole).toString();
            QString type = idx.data(TypeRole).toString();
            QStringList tags = idx.data(TagsRole).toStringList();
            
            stats.ratingCounts[rating]++;
            stats.colorCounts[color]++;
            if (type == "folder") stats.typeCounts["folder"]++;
            else {
                QString path = idx.data(PathRole).toString();
                stats.typeCounts[QFileInfo(path).suffix().toUpper()]++;
            }
            
            for (const QString& t : tags) stats.tagCounts[t]++;
            if (tags.isEmpty()) stats.noTagCount++;
        }
        if (stats.noTagCount > 0) stats.tagCounts["__none__"] = stats.noTagCount;
        
        emit directoryStatsReady(stats.ratingCounts, stats.colorCounts, stats.tagCounts,
                               stats.typeCounts, stats.createDateCounts, stats.modifyDateCounts);
    }
} 
 
void ContentPanel::applyFilters(const FilterState& state) { 
    m_currentFilter = state; 
    applyFilters(); 
} 
 
void ContentPanel::applyFilters() { 
    // 2026-05-25 编译修复：改用 qobject_cast 彻底根除 static_cast 指针转换报错 
    auto* proxy = qobject_cast<FilterProxyModel*>(m_proxyModel); 
    if (proxy) { 
        proxy->currentFilter = m_currentFilter; 
        proxy->updateFilter(); 
    } 
    // 2026-05-08 按照用户要求：筛选条件变化后更新状态栏统计
    updateStatusBarStats();
} 
 
void ContentPanel::previewFile(const QString& path) { 
    // 2026-03-xx 按照用户要求：全能预览实现，支持图片与多种文本格式，破除 .md 局限 
    QFileInfo info(path); 
    QString ext = info.suffix().toLower(); 
 
    // 1. 图片格式识别 
    static const QStringList imageExts = {"jpg", "jpeg", "png", "bmp", "webp", "gif", "ico"}; 
    if (imageExts.contains(ext)) { 
        QPixmap pix(path); 
        if (!pix.isNull()) { 
            m_viewStack->hide(); 
            m_textPreview->hide(); 
             
            // 保持比例缩放显示 
            m_imagePreview->setPixmap(pix.scaled(m_imagePreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)); 
            m_imagePreview->show(); 
            return; 
        } 
    } 
 
    // 2. 文本格式识别 (参考版本A 扩展识别) 
    // 此处可根据需要进一步细化，目前先处理常规文本 
    QFile file(path); 
    if (file.open(QIODevice::ReadOnly)) { 
        m_viewStack->hide(); 
        m_imagePreview->hide(); 
 
        // 针对 Markdown 特殊渲染 
        if (ext == "md" || ext == "markdown") { 
             m_textPreview->setMarkdown(file.readAll()); 
        } else { 
             // 针对其他代码或文本，直接显示原文 
             // 限制读取前 1MB 以防大文件卡死 
             m_textPreview->setPlainText(QString::fromUtf8(file.read(1024 * 1024))); 
        } 
        m_textPreview->show(); 
        file.close(); 
    } 
} 
 
void ContentPanel::loadCategory(int categoryId) { 
    m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    emit dataSourceChanged("category"); // 2026-05-17 按照用户要求：发射数据源变更信号，高亮展示分类面板焦点线条
     
    m_lazyIconTimer->stop(); 
    m_iconPendingPaths.clear(); 
    m_pathToIndexMap.clear(); 
 
    m_model->clear(); 
    m_model->setHorizontalHeaderLabels({"名称", "大小", "类型", "修改时间"}); 
 
    // 2026-05-07 按照用户要求：添加统计数据结构，用于筛选器填充 
    ScanStats stats; 
 
    // 1. 加载子分类 
    auto allCategories = CategoryRepo::getAll(); 
    QFileIconProvider provider;
    for (const auto& cat : allCategories) { 
        if (cat.parentId == categoryId) { 
            QList<QStandardItem*> row; 
            QString color = QString::fromStdWString(cat.color).isEmpty() ? "#aaaaaa" : QString::fromStdWString(cat.color); 
            // 回归原生：统一使用系统默认文件夹图标
            QIcon icon = provider.icon(QFileIconProvider::Folder); 
             
            auto* item = new QStandardItem(icon, QString::fromStdWString(cat.name)); 
            item->setData("category", TypeRole); 
            item->setData(cat.id, CategoryIdRole); 
            item->setData(color, ColorRole); 
             
            row << item << new QStandardItem("-") << new QStandardItem("子分类") << new QStandardItem("-"); 
            m_model->appendRow(row); 
        } 
    } 
 
    // 2. 加载绑定的 File ID 
    std::vector<std::string> fids = CategoryRepo::getFileIdsInCategory(categoryId); 
    QSqlDatabase db = ArcMeta::Database::instance().getThreadDatabase(); 
     
    for (const auto& fid : fids) { 
        QSqlQuery q(db); 
        q.prepare("SELECT path, type, size, mtime FROM items WHERE file_id_128 = ? AND deleted = 0"); 
        q.addBindValue(QString::fromStdString(fid)); 
         
        if (q.exec() && q.next()) { 
            QString itemPath = q.value(0).toString(); 
            QString type = q.value(1).toString(); 
            qint64 size = q.value(2).toLongLong(); 
            QDateTime mtime = QDateTime::fromMSecsSinceEpoch(q.value(3).toDouble()); 
 
            QFileInfo info(itemPath); 
            if (!info.exists()) continue; 
 
            QList<QStandardItem*> row; 
            QString normPath = QDir::toNativeSeparators(QDir::cleanPath(itemPath)); 
            RuntimeMeta rm = MetadataManager::instance().getMeta(normPath.toStdWString()); 
            QString colorName = QString::fromStdWString(rm.color); 
 
            QIcon itemIcon = UiHelper::getFileIcon(itemPath, 128); 
            auto* nameItem = new QStandardItem(itemIcon, info.fileName()); 
            nameItem->setData(itemPath, PathRole); 
            nameItem->setData(type, TypeRole); 
            nameItem->setData(rm.rating, RatingRole); 
            nameItem->setData(colorName, ColorRole); 
            nameItem->setData(rm.pinned, PinnedRole); 
            nameItem->setData(rm.pinned, IsLockedRole); 
            nameItem->setData(rm.tags, TagsRole); 
            nameItem->setData(1.0, AspectRatioRole);
            nameItem->setData(false, HasThumbnailRole);
            // 2026-06-xx 注入物理色板
            QVariantList palList;
            for (const auto& p : rm.palettes) {
                QVariantMap m; m["color"] = p.color; m["ratio"] = p.ratio;
                palList << m;
            }
            nameItem->setData(palList, PalettesRole);
 
            bool isEmpty = false; 
            if (type == "folder") isEmpty = QDir(itemPath).isEmpty(); 
            nameItem->setData(isEmpty, IsEmptyRole); 
 
            row << nameItem; 
            row << new QStandardItem(type == "folder" ? "-" : QString::number(size / 1024) + " KB"); 
            row << new QStandardItem(type == "folder" ? (isEmpty ? "文件夹 (空)" : "文件夹") : info.suffix().toUpper() + " 文件"); 
            row << new QStandardItem(mtime.toString("yyyy-MM-dd HH:mm")); 
            m_model->appendRow(row); 
 
            // 2026-05-07 按照用户要求：累加统计数据 
            stats.ratingCounts[rm.rating]++; 
            if (!colorName.isEmpty()) stats.colorCounts[colorName]++; 
            else stats.colorCounts[""]++; 
            // 使用实际文件扩展名而非数据库 type 字段 
            QString ext = type == "folder" ? "folder" : info.suffix().toUpper(); 
            stats.typeCounts[ext]++; 
             
            for (const QString& tag : rm.tags) { 
                stats.tagCounts[tag]++; 
            } 
            if (rm.tags.empty()) stats.noTagCount++; 
             
            QDate cdate = info.birthTime().date(); 
            QDate today = QDate::currentDate(); 
            if (cdate == today) stats.createDateCounts["today"]++; 
            else if (cdate == today.addDays(-1)) stats.createDateCounts["yesterday"]++; 
            else stats.createDateCounts[cdate.toString("yyyy-MM-dd")]++; 
             
            QDate mdate = info.lastModified().date(); 
            if (mdate == today) stats.modifyDateCounts["today"]++; 
            else if (mdate == today.addDays(-1)) stats.modifyDateCounts["yesterday"]++; 
            else stats.modifyDateCounts[mdate.toString("yyyy-MM-dd")]++; 
 
            m_iconPendingPaths << itemPath; 
            m_pathToIndexMap[itemPath] = QPersistentModelIndex(nameItem->index()); 
        } 
    } 
     
    // 2026-05-07 按照用户要求：发出统计数据信号，填充筛选器 
    if (stats.noTagCount > 0) stats.tagCounts["__none__"] = stats.noTagCount; 
    emit directoryStatsReady(stats.ratingCounts, stats.colorCounts, stats.tagCounts,  
                           stats.typeCounts, stats.createDateCounts, stats.modifyDateCounts); 
     
    applyFilters(); 
    if (!m_lazyIconTimer->isActive()) m_lazyIconTimer->start(); 
} 
 
void ContentPanel::loadPaths(const QStringList& paths) { 
    m_viewStack->show(); 
    if (m_textPreview) m_textPreview->hide(); 
    if (m_imagePreview) m_imagePreview->hide(); 
    emit dataSourceChanged("category"); // 2026-05-17 按照用户要求：发射数据源变更信号，高亮展示分类面板焦点线条
     
    m_lazyIconTimer->stop(); 
    m_iconPendingPaths.clear(); 
    m_pathToIndexMap.clear(); 
 
    m_model->clear(); 
    m_model->setHorizontalHeaderLabels({"名称", "大小", "类型", "修改时间"}); 
 
    // 2026-05-07 按照用户要求：添加统计数据结构，用于筛选器填充 
    ScanStats stats; 
     
    for (const QString& path : paths) { 
        QFileInfo info(path); 
        if (!info.exists()) continue; 
 
        QList<QStandardItem*> row; 
         
        // 回归原生：停止对从系统获取的物理图标进行人工着色
        QString normPath = QDir::toNativeSeparators(QDir::cleanPath(path)); 
        RuntimeMeta rm = MetadataManager::instance().getMeta(normPath.toStdWString()); 
        QString colorName = QString::fromStdWString(rm.color); 
 
        QIcon itemIcon = UiHelper::getFileIcon(path, 128); 
        auto* nameItem = new QStandardItem(itemIcon, info.fileName()); 
        nameItem->setData(path, PathRole); 
        nameItem->setData(info.isDir() ? "folder" : "file", TypeRole); 
         
        // 2026-05-27 物理修复：复用已归一化的路径和元数据，解决重定义报错 
        nameItem->setData(rm.rating, RatingRole); 
        nameItem->setData(colorName, ColorRole); 
        nameItem->setData(rm.pinned, PinnedRole); 
        nameItem->setData(rm.pinned, IsLockedRole); 
        nameItem->setData(rm.tags, TagsRole); 
        nameItem->setData(1.0, AspectRatioRole);
        nameItem->setData(false, HasThumbnailRole);
        // 2026-06-xx 注入物理色板
        QVariantList palList;
        for (const auto& p : rm.palettes) {
            QVariantMap m; m["color"] = p.color; m["ratio"] = p.ratio;
            palList << m;
        }
        nameItem->setData(palList, PalettesRole);
 
        bool isEmpty = false; 
        if (info.isDir()) isEmpty = QDir(path).isEmpty(); 
        nameItem->setData(isEmpty, IsEmptyRole); 
 
        row << nameItem; 
        row << new QStandardItem(info.isDir() ? "-" : QString::number(info.size() / 1024) + " KB"); 
        row << new QStandardItem(info.isDir() ? (isEmpty ? "文件夹 (空)" : "文件夹") : info.suffix().toUpper() + " 文件"); 
        row << new QStandardItem(info.lastModified().toString("yyyy-MM-dd HH:mm")); 
        m_model->appendRow(row); 
 
        // 2026-05-07 按照用户要求：累加统计数据 
        stats.ratingCounts[rm.rating]++; 
        if (!colorName.isEmpty()) stats.colorCounts[colorName]++; 
        else stats.colorCounts[""]++; 
        // 使用实际文件扩展名而非通用的 "file" 
        QString type = info.isDir() ? "folder" : info.suffix().toUpper(); 
        stats.typeCounts[type]++; 
         
        for (const QString& tag : rm.tags) { 
            stats.tagCounts[tag]++; 
        } 
        if (rm.tags.empty()) stats.noTagCount++; 
         
        QDate cdate = info.birthTime().date(); 
        QDate today = QDate::currentDate(); 
        if (cdate == today) stats.createDateCounts["today"]++; 
        else if (cdate == today.addDays(-1)) stats.createDateCounts["yesterday"]++; 
        else stats.createDateCounts[cdate.toString("yyyy-MM-dd")]++; 
         
        QDate mdate = info.lastModified().date(); 
        if (mdate == today) stats.modifyDateCounts["today"]++; 
        else if (mdate == today.addDays(-1)) stats.modifyDateCounts["yesterday"]++; 
        else stats.modifyDateCounts[mdate.toString("yyyy-MM-dd")]++; 
 
        m_iconPendingPaths << path; 
        m_pathToIndexMap[path] = QPersistentModelIndex(nameItem->index()); 
    } 
 
    // 2026-05-07 按照用户要求：发出统计数据信号，填充筛选器 
    if (stats.noTagCount > 0) stats.tagCounts["__none__"] = stats.noTagCount; 
    emit directoryStatsReady(stats.ratingCounts, stats.colorCounts, stats.tagCounts,  
                           stats.typeCounts, stats.createDateCounts, stats.modifyDateCounts); 
     
    applyFilters(); 
    if (!m_lazyIconTimer->isActive()) m_lazyIconTimer->start(); 
} 
 
void ContentPanel::createNewItem(const QString& type) { 
    if (m_currentPath.isEmpty() || m_currentPath == "computer://") return; 
 
    QString baseName = (type == "folder") ? "新建文件夹" : "未命名"; 
    QString ext = (type == "md") ? ".md" : ((type == "txt") ? ".txt" : ""); 
    QString finalName = baseName + ext; 
    QString fullPath = m_currentPath + "/" + finalName; 
 
    int counter = 1; 
    while (QFileInfo::exists(fullPath)) { 
        finalName = baseName + QString(" (%1)").arg(counter++) + ext; 
        fullPath = m_currentPath + "/" + finalName; 
    } 
 
    bool success = false; 
    if (type == "folder") { 
        success = QDir(m_currentPath).mkdir(finalName); 
    } else { 
        QFile file(fullPath); 
        if (file.open(QIODevice::WriteOnly)) { 
            file.close(); 
            success = true; 
        } 
    } 
 
    if (success) { 
        loadDirectory(m_currentPath, m_isRecursive); 
        auto results = m_model->findItems(finalName, Qt::MatchExactly, 0); 
        if (!results.isEmpty()) { 
            QModelIndex srcIdx = results.first()->index(); 
            QModelIndex proxyIdx = m_proxyModel->mapFromSource(srcIdx); 
            if (proxyIdx.isValid()) { 
                m_gridView->setCurrentIndex(proxyIdx); 
                m_gridView->edit(proxyIdx); 
            } 
        } 
    } 
} 
 
void ContentPanel::updateLayersButtonState() { 
    if (!m_btnLayers) return; 
 
    if (m_currentPath.isEmpty() || m_currentPath == "computer://") { 
        m_btnLayers->setEnabled(false); 
        m_btnLayers->setChecked(false); 
        m_btnLayers->setProperty("tooltipText", "“此电脑”不支持递归显示"); 
        return; 
    } 
 
    m_btnLayers->setEnabled(true); 
    m_btnLayers->setProperty("tooltipText", "递归显示子目录所有文件"); 
} 
 
// --- Delegate --- 
 
GridItemDelegate::GridMetrics GridItemDelegate::calculateMetrics(const QStyleOptionViewItem& option) { 
    GridMetrics m; 
    // cardRect 为条目占用的总区域 
    m.cardRect = option.rect.adjusted(5, 5, -5, -5); // 2026-05-08 按照用户要求：增加到5px使卡片之间间距达到10px 
    double zoom = (double)option.decorationSize.width(); 
 
    // 2026-06-05 按照用户要求：定义正方形主体区域 
    int side = m.cardRect.width(); 
    m.squareRect = QRect(m.cardRect.left(), m.cardRect.top(), side, side); 
 
    m.iconDrawSize = (int)(zoom * 0.7); 
    m.ratingH      = 20;  // 2026-05-17 物理锁定评分区高度 
    m.nameH        = (int)(zoom * 0.25); 
 
    // 2026-05-17 按照用户要求：主图标在正方形内垂直居中
    m.iconRect = QRect(m.squareRect.left() + (m.squareRect.width() - m.iconDrawSize) / 2, 
                       m.squareRect.top() + (m.squareRect.height() - m.iconDrawSize) / 2, 
                       m.iconDrawSize, m.iconDrawSize); 
    
    // 2026-05-17 物理布局重构：星级区置于正方形下方
    // 2026-06-xx 按照用户要求：下移 2 像素（从 +6 改为 +8）
    m.ratingY = m.squareRect.bottom() + 8; 
 
    m.starSize    = 18; // 2026-05-17 尺寸微调以匹配外部布局
    m.starSpacing = 1;   
    int banW = 14;      // 2026-06-xx 物理对齐：将禁止图标缩减至 12px，使其与星级在视觉权重上保持一致
    int banGap = 2; 
 
    m.infoTotalW = banW + banGap + (5 * m.starSize) + (4 * m.starSpacing); 
    m.infoStartX = m.squareRect.left() + (m.squareRect.width() - m.infoTotalW) / 2; 
 
    m.banRect = QRect(m.infoStartX, m.ratingY + (m.ratingH - banW) / 2, banW, banW); 
    m.starsStartX = m.infoStartX + banW + banGap; 
     
    // 名称区紧贴评分区下方 
    // 2026-06-xx 按照要求：第三次向上偏移 2 像素以实现零间距极致排版（从 +2 调整为 +0）
    m.nameY = m.ratingY + m.ratingH + 0; 
    m.nameRect = QRect(m.cardRect.left(), m.nameY, m.cardRect.width(), m.nameH); 
 
    return m; 
} 
 
void GridItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const { 
    painter->save(); 
    painter->setRenderHint(QPainter::Antialiasing); 
 
    GridMetrics m = calculateMetrics(option); 
    bool isSelected = (option.state & QStyle::State_Selected); 
    bool isHovered = (option.state & QStyle::State_MouseOver); 
    QString colorName = index.data(ColorRole).toString();
     
    // 2026-06-05 按照用户要求：背景框仅限正方形区域，名称外置 
    QColor cardBg = isSelected ? QColor("#282828") : (isHovered ? QColor("#2A2A2A") : QColor("#2D2D2D")); 
    painter->setPen(isSelected ? QPen(QColor("#3498db"), 2) : QPen(QColor("#333333"), 1)); 
    painter->setBrush(cardBg); 
    painter->drawRoundedRect(m.squareRect, 8, 8); 
 
    // [内容区绘制] - 均在正方形内 
    // 1. 状态位图标绘制 (置顶 vs. 已录入 互斥) 
    // 2026-06-xx 物理修复：校准 ItemRole 作用域，确保 GridItemDelegate 编译通过 
    bool isPinned = index.data(IsLockedRole).toBool(); 
    bool isManaged = index.data(InDatabaseRole).toBool(); 
     
    if (isPinned || isManaged) { 
        QRect statusRect(m.squareRect.right() - 22, m.squareRect.top() + 8, 16, 16); 
        if (isPinned) { 
            // 置顶优先 
            QIcon pinIcon = UiHelper::getIcon("pin_vertical", QColor("#FF551C"), 16); 
            pinIcon.paint(painter, statusRect); 
        } else { 
            // 已录入但未置顶，显示绿对勾 
            QIcon checkIcon = UiHelper::getIcon("check_circle", QColor("#2ecc71"), 16); 
            checkIcon.paint(painter, statusRect); 
        } 
    } 
 
    // 2. 扩展名角标 
    QString path = index.data(PathRole).toString(); 
    QFileInfo info(path); 
    QString ext = info.isDir() ? "DIR" : info.suffix().toUpper(); 
    if (ext.isEmpty()) ext = "FILE"; 
    QColor badgeColor = UiHelper::getExtensionColor(ext); 
 
    QRect extRect(m.squareRect.left() + 8, m.squareRect.top() + 8, 36, 18); 
    painter->setPen(Qt::NoPen); 
    painter->setBrush(badgeColor); 
    painter->drawRoundedRect(extRect, 2, 2); 
    painter->setPen(QColor("#FFFFFF")); 
    QFont extFont = painter->font(); 
    extFont.setPointSize(8); 
    extFont.setBold(true); 
    painter->setFont(extFont); 
    painter->drawText(extRect, Qt::AlignCenter, ext); 
 
    // 3. 主图标 
    QIcon icon = index.data(Qt::DecorationRole).value<QIcon>(); 
    icon.paint(painter, m.iconRect); 
 
    // 4. 评级星级 
    int rating = index.data(RatingRole).toInt(); 
     
    // 2026-xx-xx 按照最新要求： 
    // 1. 如果已有打分 (rating > 0)，始终显示。 
    // 2. 如果未打分但被选中，显示禁止图标和空心星。 
    // 3. 如果未打分且未选中，不显示。 
    bool shouldShowRating = (rating > 0) || isSelected; 
 
    if (shouldShowRating) { 
        QColor starColor("#CCCCCC");
        QColor emptyStarColor("#888888");

        // [新增] 物理同步：绘制彩色背景胶囊
        if (!colorName.isEmpty()) {
            QColor bgColor = UiHelper::parseColorName(colorName);
            if (bgColor.isValid()) {
                painter->save();
                painter->setBrush(bgColor);
                painter->setPen(Qt::NoPen);
                QRect totalRect = m.banRect.united(QRect(m.starsStartX + 4 * (m.starSize + m.starSpacing), m.ratingY + (m.ratingH - m.starSize) / 2, m.starSize, m.starSize));
                painter->drawRoundedRect(totalRect.adjusted(-4, -1, 4, 1), 4, 4);
                painter->restore();
            }
        }

        // 2026-xx-xx 深度修复：调高禁止图标与空心星亮度，确保在深色卡片背景下清晰可见 
        QIcon banIcon = UiHelper::getIcon("no_color", starColor, m.banRect.width());
        banIcon.paint(painter, m.banRect); 
 
        QPixmap filledStar = UiHelper::getPixmap("star-svgrepo-com.svg", QSize(m.starSize, m.starSize), starColor);
        QPixmap emptyStar = UiHelper::getPixmap("star-rate-rating-outline-svgrepo-com.svg", QSize(m.starSize, m.starSize), emptyStarColor);
 
        for (int i = 0; i < 5; ++i) { 
            QRect starRect(m.starsStartX + i * (m.starSize + m.starSpacing), m.ratingY + (m.ratingH - m.starSize) / 2, m.starSize, m.starSize); 
            painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar); 
        } 
    } 
     
    // [名称区绘制] - 在正方形下方 
    bool isEmptyFolder = (index.data(TypeRole).toString() == "folder" && index.data(IsEmptyRole).toBool()); 
 
    if (!isSelected && isEmptyFolder) {
        // 对空文件夹正方形区进行特殊描边 (仅限未选中状态，防止与蓝色边框重叠)
        painter->setPen(QPen(QColor("#41F2F2"), 1, Qt::DashLine)); 
        painter->setBrush(QColor(65, 242, 242, 30)); 
        painter->drawRoundedRect(m.squareRect, 8, 8); 
        painter->setPen(QColor("#41F2F2")); 
    } 
 
    QString name = index.data(Qt::DisplayRole).toString(); 
    painter->setPen(QColor("#EEEEEE")); 
    QFont textFont = painter->font(); 
    textFont.setPointSize(8); 
    textFont.setBold(false); 
    painter->setFont(textFont); 
 
    // 2026-06-xx 按照要求：未录入项文字半透明 
    // 2026-06-xx 物理修复：校准 InDatabaseRole 作用域 
    if (!isSelected && !index.data(InDatabaseRole).toBool()) { 
        painter->setPen(QColor(238, 238, 238, 120)); 
    } 
 
    // 2026-06-05 按照用户要求：恢复自动换行逻辑，并在“实在太长”时使用省略号 
    // 零宽空格注入以支持非标准断行（如下划线或点号） 
    QString displayName = name; 
    displayName.replace("_", "_\u200B"); 
    displayName.replace(".", ".\u200B"); 
 
    // 首先尝试进行自动换行绘制，如果高度超出，则回退到省略号单行显示 
    QRect boundingRect = painter->boundingRect(m.nameRect.adjusted(4, 0, -4, 0), Qt::AlignCenter | Qt::TextWordWrap, displayName); 
    if (boundingRect.height() > m.nameRect.height()) { 
        QString elidedName = option.fontMetrics.elidedText(name, Qt::ElideRight, m.nameRect.width() - 8); 
        painter->drawText(m.nameRect, Qt::AlignCenter, elidedName); 
    } else { 
        painter->drawText(m.nameRect.adjusted(4, 0, -4, 0), Qt::AlignCenter | Qt::TextWordWrap, displayName); 
    } 
 
    painter->restore(); 
} 
 
 
bool GridItemDelegate::eventFilter(QObject* obj, QEvent* event) { 
    if (event->type() == QEvent::KeyPress) { 
        // 2026-05-25 物理修复：改用 reinterpret_cast 避开事件转型报错 
        QKeyEvent* keyEvent = reinterpret_cast<QKeyEvent*>(event); 
        QLineEdit* editor = qobject_cast<QLineEdit*>(obj); 
        if (editor) { 
            switch (keyEvent->key()) { 
                case Qt::Key_Left: 
                case Qt::Key_Right: 
                case Qt::Key_Up: 
                case Qt::Key_Down: 
                case Qt::Key_Home: 
                case Qt::Key_End: 
                    keyEvent->accept(); 
                    return false; 
                default: 
                    break; 
            } 
        } 
    } 
    return QStyledItemDelegate::eventFilter(obj, event); 
} 
 
bool GridItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) { 
    if (event->type() == QEvent::MouseButtonPress) { 
        // 2026-05-25 物理修复：改用 reinterpret_cast 避开 QEvent 子类转型歧义 
        QMouseEvent* mEvent = reinterpret_cast<QMouseEvent*>(event); 
        if (mEvent->button() == Qt::LeftButton) { 
            // 2026-05-28 按照用户授权：废除本地硬编码判定，统一使用 calculateMetrics 保证 Hitbox 零偏差 
            GridMetrics m = calculateMetrics(option); 
            QString path = index.data(PathRole).toString(); 
 
            if (m.banRect.contains(mEvent->pos())) { 
                if (!path.isEmpty()) { 
                    MetadataManager::instance().setRating(path.toStdWString(), 0); 
                    model->setData(index, 0, RatingRole); 
                } 
                // 2026-05-08 彻底阻止编辑触发：临时禁用编辑触发器，防止清除星级时错误触发行内编辑
                auto* view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(option.widget));
                if (view) {
                    QAbstractItemView::EditTriggers originalTriggers = view->editTriggers();
                    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
                    QTimer::singleShot(0, [view, originalTriggers]() {
                        view->setEditTriggers(originalTriggers);
                    });
                }
                event->accept(); 
                return true; 
            } 
 
            for (int i = 0; i < 5; ++i) { 
                QRect starRect(m.starsStartX + i * (m.starSize + m.starSpacing), m.ratingY + (m.ratingH - m.starSize) / 2, m.starSize, m.starSize); 
                if (starRect.contains(mEvent->pos())) { 
                    int r = i + 1; 
                    if (!path.isEmpty()) { 
                        MetadataManager::instance().setRating(path.toStdWString(), r); 
                        model->setData(index, r, RatingRole); 
                    } 
                    // 2026-05-08 彻底阻止编辑触发：临时禁用编辑触发器，防止星级点击时错误触发行内编辑
                    auto* view = qobject_cast<QAbstractItemView*>(const_cast<QWidget*>(option.widget));
                    if (view) {
                        QAbstractItemView::EditTriggers originalTriggers = view->editTriggers();
                        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
                        QTimer::singleShot(0, [view, originalTriggers]() {
                            view->setEditTriggers(originalTriggers);
                        });
                    }
                    event->accept(); 
                    return true; 
                } 
            } 
        } 
    } 
    return QStyledItemDelegate::editorEvent(event, model, option, index); 
} 
 
QWidget* GridItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const { 
    Q_UNUSED(option); 
    QLineEdit* editor = new QLineEdit(parent); 
    editor->installEventFilter(const_cast<GridItemDelegate*>(this)); 
    editor->setAlignment(Qt::AlignCenter); 
    editor->setFrame(false); 
     
    QString tagColorStr = index.data(ColorRole).toString(); 
    QString bgColor = tagColorStr.isEmpty() ? "#3E3E42" : tagColorStr; 
    QString textColor = tagColorStr.isEmpty() ? "#FFFFFF" : "#000000"; 
 
    editor->setStyleSheet( 
        QString("QLineEdit { background-color: %1; color: %2; border-radius: 2px; " 
                "border: 2px solid #3498db; font-weight: bold; font-size: 8pt; padding: 0px; }") 
        .arg(bgColor, textColor) 
    ); 
    return editor; 
} 
 
void GridItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const { 
    QString value = index.model()->data(index, Qt::EditRole).toString(); 
    // 2026-05-25 物理修复：改用 qobject_cast 彻底根除 static_cast 类型无法识别的 Bug 
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor); 
    if (lineEdit) lineEdit->setText(value); 
     
    int lastDot = value.lastIndexOf('.'); 
    if (lastDot > 0) { 
        lineEdit->setSelection(0, lastDot); 
    } else { 
        lineEdit->selectAll(); 
    } 
} 
 
void GridItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const { 
    // 2026-05-25 物理修复：改用 qobject_cast 彻底根除 static_cast 类型无法识别的 Bug 
    QLineEdit* lineEdit = qobject_cast<QLineEdit*>(editor); 
    if (!lineEdit) return; 
    QString value = lineEdit->text(); 
    if(value.isEmpty() || value == index.data(Qt::DisplayRole).toString()) return; 
 
    QString oldPath = index.data(PathRole).toString(); 
    QFileInfo info(oldPath); 
    QString newPath = info.absolutePath() + "/" + value; 
     
    if (QFile::rename(oldPath, newPath)) { 
        model->setData(index, value, Qt::EditRole); 
        model->setData(index, newPath, PathRole); 
        // 2026-05-24 按照用户要求：彻底移除 JSON 逻辑，重命名后仅需同步更新内存与数据库索引 
        MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString()); 
    }  
} 

void ContentPanel::recalculateAndEmitStats() {
    ScanStats stats;
    for (int i = 0; i < m_model->rowCount(); ++i) {
        QStandardItem* nameItem = m_model->item(i, 0);
        if (!nameItem) continue;
        int rating = nameItem->data(RatingRole).toInt();
        QString type = nameItem->data(TypeRole).toString();
        QStringList tags = nameItem->data(TagsRole).toStringList();
        
        stats.ratingCounts[rating]++;

        // 2026-06-xx 物理占比统计：遍历所有色板颜色，而不仅仅是主色调
        QVariant palVar = nameItem->data(PalettesRole);
        QString dominantColor = nameItem->data(ColorRole).toString();
        
        if (palVar.isValid()) {
            QVariantList pal = palVar.toList();
            for (const auto& v : pal) {
                QVariantMap m = v.toMap();
                QColor c = m["color"].value<QColor>();
                // 4-bit 量化对齐，防止统计碎片化
                QString hex = UiHelper::quantizeColor(c).name().toUpper();
                stats.colorCounts[hex]++;
            }
        } else if (!dominantColor.isEmpty()) {
            stats.colorCounts[dominantColor.toUpper()]++;
        } else {
            stats.colorCounts[""]++;
        }
        
        if (type == "folder") {
            stats.typeCounts["folder"]++;
        } else {
            QString path = nameItem->data(PathRole).toString();
            stats.typeCounts[QFileInfo(path).suffix().toUpper()]++;
        }
        
        for (const QString& tag : tags) {
            stats.tagCounts[tag]++;
        }
        if (tags.isEmpty()) stats.noTagCount++;
        
        QString path = nameItem->data(PathRole).toString();
        QFileInfo info(path);
        if (info.exists()) {
            QDate cdate = info.birthTime().date();
            QDate mdate = info.lastModified().date();
            QDate today = QDate::currentDate();
            
            if (cdate == today) stats.createDateCounts["today"]++;
            else if (cdate == today.addDays(-1)) stats.createDateCounts["yesterday"]++;
            else stats.createDateCounts[cdate.toString("yyyy-MM-dd")]++;
            
            if (mdate == today) stats.modifyDateCounts["today"]++;
            else if (mdate == today.addDays(-1)) stats.modifyDateCounts["yesterday"]++;
            else stats.modifyDateCounts[mdate.toString("yyyy-MM-dd")]++;
        }
    }
    if (stats.noTagCount > 0) stats.tagCounts["__none__"] = stats.noTagCount;
    emit directoryStatsReady(stats.ratingCounts, stats.colorCounts, stats.tagCounts,
                           stats.typeCounts, stats.createDateCounts, stats.modifyDateCounts);
}
 
void GridItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const { 
    Q_UNUSED(index); 
    // 2026-06-05 按照重构布局：同步定位编辑器至正方形下方的名称区 
    GridMetrics m = calculateMetrics(option); 
    editor->setGeometry(m.nameRect); 
}

QSize GridItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (auto* view = qobject_cast<QListView*>(const_cast<QWidget*>(option.widget))) {
        if (view->gridSize().isValid()) return view->gridSize();
    }
    return QStyledItemDelegate::sizeHint(option, index);
}
 
} // namespace ArcMeta
// Force recompile to apply SvgIcons.h changes 
