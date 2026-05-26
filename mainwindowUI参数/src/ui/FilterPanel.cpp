#include "FilterPanel.h"
#include "../core/DatabaseManager.h"
#include "IconHelper.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QApplication>
#include <QTimer>
#include <QtConcurrent>

FilterPanel::FilterPanel(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    setMinimumSize(230, 350);
    initUI();
    setupTree();

    connect(&m_statsWatcher, &QFutureWatcher<QVariantMap>::finished, this, &FilterPanel::onStatsReady);
}

void FilterPanel::initUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 内容容器
    auto* contentWidget = new QWidget();
    contentWidget->setStyleSheet(
        "QWidget { "
        "  background-color: transparent; "
        "  border: none; "
        "  border-bottom-left-radius: 0px; "
        "  border-bottom-right-radius: 0px; "
        "}"
    );
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(10, 8, 10, 10);
    contentLayout->setSpacing(8);

    // 树形筛选器
    m_tree = new QTreeWidget();
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(0);
    m_tree->setFocusPolicy(Qt::NoFocus);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAnimated(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setStyleSheet(
        "QTreeWidget {"
        "  background-color: transparent;"
        "  color: #ddd;"
        "  border: none;"
        "  font-size: 12px;"
        "}"
        "QTreeWidget::branch { image: none; border: none; width: 0px; }"
        "QTreeWidget::item {"
        "  height: 28px;"
        "  border-radius: 4px;"
        "  margin-left: 10px;"
        "  margin-right: 10px;"
        "  padding-left: 2px;"
        "}"
        "QTreeWidget::item:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
        "QTreeWidget::item:selected { background-color: #3e3e42; color: white; }" // 2026-03-xx 统一选中色
        "QTreeWidget::indicator {"
        "  width: 14px;"
        "  height: 14px;"
        "  margin-left: 20px;"
        "}"
        "QScrollBar:vertical { border: none; background: transparent; width: 6px; margin: 0px; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::handle:vertical:hover { background: #555; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    );
    connect(m_tree, &QTreeWidget::itemChanged, this, &FilterPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &FilterPanel::onItemClicked);
    m_tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentLayout->addWidget(m_tree);

    // 底部区域
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(4);

    m_btnReset = new QPushButton(" 重置");
    m_btnReset->setIcon(IconHelper::getIcon("refresh", "white"));
    m_btnReset->setCursor(Qt::PointingHandCursor);
    m_btnReset->setFixedWidth(80);
    m_btnReset->setStyleSheet(
        "QPushButton {"
        "  background-color: #252526;"
        "  border: 1px solid #444;"
        "  color: #888;"
        "  border-radius: 6px;"
        "  padding: 8px;"
        "  font-size: 12px;"
        "}"
        "QPushButton:hover { color: #ddd; background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
    );
    connect(m_btnReset, &QPushButton::clicked, this, &FilterPanel::resetFilters);
    bottomLayout->addWidget(m_btnReset);
    bottomLayout->addStretch();

    contentLayout->addLayout(bottomLayout);
    mainLayout->addWidget(contentWidget);
}

void FilterPanel::setupTree() {
    struct Section {
        QString key;
        QString label;
        QString icon;
        QString color;
    };

    QList<Section> sections = {
        {"stars", "评级", "star_filled", "#f39c12"},
        {"date_create", "创建日期", "today", "#2ecc71"},
        {"date_update", "修改日期", "clock", "#9b59b6"},
        {"colors", "颜色", "palette", "#e91e63"},
        {"types", "类型", "folder", "#3498db"},
        {"tags", "标签", "tag", "#e67e22"}
    };

    QFont headerFont = m_tree->font();
    headerFont.setBold(true);

    for (const auto& sec : sections) {
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(0, sec.label);
        item->setIcon(0, IconHelper::getIcon(sec.icon, sec.color));
        item->setExpanded(true);
        item->setFlags(Qt::ItemIsEnabled);
        item->setFont(0, headerFont);
        item->setForeground(0, QBrush(Qt::gray));
        m_roots[sec.key] = item;
    }
}

void FilterPanel::updateStats(const QString& keyword, const QString& type, const QVariant& value) {
    // [PERF] 性能优化：将耗时的 FTS5 聚合统计移至后台线程，防止搜索时 UI 线程假死。
    if (m_statsWatcher.isRunning()) {
        m_statsWatcher.cancel();
    }

    m_pendingKeyword = keyword;
    m_pendingType = type;
    m_pendingValue = value;

    auto future = QtConcurrent::run([keyword, type, value]() {
        return DatabaseManager::instance().getFilterStats(keyword, type, value);
    });
    m_statsWatcher.setFuture(future);
}

void FilterPanel::onStatsReady() {
    QVariantMap stats = m_statsWatcher.result();
    if (stats.isEmpty()) return;

    m_tree->blockSignals(true);
    m_blockItemClick = true;

    // 1. 评级
    QList<QVariantMap> starData;
    QVariantMap starStats = stats["stars"].toMap();
    for (int i = 5; i >= 1; --i) {
        int count = starStats[QString::number(i)].toInt();
        if (count > 0) {
            QVariantMap item;
            item["key"] = QString::number(i);
            item["label"] = QString(i, QChar(0x2605)); // ★
            item["count"] = count;
            starData.append(item);
        }
    }
    if (starStats["0"].toInt() > 0) {
        QVariantMap item;
        item["key"] = "0";
        item["label"] = "无评级";
        item["count"] = starStats["0"].toInt();
        starData.append(item);
    }
    refreshNode("stars", starData);

    // 2. 颜色
    QList<QVariantMap> colorData;
    QVariantMap colorStats = stats["colors"].toMap();
    for (auto it = colorStats.begin(); it != colorStats.end(); ++it) {
        int count = it.value().toInt();
        if (count > 0) {
            QVariantMap item;
            item["key"] = it.key();
            item["label"] = it.key();
            item["count"] = count;
            colorData.append(item);
        }
    }
    refreshNode("colors", colorData, true);

    // 3. 类型
    QMap<QString, QString> typeMap = {{"text", "文本"}, {"image", "图片"}, {"file", "文件"}};
    QList<QVariantMap> typeData;
    QVariantMap typeStats = stats["types"].toMap();
    for (auto it = typeStats.begin(); it != typeStats.end(); ++it) {
        int count = it.value().toInt();
        if (count > 0) {
            QVariantMap item;
            item["key"] = it.key();
            item["label"] = typeMap.value(it.key(), it.key());
            item["count"] = count;
            typeData.append(item);
        }
    }
    refreshNode("types", typeData);

    // 4. 标签
    QList<QVariantMap> tagData;
    QVariantMap tagStats = stats["tags"].toMap();
    for (auto it = tagStats.begin(); it != tagStats.end(); ++it) {
        QVariantMap item;
        item["key"] = it.key();
        item["label"] = it.key();
        item["count"] = it.value().toInt();
        tagData.append(item);
    }
    refreshNode("tags", tagData);

    // 5. 创建日期与修改日期辅助逻辑
    QDate today = QDate::currentDate();
    auto processDateStats = [&](const QString& key, const QString& statsKey) {
        QList<QVariantMap> dateData;
        QVariantMap dateStats = stats[statsKey].toMap();
        QStringList sortedDates = dateStats.keys();
        std::sort(sortedDates.begin(), sortedDates.end(), std::greater<QString>());

        for (const QString& dateStr : sortedDates) {
            QDate date = QDate::fromString(dateStr, "yyyy-MM-dd");
            QString label;
            qint64 daysTo = date.daysTo(today);
            if (daysTo == 0) label = "今天";
            else if (daysTo == 1) label = "昨天";
            else if (daysTo == 2) label = "2 天前";
            else label = date.toString("yyyy/M/d");

            QVariantMap item;
            item["key"] = dateStr;
            item["label"] = label;
            item["count"] = dateStats[dateStr].toInt();
            dateData.append(item);
        }
        refreshNode(key, dateData);
    };

    processDateStats("date_create", "date_create");
    processDateStats("date_update", "date_update");

    m_blockItemClick = false;
    m_tree->blockSignals(false);
}

void FilterPanel::refreshNode(const QString& key, const QList<QVariantMap>& items, bool isCol) {
    if (!m_roots.contains(key)) return;
    auto* root = m_roots[key];

    // 建立现有的 key -> item 映射
    QMap<QString, QTreeWidgetItem*> existingItems;
    for (int i = 0; i < root->childCount(); ++i) {
        auto* child = root->child(i);
        existingItems[child->data(0, Qt::UserRole).toString()] = child;
    }

    QSet<QString> currentKeys;
    for (const auto& data : items) {
        QString itemKey = data["key"].toString();
        QString label = data["label"].toString();
        int count = data["count"].toInt();
        currentKeys.insert(itemKey);

        QString newText = QString("%1 (%2)").arg(label).arg(count);
        if (existingItems.contains(itemKey)) {
            auto* child = existingItems[itemKey];
            if (child->text(0) != newText) {
                child->setText(0, newText);
            }
        } else {
            auto* child = new QTreeWidgetItem(root);
            child->setText(0, newText);
            child->setData(0, Qt::UserRole, itemKey);
            child->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            child->setCheckState(0, Qt::Unchecked);
            
            if (isCol) {
                child->setIcon(0, IconHelper::getIcon("circle_filled", itemKey)); // 颜色项仍保留颜色圆点
            }
        }
    }

    // 移除不再需要的项目
    for (int i = root->childCount() - 1; i >= 0; --i) {
        auto* child = root->child(i);
        if (!currentKeys.contains(child->data(0, Qt::UserRole).toString())) {
            delete root->takeChild(i);
        }
    }
}


QVariantMap FilterPanel::getCheckedCriteria() const {
    QVariantMap criteria;
    for (auto it = m_roots.begin(); it != m_roots.end(); ++it) {
        QStringList checked;
        for (int i = 0; i < it.value()->childCount(); ++i) {
            auto* item = it.value()->child(i);
            if (item->checkState(0) == Qt::Checked) {
                checked << item->data(0, Qt::UserRole).toString();
            }
        }
        if (!checked.isEmpty()) {
            criteria[it.key()] = checked;
        }
    }
    return criteria;
}

void FilterPanel::resetFilters() {
    m_tree->blockSignals(true);
    for (auto* root : m_roots) {
        for (int i = 0; i < root->childCount(); ++i) {
            root->child(i)->setCheckState(0, Qt::Unchecked);
        }
    }
    m_tree->blockSignals(false);
    emit filterChanged();
}

void FilterPanel::onItemChanged(QTreeWidgetItem* item, int column) {
    if (m_blockItemClick) return;
    
    // 记录最近改变的项，用于防止 onItemClicked 重复处理
    m_lastChangedItem = item;
    QTimer::singleShot(100, [this]() { m_lastChangedItem = nullptr; });
    
    emit filterChanged();
}

void FilterPanel::onItemClicked(QTreeWidgetItem* item, int column) {
    if (!item) return;

    // 如果该项刚刚由 Qt 原生机制改变了状态（点击了复选框），则忽略此次点击事件
    if (m_lastChangedItem == item) return;

    if (item->parent() == nullptr) {
        item->setExpanded(!item->isExpanded());
    } else if (item->flags() & Qt::ItemIsUserCheckable) {
        m_blockItemClick = true;
        Qt::CheckState state = item->checkState(0);
        item->setCheckState(0, (state == Qt::Checked) ? Qt::Unchecked : Qt::Checked);
        m_blockItemClick = false;
        emit filterChanged();
    }
}
