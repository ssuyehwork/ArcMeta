#include "MetaPanel.h"
#include "SvgIcons.h"
#include "ToolTipOverlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QScrollArea>
#include <QFileInfo>
#include <QLabel>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QDir>
#include <QAbstractTextDocumentLayout>
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../meta/MetadataManager.h"

namespace ArcMeta {

ElasticEdit::ElasticEdit(QWidget* parent) : QPlainTextEdit(parent) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    document()->setDocumentMargin(0);
    connect(this, &QPlainTextEdit::textChanged, this, &ElasticEdit::adjustHeight);
}

void ElasticEdit::adjustHeight() {
    // 2026-06-xx 工业级修正：高度计算必须考虑样式表中的 padding 和 border 物理厚度
    // 否则在带有背景色和边框的样式下，文字会因为高度不足而发生垂直位移或截断
    document()->documentLayout()->update();
    int contentH = (int)document()->size().height();
    
    // 物理冗余：padding-top(4) + padding-bottom(4) + border(2) + 呼吸冗余(2) = 12
    int newH = qMax(28, contentH + 16); 
    if (this->height() != newH) {
        setFixedHeight(newH);
    }
}

void ElasticEdit::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    adjustHeight();
}

void ElasticEdit::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier)) {
        clearFocus();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
}

PaletteCapsule::PaletteCapsule(QWidget* parent) : QWidget(parent) {
    setFixedHeight(28); // 增加 2px 物理高度以适配描边
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

void PaletteCapsule::setPalette(const QVector<QPair<QColor, float>>& palette) {
    m_palette = palette;
    // 2026-06-xx 工业级重构：废弃 setFixedSize，改用 sizeHint 和自适应绘制
    // 允许布局系统根据父容器宽度进行压缩
    updateGeometry();
    update();
}

QSize PaletteCapsule::sizeHint() const {
    if (m_palette.isEmpty()) return QSize(0, 28);
    int w = m_padding * 2 + (int)m_palette.size() * m_dotSize + ((int)m_palette.size() - 1) * m_spacing;
    return QSize(w, 28);
}

QSize PaletteCapsule::minimumSizeHint() const {
    // 最小宽度：至少显示一个色点或 28px 高度的圆形
    return QSize(28, 28);
}

void PaletteCapsule::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 2026-06-xx 响应式绘制：根据可用宽度动态调整间距
    int availableW = width();
    int totalDots = (int)m_palette.size();
    float currentSpacing = m_spacing;

    // 如果宽度不足，尝试压缩间距 (最小间距 2px)
    if (totalDots > 1) {
        int contentW = m_padding * 2 + totalDots * m_dotSize + (totalDots - 1) * m_spacing;
        if (contentW > availableW) {
            currentSpacing = (float)(availableW - m_padding * 2 - totalDots * m_dotSize) / (totalDots - 1);
            if (currentSpacing < 2.0f) currentSpacing = 2.0f;
        }
    }

    // 1. 绘制总背景 (Capsule) - 提升亮度并增加边框
    painter.setPen(QPen(QColor("#4D4D4D"), 1)); 
    painter.setBrush(QColor("#2E2E2E")); 
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 14, 14);

    // 2. 绘制色点
    for (int i = 0; i < totalDots; ++i) {
        int x = m_padding + i * (m_dotSize + currentSpacing);

        // 边界保护：不绘制超出范围的色点
        if (x + m_dotSize > availableW - m_padding + 2) break;

        QRect dotRect(x, (height() - m_dotSize) / 2, m_dotSize, m_dotSize);

        painter.setPen(Qt::NoPen);
        painter.setBrush(m_palette[i].first);
        painter.drawEllipse(dotRect.adjusted(1, 1, -1, -1));

        // 悬停反馈：1px 白色边缘
        if (i == m_hoverIndex) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 1.0));
            painter.drawEllipse(dotRect.adjusted(0, 0, -1, -1));
        }
    }
}

