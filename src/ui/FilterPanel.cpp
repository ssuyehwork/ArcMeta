#include "FilterPanel.h"
#include "ToolTipOverlay.h"
#include "UiHelper.h"
#include <QPushButton>
#include <QMouseEvent>
#include <QCursor>

namespace ArcMeta {

// ─── 颜色映射表 ────────────────────────────────────────────────────
QMap<QString, QColor> FilterPanel::s_colorMap() {
    return {
        { "",       QColor("#888780") },
        { "red",    QColor("#E24B4A") },
        { "orange", QColor("#EF9F27") },
        { "yellow", QColor("#FAC775") },
        { "green",  QColor("#639922") },
        { "cyan",   QColor("#1D9E75") },
        { "blue",   QColor("#378ADD") },
        { "purple", QColor("#7F77DD") },
        { "gray",   QColor("#5F5E5A") },
    };
}

static QString colorDisplayName(const QString& key) {
    static QMap<QString, QString> n {
        { "",       "无色标" }, { "red",    "红色" },
        { "orange", "橙色"  }, { "yellow", "黄色" },
        { "green",  "绿色"  }, { "cyan",   "青色" },
        { "blue",   "蓝色"  }, { "purple", "紫色" },
        { "gray",   "灰色"  },
    };
    return n.value(key, key);
}

static QString ratingDisplayName(int r) {
    return r == 0 ? "无评级" : QString("★").repeated(r);
}

// ─── 可整行点击的行控件 ────────────────────────────────────────────
/**
 * ClickableRow: 点击行内任意位置均触发关联 QCheckBox 的 toggle。
 * 复选框本身的点击事件不需要额外处理，它会自然传播。
 */
class ClickableRow : public QWidget {
public:
    explicit ClickableRow(QCheckBox* cb, QWidget* parent = nullptr)
        : QWidget(parent), m_cb(cb) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_StyledBackground);
    }
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            // 如果点击位置不在复选框上，手动 toggle，避免双重触发
            QPoint local = m_cb->mapFromGlobal(e->globalPosition().toPoint());
            if (!m_cb->rect().contains(local)) {
                m_cb->setChecked(!m_cb->isChecked());
            }
        }
        QWidget::mousePressEvent(e);
    }
    void enterEvent(QEnterEvent* e) override {
        setStyleSheet("QWidget { background: #2A2A2A; border-radius: 4px; }");
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        setStyleSheet("");
        QWidget::leaveEvent(e);
    }
private:
    QCheckBox* m_cb;
};

// ─── FilterPanel ──────────────────────────────────────────────────
FilterPanel::FilterPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("FilterContainer");
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(230);
    
    // 核心修正：移除宽泛的 QWidget QSS，防止其屏蔽 MainWindow 赋予的 ID 边框样式
    // 统一将文字颜色设为 #EEEEEE
    setStyleSheet("color: #EEEEEE;");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 顶部标题栏
    QWidget* topBar = new QWidget(this);
    topBar->setObjectName("ContainerHeader");
    topBar->setFixedHeight(32);
    // 重新注入标题栏样式，确保背景色和边框还原
    topBar->setStyleSheet(
        "QWidget#ContainerHeader {"
        "  background-color: #252526;"
        "  border-bottom: 1px solid #333;"
        "}"
    );
    QHBoxLayout* topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(15, 2, 15, 0); // 严格还原 15px 左右边距，顶部 2px 偏移以垂直居中
    topL->setSpacing(8);

    QLabel* iconLabel = new QLabel(topBar);
    iconLabel->setPixmap(UiHelper::getIcon("filter", QColor("#f1c40f"), 18).pixmap(18, 18));
    topL->addWidget(iconLabel);

    QLabel* title = new QLabel("筛选", topBar);
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #f1c40f; background: transparent; border: none;");

    m_btnClearAll = new QPushButton("清除", topBar);
    m_btnClearAll->setFixedSize(42, 22);
    m_btnClearAll->setProperty("tooltipText", "重置所有筛选条件");
    m_btnClearAll->installEventFilter(this);
    m_btnClearAll->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px;"
        "              color: #AAAAAA; font-size: 11px; }"
        "QPushButton:hover { background: #3e3e42; color: #EEEEEE; }");
    connect(m_btnClearAll, &QPushButton::clicked, this, &FilterPanel::clearAllFilters);

    topL->addWidget(title);
    topL->addStretch();
    topL->addWidget(m_btnClearAll);
    m_mainLayout->addWidget(topBar);

    // 滚动内容区
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_container = new QWidget(m_scrollArea);
    m_container->setStyleSheet("QWidget { background: transparent; }");
    m_containerLayout = new QVBoxLayout(m_container);
    // 恢复旧版边距：右侧和底部留出 10px 缓冲空间
    m_containerLayout->setContentsMargins(0, 0, 10, 10);
    m_containerLayout->setSpacing(0);
    m_containerLayout->addStretch();

    m_scrollArea->setWidget(m_container);
    m_mainLayout->addWidget(m_scrollArea, 1);
}

