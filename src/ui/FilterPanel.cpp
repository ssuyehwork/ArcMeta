#include "FilterPanel.h"
#include "ToolTipOverlay.h"
#include "UiHelper.h"
#include "ColorPicker.h"
#include <QPushButton>
#include <QMouseEvent>
#include <QCursor>

namespace ArcMeta {

// ─── 颜色映射表 ────────────────────────────────────────────────────
QMap<QString, QColor> FilterPanel::s_colorMap() {
    return {
        { "",        QColor("#888780") },
        { "#E04040", QColor("#E24B4A") },
        { "#E09020", QColor("#EF9F27") },
        { "#F0C070", QColor("#FAC775") },
        { "#609020", QColor("#639922") },
        { "#109070", QColor("#1D9E75") },
        { "#3080D0", QColor("#378ADD") },
        { "#7070D0", QColor("#7F77DD") },
        { "#505050", QColor("#5F5E5A") },
        // 兼容旧语义名数据
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
        { "",        "无色标" }, 
        { "#E04040", "红色" },
        { "#E09020", "橙色" },
        { "#F0C070", "黄色" },
        { "#609020", "绿色" },
        { "#109070", "青色" },
        { "#3080D0", "蓝色" },
        { "#7070D0", "紫色" },
        { "#505050", "灰色" },
        { "#000000", "黑色" },
        { "#F0F0F0", "白色" },
        // 兼容旧语义名
        { "red",    "红色" }, { "orange", "橙色" }, { "yellow", "黄色" },
        { "green",  "绿色" }, { "cyan",   "青色" }, { "blue",   "蓝色" },
        { "purple", "紫色" }, { "gray",   "灰色" },
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
        "  border-bottom: none;" // 2026-05-17 按照用户要求：覆盖全局 QSS 的 border-bottom，避免与首个 GroupHdrRow 的 border-top 叠加形成 2px 视觉分割线
        "}"
    );
    QHBoxLayout* topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(15, 0, 5, 0); // 2026-05-17 按照用户要求：右侧边距统一设为 5px，上下 0px 垂直居中
    topL->setSpacing(5);                  // 2026-05-17 按照用户要求：间距统一为 5px

    QLabel* iconLabel = new QLabel(topBar);
    iconLabel->setPixmap(UiHelper::getIcon("filter", QColor("#f1c40f"), 18).pixmap(18, 18));
    topL->addWidget(iconLabel);

    QLabel* title = new QLabel("筛选", topBar);
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #f1c40f; background: transparent; border: none;");

    m_btnClearAll = new QPushButton(topBar);
    m_btnClearAll->setFixedSize(24, 24); // 2026-05-17 按照用户要求：统一为 24x24 规格以实现像素级对齐
    m_btnClearAll->setIcon(UiHelper::getIcon("trash", QColor("#B0B0B0"))); // 将文字重构为具有高度语义化的 trash SVG 图标
    m_btnClearAll->setIconSize(QSize(16, 16));
    m_btnClearAll->setFlat(true);
    m_btnClearAll->setCursor(Qt::PointingHandCursor);
    m_btnClearAll->setProperty("tooltipText", "重置所有筛选条件");
    m_btnClearAll->installEventFilter(this);
    m_btnClearAll->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }");
    connect(m_btnClearAll, &QPushButton::clicked, this, &FilterPanel::clearAllFilters);

    topL->addWidget(title);
    topL->addStretch();
    topL->addWidget(m_btnClearAll, 0, Qt::AlignVCenter);
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
    m_containerLayout->setContentsMargins(0, 0, 0, 10); // 2026-05-17 按照用户要求：右侧由 10 改为 0，消除内容区右侧留白
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
    