void PaletteCapsule::mouseMoveEvent(QMouseEvent* event) {
    int newHover = -1;
    for (int i = 0; i < m_palette.size(); ++i) {
        int x = m_padding + i * (m_dotSize + m_spacing);
        QRect dotRect(x, 0, m_dotSize, height());
        if (dotRect.contains(event->pos())) {
            newHover = i;
            break;
        }
    }

    if (newHover != m_hoverIndex) {
        m_hoverIndex = newHover;
        if (m_hoverIndex != -1) {
            QString hex = m_palette[m_hoverIndex].first.name().toUpper();
            int ratio = qRound(m_palette[m_hoverIndex].second * 100);
            QString text = QString("%1 (%2%)").arg(hex).arg(ratio);
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
        } else {
            ToolTipOverlay::hideTip();
        }
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void PaletteCapsule::leaveEvent(QEvent* event) {
    m_hoverIndex = -1;
    update();
    QWidget::leaveEvent(event);
}

void PaletteCapsule::mousePressEvent(QMouseEvent* event) {
    if (m_hoverIndex != -1) {
        QMenu menu(this);
        UiHelper::applyMenuStyle(&menu);
        QColor color = m_palette[m_hoverIndex].first;

        menu.addAction("搜索相似颜色的项目", [this, color]() {
            // 2026-06-xx 解耦重构：移除 findChild，通过信号通知
            emit colorSelected(color);
        });
        menu.addSeparator();
        QString hex = color.name().toUpper();
        menu.addAction(QString("复制 %1").arg(hex), [hex]() { QApplication::clipboard()->setText(hex); });
        
        menu.exec(event->globalPosition().toPoint());
    }
    QWidget::mousePressEvent(event);
}

// --- TagPill ---
TagPill::TagPill(const QString& text, QWidget* parent) 
    : QWidget(parent), m_text(text) {
    setProperty("tagText", text);
    setFixedHeight(22);
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 4, 0);
    layout->setSpacing(4);
    QLabel* lbl = new QLabel(text, this);
    lbl->setStyleSheet("color: #EEEEEE; font-size: 13px; border: none; background: transparent;");
    m_closeBtn = new QPushButton(this);
    m_closeBtn->setFixedSize(14, 14);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setIcon(UiHelper::getIcon("close", QColor("#B0B0B0"), 12));
    m_closeBtn->setIconSize(QSize(10, 10));
    m_closeBtn->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: rgba(255, 255, 255, 0.1); border-radius: 2px; }");
    layout->addWidget(lbl);
    layout->addWidget(m_closeBtn);
    connect(m_closeBtn, &QPushButton::clicked, [this]() { emit deleteRequested(m_text); });
    QFontMetrics fm(lbl->font());
    setFixedWidth(fm.horizontalAdvance(text) + 30); 
}