// 2026-03-xx 按照用户要求：物理拦截事件以实现自定义 ToolTipOverlay 的显隐控制
bool FilterPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 物理级别禁绝原生 ToolTip，强制调用 ToolTipOverlay
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
        }
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::MouseButtonPress) {
        ToolTipOverlay::hideTip();
    }
    return QWidget::eventFilter(watched, event);
}

// ─── populate ─────────────────────────────────────────────────────
void FilterPanel::populate(
    const QMap<int, int>&       ratingCounts,
    const QMap<QString, int>&   colorCounts,
    const QMap<QString, int>&   tagCounts,
    const QMap<QString, int>&   typeCounts,
    const QMap<QString, int>&   createDateCounts,
    const QMap<QString, int>&   modifyDateCounts)
{
    m_ratingCounts     = ratingCounts;
    m_colorCounts      = colorCounts;
    m_tagCounts        = tagCounts;
    m_typeCounts       = typeCounts;
    m_createDateCounts = createDateCounts;
    m_modifyDateCounts = modifyDateCounts;
    rebuildGroups();
}

// ─── rebuildGroups ────────────────────────────────────────────────
void FilterPanel::rebuildGroups() {
    // 清空旧内容（保留末尾 stretch）
    while (m_containerLayout->count() > 1) {
        QLayoutItem* item = m_containerLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    auto colorMap = s_colorMap();

    // ── 1. 评级 ──────────────────────────────────────────────
    if (!m_ratingCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("评级", gl);
        for (int r : {0, 1, 2, 3, 4, 5}) {
            if (!m_ratingCounts.contains(r)) continue;
            QCheckBox* cb = addFilterRow(gl, ratingDisplayName(r), m_ratingCounts[r]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.ratings.contains(r));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, r](bool on) {
                if (on) { if (!m_filter.ratings.contains(r)) m_filter.ratings.append(r); }
                else m_filter.ratings.removeAll(r);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 2. 颜色标记 ──────────────────────────────────────────
    if (!m_colorCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("颜色标记", gl);
        for (const QString& key : QStringList{"", "red", "orange", "yellow", "green", "cyan", "blue", "purple", "gray"}) {
            if (!m_colorCounts.contains(key)) continue;
            QCheckBox* cb = addFilterRow(gl, colorDisplayName(key), m_colorCounts[key], colorMap.value(key));
            cb->blockSignals(true);
            cb->setChecked(m_filter.colors.contains(key));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                if (on) { if (!m_filter.colors.contains(key)) m_filter.colors.append(key); }
                else m_filter.colors.removeAll(key);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 3. 标签 / 关键字 ─────────────────────────────────────
    if (!m_tagCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("标签 / 关键字", gl);
        if (m_tagCounts.contains("__none__")) {
            QCheckBox* cb = addFilterRow(gl, "无标签", m_tagCounts["__none__"]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.tags.contains("__none__"));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this](bool on) {
                if (on) { if (!m_filter.tags.contains("__none__")) m_filter.tags.append("__none__"); }
                else    m_filter.tags.removeAll("__none__");
                emit filterChanged(m_filter);
            });
        }
        QStringList sorted = m_tagCounts.keys();
        sorted.sort(Qt::CaseInsensitive);
        for (const QString& tag : sorted) {
            if (tag == "__none__") continue;
            QCheckBox* cb = addFilterRow(gl, tag, m_tagCounts[tag]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.tags.contains(tag));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, tag](bool on) {
                if (on) { if (!m_filter.tags.contains(tag)) m_filter.tags.append(tag); }
                else m_filter.tags.removeAll(tag);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 4. 文件类型 ──────────────────────────────────────────
    if (!m_typeCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("文件类型", gl);
        if (m_typeCounts.contains("folder")) {
            QCheckBox* cb = addFilterRow(gl, "文件夹", m_typeCounts["folder"]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.types.contains("folder"));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this](bool on) {
                if (on) { if (!m_filter.types.contains("folder")) m_filter.types.append("folder"); }
                else    m_filter.types.removeAll("folder");
                emit filterChanged(m_filter);
            });
        }
        QStringList exts = m_typeCounts.keys(); exts.sort();
        for (const QString& ext : exts) {
            if (ext == "folder") continue;
            QString label = ext.isEmpty() ? "无扩展名" : ext;
            QCheckBox* cb = addFilterRow(gl, label, m_typeCounts[ext]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.types.contains(ext));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, ext](bool on) {
                if (on) { if (!m_filter.types.contains(ext)) m_filter.types.append(ext); }
                else m_filter.types.removeAll(ext);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 5. 创建日期 ──────────────────────────────────────────
    if (!m_createDateCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("创建日期", gl);
        // "今天"/"昨天"置顶
        for (const QString& key : QStringList{"today", "yesterday"}) {
            if (!m_createDateCounts.contains(key)) continue;
            QString label = (key == "today") ? "今天" : "昨天";
            QCheckBox* cb = addFilterRow(gl, label, m_createDateCounts[key]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.createDates.contains(key));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                if (on) { if (!m_filter.createDates.contains(key)) m_filter.createDates.append(key); }
                else m_filter.createDates.removeAll(key);
                emit filterChanged(m_filter);
            });
        }
        QStringList dates = m_createDateCounts.keys(); dates.sort(Qt::CaseInsensitive);
        for (const QString& d : dates) {
            if (d == "today" || d == "yesterday") continue;
            QCheckBox* cb = addFilterRow(gl, d, m_createDateCounts[d]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.createDates.contains(d));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, d](bool on) {
                if (on) { if (!m_filter.createDates.contains(d)) m_filter.createDates.append(d); }
                else m_filter.createDates.removeAll(d);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 6. 修改日期 ──────────────────────────────────────────
    if (!m_modifyDateCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("修改日期", gl);
        for (const QString& key : QStringList{"today", "yesterday"}) {
            if (!m_modifyDateCounts.contains(key)) continue;
            QString label = (key == "today") ? "今天" : "昨天";
            QCheckBox* cb = addFilterRow(gl, label, m_modifyDateCounts[key]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.modifyDates.contains(key));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                if (on) { if (!m_filter.modifyDates.contains(key)) m_filter.modifyDates.append(key); }
                else m_filter.modifyDates.removeAll(key);
                emit filterChanged(m_filter);
            });
        }
        QStringList dates = m_modifyDateCounts.keys(); dates.sort(Qt::CaseInsensitive);
        for (const QString& d : dates) {
            if (d == "today" || d == "yesterday") continue;
            QCheckBox* cb = addFilterRow(gl, d, m_modifyDateCounts[d]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.modifyDates.contains(d));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, d](bool on) {
                if (on) { if (!m_filter.modifyDates.contains(d)) m_filter.modifyDates.append(d); }
                else m_filter.modifyDates.removeAll(d);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }
}

// ─── buildGroup ───────────────────────────────────────────────────
QWidget* FilterPanel::buildGroup(const QString& title, QVBoxLayout*& outContentLayout) {
    QWidget* wrapper = new QWidget(m_container);
    wrapper->setStyleSheet("QWidget { background: transparent; }");
    QVBoxLayout* wl = new QVBoxLayout(wrapper);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);

    // 2026-05-07 按照用户要求：改用 QPushButton 替代 QToolButton，因为 QPushButton 对 text-align 支持更好
    QPushButton* hdr = new QPushButton(title, wrapper);
    hdr->setCheckable(true);
    hdr->setChecked(true);
    hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hdr->setFixedHeight(24);
    hdr->setStyleSheet(
        "QPushButton { "
        "  background: #252526; "
        "  border: none; "
        "  border-top: 1px solid #333333;"
        "  color: #AAAAAA; "
        "  font-size: 11px; "
        "  font-weight: 600; "
        "  text-align: left; "
        "  padding-left: 8px; "
        "  padding-right: 0px; "
        "  padding-top: 0px; "
        "  padding-bottom: 0px; "
        "  margin: 0px; "
        "} "
        "QPushButton:hover { color: #EEEEEE; } "
        "QPushButton:pressed { background: #252526; }"); 

    QWidget* content = new QWidget(wrapper);
    content->setStyleSheet("QWidget { background: transparent; }");
    outContentLayout = new QVBoxLayout(content);
    outContentLayout->setContentsMargins(0, 0, 0, 0);
    outContentLayout->setSpacing(0);

    connect(hdr, &QPushButton::toggled, content, &QWidget::setVisible);

    wl->addWidget(hdr);
    wl->addWidget(content);
    return wrapper;
}

// ─── addFilterRow ─────────────────────────────────────────────────
QCheckBox* FilterPanel::addFilterRow(QVBoxLayout* layout, const QString& label, int count, const QColor& dotColor) {
    QCheckBox* cb = new QCheckBox();
    // 2026-03-xx 按照用户要求，仅保留蓝色勾选标记 (#378ADD)，背景保持深色
    cb->setStyleSheet(
        "QCheckBox { spacing: 0px; }"
        "QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #444;"
        "                       border-radius: 2px; background: #1E1E1E; }"
        "QCheckBox::indicator:hover { border: 1px solid #666; }"
        "QCheckBox::indicator:checked { "
        "   border: 1px solid #378ADD; "
        "   background: #1E1E1E; "
        "   image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjMzc4QUREIiBzdHJva2Utd2lkdGg9IjMuNSIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=);"
        "}"
    );

    // 整行可点击容器
    // 增加高度至 24px 以适配各种系统缩放，避免文字截断
    ClickableRow* row = new ClickableRow(cb);
    row->setFixedHeight(24);

    QHBoxLayout* rl = new QHBoxLayout(row);
    rl->setContentsMargins(4, 0, 4, 0);
    rl->setSpacing(5);
    rl->addWidget(cb);

    if (dotColor.isValid() && dotColor != Qt::transparent) {
        QLabel* dot = new QLabel(row);
        dot->setFixedSize(10, 10);
        dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(dotColor.name()));
        rl->addWidget(dot);
    }

    QLabel* lbl = new QLabel(label, row);
    lbl->setStyleSheet("font-size: 12px; color: #CCCCCC; background: transparent;");
    rl->addWidget(lbl, 1);

    QLabel* cnt = new QLabel(QString::number(count), row);
    cnt->setStyleSheet("font-size: 11px; color: #555555; background: transparent;");
    cnt->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rl->addWidget(cnt);

    layout->addWidget(row);
    return cb;
}

// ─── clearAllFilters ──────────────────────────────────────────────
void FilterPanel::clearAllFilters() {
    m_filter = FilterState{};
    const auto cbs = m_container->findChildren<QCheckBox*>();
    for (QCheckBox* cb : cbs) {
        cb->blockSignals(true);
        cb->setChecked(false);
        cb->blockSignals(false);
    }
    emit filterChanged(m_filter);
    emit resetSearchRequested();
}

} // namespace ArcMeta