    // 2026-05-17 根因修复：已废除 Resize 监听逻辑（原用于绝对定位，现已改为内嵌布局）
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
    if (!m_colorCounts.isEmpty() || !m_filter.colors.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QHBoxLayout* hdrLayout = nullptr; // 标题行内嵌布局
        QWidget* g = buildGroup("颜色标记", gl, &hdrLayout);
        
        if (hdrLayout) {
            QPushButton* btnCustomColor = new QPushButton(hdrLayout->parentWidget());
            btnCustomColor->setObjectName("BtnCustomColor");
            btnCustomColor->setFixedSize(20, 20);
            btnCustomColor->setCursor(Qt::PointingHandCursor);
            // 2026-05-17 按照用户要求：rebuildGroups 每次重建按钮，图标颜色必须从
            // m_filter.colors 中反查最后一个自定义色（#开头）来初始化，
            // 而不能固定为蓝色，否则 rebuildGroups 后颜色会被还原。
            QColor btnIconColor("#3498db"); // 默认蓝，未选过自定义颜色时的占位色
            for (const QString& key : m_filter.colors)
                if (key.startsWith('#')) btnIconColor = QColor(key);
            btnCustomColor->setIcon(UiHelper::getIcon("paint_bucket", btnIconColor, 14));
            btnCustomColor->setStyleSheet(
                "QPushButton { background: transparent; border: none; }"
                "QPushButton:hover { background: rgba(255,255,255,0.1); border-radius: 4px; }"
            );
            hdrLayout->addWidget(btnCustomColor); 
            
            connect(btnCustomColor, &QPushButton::clicked, this, [this, btnCustomColor]() {
                ColorPicker* picker = new ColorPicker(this);
                connect(picker, &ColorPicker::colorSelected, this, [this, btnCustomColor](const QColor& c, int tolerance) {
                    QString hex = c.name().toUpper();
                    // 2026-05-17 按照用户要求：存入容差值并触发筛选更新
                    m_filter.colorTolerance = tolerance;
                    // 2026-05-17 按照用户要求：图标颜色随所选颜色实时同步，不再固定为蓝色
                    btnCustomColor->setIcon(UiHelper::getIcon("paint_bucket", c, 14));
                    if (!m_filter.colors.contains(hex)) {
                        m_filter.colors.append(hex);
                        emit filterChanged(m_filter);
                        rebuildGroups();
                    }
                });
                picker->adjustSize();
                QPoint pos = btnCustomColor->mapToGlobal(QPoint(0, 0));
                pos.setY(pos.y() - picker->height() - 5);
                picker->move(pos);
                picker->show();
            });
        }
        
        // 追加已被筛选但不在基础列表中的自定义颜色 (相近色)
        for (const QString& key : m_filter.colors) {
            if (key.startsWith("#")) {
                QColor dotC = QColor(key);
                QCheckBox* cb = addFilterRow(gl, "相近色: " + key, 0, dotC);
                cb->blockSignals(true);
                cb->setChecked(true);
                cb->blockSignals(false);
                connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                    if (!on) {
                        m_filter.colors.removeAll(key);
                        emit filterChanged(m_filter);
                        rebuildGroups();
                    }
                });
            }
        }

        // 2026-06-xx 按照要求：动态展示所有存在的颜色
        QSet<QString> processedKeys;
        
        // 1. 优先按品牌色顺序排列常用颜色
        QStringList priorityKeys = {"", "#E04040", "#E09020", "#F0C070", "#609020", "#109070", "#3080D0", "#7070D0", "#505050", "#000000", "#F0F0F0",
                                    "red", "orange", "yellow", "green", "cyan", "blue", "purple", "gray"};
        for (const QString& key : priorityKeys) {
            if (m_colorCounts.contains(key)) {
                QCheckBox* cb = addFilterRow(gl, colorDisplayName(key), m_colorCounts[key], colorMap.value(key, QColor(key)));
                cb->blockSignals(true);
                cb->setChecked(m_filter.colors.contains(key));
                cb->blockSignals(false);
                connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                    if (on) { if (!m_filter.colors.contains(key)) m_filter.colors.append(key); }
                    else m_filter.colors.removeAll(key);
                    emit filterChanged(m_filter);
                });
                processedKeys.insert(key);
            }
        }

        // 2. 展示其他所有动态提取到的量化颜色
        QStringList allKeys = m_colorCounts.keys();
        allKeys.sort();
        for (const QString& key : allKeys) {
            if (processedKeys.contains(key)) continue;
            
            QCheckBox* cb = addFilterRow(gl, colorDisplayName(key), m_colorCounts[key], QColor(key));
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
// 2026-05-17 终极根因修复：引入独立 hdrRow(QWidget) 作为标题行容器
// hdr(QPushButton) 恢复原始有文字写法，绝对不携带任何子控件和内嵌布局
// btnCustomColor 等右侧按钮通过 outHdrLayout 追加到 hdrRow 的 QHBoxLayout 里
// 这是 Qt 最正规的写法，彻底消除 QPushButton 内嵌布局引发的渲染冲突和留白
QWidget* FilterPanel::buildGroup(const QString& title, QVBoxLayout*& outContentLayout,
                                  QHBoxLayout** outHdrLayout) {
    QWidget* wrapper = new QWidget(m_container);
    // 2026-05-17 根因修复：不使用 "QWidget { background: transparent; }" 类选择器
    // 该选择器会级联到所有子孙 QWidget，与 MainWindow 全局 QSS 相互叠加造成混乱
    // 改为仅对 wrapper 自身设置透明背景（借助 WA_StyledBackground 隔离传播）
    wrapper->setAttribute(Qt::WA_StyledBackground, true);
    wrapper->setStyleSheet("background: transparent;"); // 无选择器，仅作用于 wrapper 自身
    QVBoxLayout* wl = new QVBoxLayout(wrapper);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);

    // hdrRow：整个标题行的容器，负责背景色和上边框
    // 2026-05-17 终极根因修复：必须使用 ID 选择器 QWidget#GroupHdrRow
    // 原因：MainWindow 全局 QSS 有 #FilterContainer { background-color: #1E1E1E } 的规则，
    //       会级联到所有 QWidget 子孙，将 hdrRow 的背景强制变为 #1E1E1E（深黑色），
    //       造成标题行与内容区视觉上无法区分，形成"留白"假象。
    //       ID 选择器的 QSS 特异性高于祖先容器的规则，可以正确覆盖。
    QWidget* hdrRow = new QWidget(wrapper);
    hdrRow->setObjectName("GroupHdrRow");   // ← 关键：打上 ID 以提升 QSS 优先级
    hdrRow->setFixedHeight(24);
    hdrRow->setAttribute(Qt::WA_StyledBackground, true);
    hdrRow->setStyleSheet(
        "QWidget#GroupHdrRow {"            // ID 选择器，特异性高于全局级联
        "  background: #252526;"
        "  border-top: 1px solid #333333;"
        "}");

    QHBoxLayout* hdrRowLayout = new QHBoxLayout(hdrRow);
    hdrRowLayout->setContentsMargins(0, 0, 0, 0);
    hdrRowLayout->setSpacing(0);

    // 2026-05-07 按照用户要求：QPushButton（有文字，text-align left），纯净无子控件
    // 2026-05-17 终极修复：parent 改为 hdrRow，背景 transparent（由 hdrRow 统一提供）
    QPushButton* hdr = new QPushButton(title, hdrRow);
    hdr->setCheckable(true);
    hdr->setChecked(true);
    hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hdr->setFixedHeight(24);
    hdr->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"   // hdrRow 已提供背景，此处透明即可
        "  border: none;"
        "  color: #AAAAAA;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  text-align: left;"
        "  padding-left: 8px;"
        "  padding-right: 4px;"
        "  padding-top: 0px;"
        "  padding-bottom: 0px;"
        "  margin: 0px;"
        "}"
        "QPushButton:hover { color: #EEEEEE; }"
        "QPushButton:pressed { background: transparent; }");
    hdrRowLayout->addWidget(hdr);   // hdr 占满剩余宽度

    if (outHdrLayout) *outHdrLayout = hdrRowLayout; // 暴露 hdrRow 布局，供追加右侧按钮

    QWidget* content = new QWidget(wrapper);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet("background: transparent;"); // 无选择器，仅作用于 content 自身
    outContentLayout = new QVBoxLayout(content);
    outContentLayout->setContentsMargins(0, 0, 0, 0);
    outContentLayout->setSpacing(0);

    connect(hdr, &QPushButton::toggled, content, &QWidget::setVisible);

    wl->addWidget(hdrRow);     // 加入 hdrRow，不再直接加 hdr
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