void TagPill::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#2B2B2B"));
    painter.setPen(QPen(QColor("#444444"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 11, 11);
}

// --- FlowLayout ---
FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}
FlowLayout::~FlowLayout() {
    QLayoutItem *item;
    while ((item = takeAt(0))) delete item;
}
void FlowLayout::addItem(QLayoutItem *item) { itemList.append(item); }
int FlowLayout::horizontalSpacing() const { return m_hSpace >= 0 ? m_hSpace : 4; }
int FlowLayout::verticalSpacing() const { return m_vSpace >= 0 ? m_vSpace : 4; }
int FlowLayout::count() const { return itemList.size(); }
QLayoutItem *FlowLayout::itemAt(int index) const { return itemList.value(index); }
QLayoutItem *FlowLayout::takeAt(int index) { return (index >= 0 && index < itemList.size()) ? itemList.takeAt(index) : nullptr; }
Qt::Orientations FlowLayout::expandingDirections() const { return Qt::Orientations(); }
bool FlowLayout::hasHeightForWidth() const { return true; }
int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }
void FlowLayout::setGeometry(const QRect &rect) { QLayout::setGeometry(rect); doLayout(rect, false); }
QSize FlowLayout::sizeHint() const { return minimumSize(); }
QSize FlowLayout::minimumSize() const {
    QSize size;
    for (QLayoutItem *item : itemList) size = size.expandedTo(item->minimumSize());
    size += QSize(2 * contentsMargins().top(), 2 * contentsMargins().top());
    return size;
}
int FlowLayout::doLayout(const QRect &rect, bool testOnly) const {
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;
    for (QLayoutItem *item : itemList) {
        int spaceX = horizontalSpacing();
        int spaceY = verticalSpacing();
        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }
        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

// --- StarRatingWidget ---
StarRatingWidget::StarRatingWidget(QWidget* parent) : QWidget(parent) { setFixedSize(5 * 18 + 4 * 1, 20); setCursor(Qt::PointingHandCursor); }
void StarRatingWidget::setRating(int rating) { m_rating = rating; update(); }
void StarRatingWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing); 
    int starSize = 18; int spacing = 1;
    QPixmap filledStar = UiHelper::getPixmap("star-svgrepo-com.svg", QSize(starSize, starSize), QColor("#EF9F27"));
    QPixmap emptyStar = UiHelper::getPixmap("star-rate-rating-outline-svgrepo-com.svg", QSize(starSize, starSize), QColor("#444444"));
    for (int i = 0; i < 5; ++i) { QRect r(i * (starSize + spacing), (height() - starSize) / 2, starSize, starSize); painter.drawPixmap(r, (i < m_rating) ? filledStar : emptyStar); }
}
void StarRatingWidget::mousePressEvent(QMouseEvent* e) {
    e->accept();
    int x = e->pos().x();
    int starSize = 18;
    int spacing = 1;
    int index = x / (starSize + spacing);
    if (index >= 0 && index < 5) {
        int newRating = index + 1;
        if (newRating == m_rating) newRating = 0;
        setRating(newRating);
        emit ratingChanged(newRating);
    }
}

// --- ColorPickerWidget ---
ColorPickerWidget::ColorPickerWidget(QWidget* parent) : QWidget(parent) {
    m_colors = {{L"", QColor("#888780")}, {L"red", QColor("#E24B4A")}, {L"orange", QColor("#EF9F27")}, {L"yellow", QColor("#FECF0E")}, {L"green", QColor("#639922")}, {L"cyan", QColor("#1D9E75")}, {L"blue", QColor("#378ADD")}, {L"purple", QColor("#7F77DD")}, {L"gray", QColor("#5F5E5A")}};
    setFixedSize((int)m_colors.size() * 24, 24); setCursor(Qt::PointingHandCursor);
}
void ColorPickerWidget::setColor(const std::wstring& name) {
    m_currentColor = name;
    if (m_colors.size() > 9) m_colors.resize(9);
    if (!name.empty() && name[0] == L'#') {
        QColor customColor(QString::fromStdWString(name));
        if (customColor.isValid()) m_colors.push_back({name, customColor});
    }
    setFixedSize((int)m_colors.size() * 24, 24);
    update();
}
void ColorPickerWidget::paintEvent(QPaintEvent*) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < (int)m_colors.size(); ++i) {
        QRect r(i * 24 + 3, 3, 18, 18);
        if (m_colors[i].name == m_currentColor) { p.setPen(QPen(QColor("#FFFFFF"), 1.5)); p.drawEllipse(r.adjusted(-2, -2, 2, 2)); }
        p.setPen(Qt::NoPen); p.setBrush(m_colors[i].value); p.drawEllipse(r);
    }
}
void ColorPickerWidget::mousePressEvent(QMouseEvent* e) {
    e->accept();
    int x = e->pos().x();
    int index = x / 24;
    if (index >= 0 && index < (int)m_colors.size()) {
        setColor(m_colors[index].name);
        emit colorChanged(m_colors[index].name);
    }
}

// --- MetaPanel ---
MetaPanel::MetaPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("MetadataContainer"); setAttribute(Qt::WA_StyledBackground, true); setMinimumWidth(230); 
    setStyleSheet("color: #EEEEEE;");
    m_mainLayout = new QVBoxLayout(this); m_mainLayout->setContentsMargins(0, 0, 0, 0); m_mainLayout->setSpacing(0);
    initUi();
}

void MetaPanel::initUi() {
    QWidget* header = new QWidget(this); header->setObjectName("ContainerHeader"); header->setFixedHeight(32);
    header->setStyleSheet("QWidget#ContainerHeader { background-color: #252526; border-bottom: 1px solid #333; }");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 0, 5, 0);
    headerLayout->setSpacing(5);
    QLabel* iconLabel = new QLabel(header); iconLabel->setPixmap(UiHelper::getIcon("all_data", QColor("#4a90e2"), 18).pixmap(18, 18)); headerLayout->addWidget(iconLabel);
    QLabel* titleLabel = new QLabel("元数据", header); titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; background: transparent; border: none;"); headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    QPushButton* closeBtn = new QPushButton(header); closeBtn->setIcon(UiHelper::getIcon("close", QColor("#FFFFFF"), 14)); closeBtn->setFixedSize(24, 24); closeBtn->setCursor(Qt::PointingHandCursor);
    closeBtn->setStyleSheet("QPushButton { background-color: #E81123; border: none; border-radius: 4px; } QPushButton:hover { background-color: #F1707A; } QPushButton:pressed { background-color: #A50000; }");
    connect(closeBtn, &QPushButton::clicked, [this]() { this->hide(); });
    headerLayout->addWidget(closeBtn, 0, Qt::AlignVCenter);
    m_mainLayout->addWidget(header);

    m_scrollArea = new QScrollArea(this); m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true); m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_container = new QWidget(m_scrollArea);
    m_containerLayout = new QVBoxLayout(m_container);
    // 2026-06-xx 工业级强制约束：严格保持左右 10px 边距，绝不溢出
    m_containerLayout->setContentsMargins(10, 10, 10, 10);
    // 2026-06-01 修正：降低全局间距，消除视觉断层 (原 12px -> 现 8px)
    m_containerLayout->setSpacing(8);
    
    // [Section 1] 调色盘胶囊 (Palette Capsules)
    m_paletteCapsule = new PaletteCapsule(m_container);
    connect(m_paletteCapsule, &PaletteCapsule::colorSelected, this, &MetaPanel::searchByColor);
    
    // 包装器，确保胶囊左对齐且不拉伸
    QWidget* palWrapper = new QWidget(m_container);
    // 限制包装器最大宽度，预留左右各 10px 边距
    palWrapper->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    QHBoxLayout* palWrapperL = new QHBoxLayout(palWrapper);
    palWrapperL->setContentsMargins(0, 0, 0, 0);
    palWrapperL->addWidget(m_paletteCapsule);
    palWrapperL->addStretch();
    m_containerLayout->addWidget(palWrapper);

    // [Section 2] 名称输入框 (ElasticEdit)
    m_nameEdit = new ElasticEdit(m_container);
    m_nameEdit->setPlaceholderText("文件名称...");
    m_nameEdit->setStyleSheet(QString("QPlainTextEdit { background: %1; border: 1px solid %2; border-radius: 4px; padding: 6px 10px; font-size: 13px; font-weight: bold; color: %3; }")
        .arg(Style::qssColor(Style::BackgroundDeep))
        .arg(Style::qssColor(Style::BorderColor))
        .arg(Style::qssColor(Style::TextMain)));
    m_nameEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_nameEdit);

    // [Section 3] 备注输入框 (ElasticEdit)
    m_noteEdit = new ElasticEdit(m_container);
    m_noteEdit->setPlaceholderText("添加备注说明...");
    m_noteEdit->setStyleSheet("QPlainTextEdit { background: #1e1e1e; border: 1px solid #3c3c3c; border-radius: 4px; padding: 6px 10px; font-size: 13px; color: #AAAAAA; }");
    m_noteEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_noteEdit);

    // [Section 4] 链接输入框 (ElasticEdit)
    m_linkEdit = new ElasticEdit(m_container);
    m_linkEdit->setPlaceholderText("添加链接...");
    m_linkEdit->setStyleSheet("QPlainTextEdit { background: #1e1e1e; border: 1px solid #3c3c3c; border-radius: 4px; padding: 6px 10px; font-size: 13px; color: #4a90e2; }");
    m_linkEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_linkEdit);

    // [Section 5] 标签区域 (Tag Flow)
    QWidget* tagBox = new QWidget(m_container);
    QVBoxLayout* tagL = new QVBoxLayout(tagBox);
    tagL->setContentsMargins(0, 0, 0, 0);
    tagL->setSpacing(8);
    
    m_tagContainer = new QWidget(tagBox);
    m_tagFlowLayout = new FlowLayout(m_tagContainer, 0, 4, 4);
    tagL->addWidget(m_tagContainer);

    m_tagEdit = new QLineEdit(tagBox);
    m_tagEdit->setPlaceholderText("输入标签...");
    m_tagEdit->setFixedHeight(24);
    m_tagEdit->setStyleSheet("QLineEdit { background: #252526; border: 1px solid #333333; border-radius: 3px; padding-left: 6px; font-size: 13px; color: #AAAAAA; }");
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &MetaPanel::onTagAdded);
    tagL->addWidget(m_tagEdit);
    m_containerLayout->addWidget(tagBox);

    // [Section 6] 分类展示 (Category Pills)
    m_categoryEdit = new ElasticEdit(m_container);
    m_categoryEdit->setReadOnly(true);
    m_categoryEdit->setStyleSheet("QPlainTextEdit { background: #252526; border: 1px solid #2A2A2A; border-radius: 4px; padding: 6px 8px; font-size: 13px; color: #EEEEEE; }");
    m_containerLayout->addWidget(m_categoryEdit);

    m_containerLayout->addWidget(createSeparator());

    // [Section 7] 详情网格 (基本信息)
    addInfoRow("类型", lblType); addInfoRow("大小", lblSize);
    addInfoRow("创建时间", lblCtime); addInfoRow("修改时间", lblMtime); addInfoRow("访问时间", lblAtime);
    addInfoRow("物理路径", lblPath); addInfoRow("加密状态", lblEncrypted);

    m_containerLayout->addStretch(1);
    m_scrollArea->setWidget(m_container);
    m_mainLayout->addWidget(m_scrollArea);
}

void MetaPanel::addInfoRow(const QString& label, QLabel*& valueLabel) {
    QWidget* row = new QWidget(m_container); 
    QHBoxLayout* rl = new QHBoxLayout(row); 
    // 2026-06-01 视觉密度优化：压缩行间距 (原 4px -> 现 2px)
    rl->setContentsMargins(0, 2, 0, 2);
    rl->setSpacing(8);
    
    QLabel* kl = new QLabel(label, row); 
    kl->setFixedWidth(80); // 适度增加宽度以支持长标签
    kl->setStyleSheet("font-size: 13px; color: #888888;"); 
    rl->addWidget(kl, 0, Qt::AlignTop);

    valueLabel = new QLabel("-", row); 
    valueLabel->setWordWrap(true); 
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); // 允许复制路径等物理信息
    valueLabel->setStyleSheet("font-size: 13px; color: #CCCCCC; line-height: 1.5;");
    valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop); 
    rl->addWidget(valueLabel, 1); 
    
    m_containerLayout->addWidget(row);
}

QFrame* MetaPanel::createSeparator() { QFrame* l = new QFrame(this); l->setFrameShape(QFrame::HLine); l->setFixedHeight(1); l->setStyleSheet("background-color: #333333; border: none; margin: 4px 0;"); return l; }

QWidget* MetaPanel::createSectionBox(const QString& iconName, const QString& title, QWidget* content) {
    QFrame* box = new QFrame(this); box->setStyleSheet("QFrame { background-color: transparent; border: none; }");
    QVBoxLayout* layout = new QVBoxLayout(box); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
    QHBoxLayout* header = new QHBoxLayout(); header->setSpacing(8);
    QLabel* iconLbl = new QLabel(box); iconLbl->setPixmap(UiHelper::getIcon(iconName, QColor("#888888"), 16).pixmap(16, 16)); header->addWidget(iconLbl);
    QLabel* titleLbl = new QLabel(title, box); titleLbl->setStyleSheet("font-size: 13px; font-weight: bold; color: #888888; text-transform: uppercase;"); header->addWidget(titleLbl);
    header->addStretch(); layout->addLayout(header); layout->addWidget(content); return box;
}

void MetaPanel::onTagAdded() {
    QString text = m_tagEdit->text().trimmed();
    if (!text.isEmpty()) {
        QString currentPath = lblPath->text();
        if (currentPath != "-" && !currentPath.isEmpty()) {
            std::wstring wPath = currentPath.toStdWString();
            RuntimeMeta rm = MetadataManager::instance().getMeta(wPath);
            if (!rm.tags.contains(text)) {
                rm.tags << text; MetadataManager::instance().setTags(wPath, rm.tags);
                TagPill* pill = new TagPill(text, m_tagContainer); connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted); m_tagFlowLayout->addWidget(pill);
            }
        }
        m_tagEdit->clear();
    }
}

void MetaPanel::onTagDeleted(const QString& text) {
    for (int i = 0; i < m_tagFlowLayout->count(); ++i) {
        QLayoutItem* item = m_tagFlowLayout->itemAt(i); TagPill* pill = qobject_cast<TagPill*>(item->widget());
        if (pill && pill->property("tagText").toString() == text) {
            m_tagFlowLayout->takeAt(i); pill->deleteLater(); delete item;
            QString currentPath = lblPath->text();
            if (currentPath != "-" && !currentPath.isEmpty()) {
                std::wstring wPath = currentPath.toStdWString(); RuntimeMeta rm = MetadataManager::instance().getMeta(wPath); rm.tags.removeAll(text); MetadataManager::instance().setTags(wPath, rm.tags);
            }
            return;
        }
    }
}

void MetaPanel::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);

    // 2026-06-xx 工业级强制约束：锁死容器宽度等于视口宽度，彻底消除横向溢出
    int viewportW = m_scrollArea->viewport()->width();
    if (m_container && viewportW > 0) {
        m_container->setFixedWidth(viewportW);
    }

    int maxW = viewportW - 20; // 预留左右各 10px 边距
    if (maxW > 0) {
        m_nameEdit->setMaximumWidth(maxW);
        m_noteEdit->setMaximumWidth(maxW);
        m_linkEdit->setMaximumWidth(maxW);
        m_categoryEdit->setMaximumWidth(maxW);
        if (lblPath) lblPath->setMaximumWidth(maxW - 80);
    }
}

void MetaPanel::updateInfo(const QString& n, const QString& t, const QString& s, const QString& ct, const QString& mt, const QString& at, const QString& p, bool e) {
    m_nameEdit->blockSignals(true);
    QFileInfo info(n);
    m_nameEdit->setPlainText(info.completeBaseName());
    m_nameEdit->adjustHeight();
    m_nameEdit->setProperty("oldPath", p);
    m_nameEdit->setProperty("suffix", info.suffix());
    m_nameEdit->blockSignals(false);
    
    lblType->setText(t); lblSize->setText(s); lblCtime->setText(ct); lblMtime->setText(mt); lblAtime->setText(at); lblPath->setText(p); lblEncrypted->setText(e ? "已加密" : "未加密");
    
    if (p != "-" && !p.isEmpty()) {
        RuntimeMeta rm = MetadataManager::instance().getMeta(p.toStdWString());
        setNote(rm.note);
        setURL(rm.url);
        setTags(rm.tags);
        
        QVector<QPair<QColor, float>> pal;
        for (const auto& entry : rm.palettes) {
            pal.append({entry.color, entry.ratio});
        }
        setPalettes(pal);
    }
    if (m_container) m_container->adjustSize();
}

void MetaPanel::setRating(int rating) {
    // 实现缺失的接口逻辑
    emit metadataChanged(rating, L"__NO_CHANGE__");
}
void MetaPanel::setColor(const std::wstring& color) {
    // 实现缺失的接口逻辑
    emit metadataChanged(-1, color);
}
void MetaPanel::setPinned(bool pinned) {
    Q_UNUSED(pinned);
    // 这里如果需要持久化 Pin 状态，应调用相关接口
}
void MetaPanel::setTags(const QStringList& tags) {
    while (QLayoutItem* item = m_tagFlowLayout->takeAt(0)) { if (QWidget* w = item->widget()) w->deleteLater(); delete item; }
    for (const QString& tag : tags) { TagPill* pill = new TagPill(tag, m_tagContainer); connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted); m_tagFlowLayout->addWidget(pill); }
}
void MetaPanel::setNote(const std::wstring& note) { 
    m_noteEdit->blockSignals(true); 
    m_noteEdit->setPlainText(QString::fromStdWString(note)); 
    m_noteEdit->adjustHeight();
    m_noteEdit->blockSignals(false); 
    if (m_container) m_container->adjustSize();
}
void MetaPanel::setURL(const std::wstring& url) { 
    m_linkEdit->blockSignals(true); 
    m_linkEdit->setPlainText(QString::fromStdWString(url)); 
    m_linkEdit->adjustHeight();
    m_linkEdit->blockSignals(false); 
    if (m_container) m_container->adjustSize();
}
void MetaPanel::setCategory(const QString& category) { 
    m_categoryEdit->blockSignals(true);
    m_categoryEdit->setPlainText(category); 
    m_categoryEdit->adjustHeight();
    m_categoryEdit->blockSignals(false);
    if (m_container) m_container->adjustSize();
}

void MetaPanel::setPalettes(const QVector<QPair<QColor, float>>& palette) {
    if (m_paletteCapsule) {
        m_paletteCapsule->setPalette(palette);
    }
    if (m_container) {
        m_container->adjustSize();
    }
}

bool MetaPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_noteEdit && event->type() == QEvent::FocusOut) {
        QString currentPath = lblPath->text(); if (currentPath != "-" && !currentPath.isEmpty()) MetadataManager::instance().setNote(currentPath.toStdWString(), m_noteEdit->toPlainText().toStdWString());
    } else if (watched == m_linkEdit && event->type() == QEvent::FocusOut) {
        QString currentPath = lblPath->text(); if (currentPath != "-" && !currentPath.isEmpty()) MetadataManager::instance().setURL(currentPath.toStdWString(), m_linkEdit->toPlainText().toStdWString());
    } else if (watched == m_nameEdit && event->type() == QEvent::FocusOut) {
        QString oldPath = m_nameEdit->property("oldPath").toString();
        QString newName = m_nameEdit->toPlainText().trimmed();
        
        // 2026-06-xx 物理加固：过滤非法文件名字符，防止重命名失败或破坏路径
        static const QString illegalChars = "\\/:*?\"<>|";
        for (auto c : illegalChars) newName.remove(c);
        m_nameEdit->setPlainText(newName);

        QString suffix = m_nameEdit->property("suffix").toString();
        if (!oldPath.isEmpty() && !newName.isEmpty()) {
            QFileInfo oldInfo(oldPath);
            if (newName != oldInfo.completeBaseName()) {
                QString newPath = oldInfo.absolutePath() + "/" + newName + (suffix.isEmpty() ? "" : "." + suffix);
                newPath = QDir::toNativeSeparators(newPath);

                // 2026-06-xx 工业级改进：检查目标路径是否已存在
                if (QFile::exists(newPath)) {
                    m_nameEdit->setPlainText(oldInfo.completeBaseName());
                    return true;
                }

                if (QFile::rename(oldPath, newPath)) {
                    MetadataManager::instance().renameItem(oldPath.toStdWString(), newPath.toStdWString());
                    lblPath->setText(newPath);
                    m_nameEdit->setProperty("oldPath", newPath);
                } else {
                    // 重命名失败，回滚文本
                    m_nameEdit->setPlainText(oldInfo.completeBaseName());
                }
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

} // namespace ArcMeta
