#include "MetaPanel.h"
#include "SvgIcons.h"
#include "ToolTipOverlay.h"
#include "UiHelper.h"
#include "Logger.h"
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
#include <QWidgetAction>
#include <QLineEdit>
#include <QDir>
#include <QTextDocument>
#include <QtMath>
#include <QTimer>
#include <QRegularExpressionValidator>

// 🚨 彻底删除 #include "../meta/MetadataManager.h"，实现 100% 数据库与业务解耦！

namespace ArcMeta {

// --- ElasticEdit 实现 ---
ElasticEdit::ElasticEdit(QWidget* parent) : QTextEdit(parent) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setLineWrapMode(QTextEdit::WidgetWidth);
    QTextOption opt = document()->defaultTextOption();
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->setDefaultTextOption(opt);
    document()->setDocumentMargin(0);
    connect(this, &QTextEdit::textChanged, this, &ElasticEdit::adjustHeight);
}

void ElasticEdit::adjustHeight() {
    int horizontalPadding = 20;
    int verticalPadding = 8;
    int border = 2;
    int w = width();
    if (w > 50) {
        int textW = w - horizontalPadding - border;
        if (document()->textWidth() != textW) {
            document()->setTextWidth(textW);
        }
    }
    qreal docHeight = document()->size().height();
    int newHeight = qMax(28, (int)qCeil(docHeight + verticalPadding + border)); 
    if (this->height() != newHeight) {
        setFixedHeight(newHeight);
        updateGeometry(); 
        QWidget* p = parentWidget();
        while (p) {
            if (p->layout()) p->layout()->activate();
            if (qobject_cast<QScrollArea*>(p)) break;
            p = p->parentWidget();
        }
    }
}

void ElasticEdit::resizeEvent(QResizeEvent* e) {
    QTextEdit::resizeEvent(e);
    adjustHeight();
}

void ElasticEdit::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier)) {
        emit returnPressed();
        clearFocus();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

// --- ColorPill 实现 ---
ColorPill::ColorPill(const QColor& color, float ratio, QWidget* parent) : QWidget(parent) {
    setFixedSize(16, 16);
    setCursor(Qt::PointingHandCursor);
    setData(color, ratio);
}

void ColorPill::setData(const QColor& color, float ratio) {
    m_color = color;
    m_ratio = ratio;
    update();
}

void ColorPill::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawRoundedRect(rect(), 4, 4);
    if (m_hovered) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(Qt::white, 1.0));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
    }
}

void ColorPill::enterEvent(QEnterEvent*) {
    m_hovered = true;
    QString hex = m_color.name().toUpper();
    int ratio = qRound(m_ratio * 100);
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("%1 (%2%)").arg(hex).arg(ratio), 0);
    update();
}

void ColorPill::leaveEvent(QEvent*) {
    m_hovered = false;
    ToolTipOverlay::hideTip();
    update();
}

void ColorPill::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        emit colorSelected(m_color);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        QMenu menu(this);
        UiHelper::applyMenuStyle(&menu);
        QColor color = m_color;
        menu.addAction("搜索相似颜色的项目", [this, color]() { emit colorSelected(color); });
        menu.addSeparator();
        QString hex = color.name().toUpper();
        menu.addAction(QString("复制 %1").arg(hex), [hex]() { QApplication::clipboard()->setText(hex); });
        menu.addSeparator();
        menu.addAction("设置为自定义主色", [this, color]() { emit requestSetAsPrimary(color); });
        menu.exec(event->globalPosition().toPoint());
    }
    QWidget::mousePressEvent(event);
}

// --- TagPill 实现 ---
TagPill::TagPill(const QString& text, QWidget* parent) : QWidget(parent), m_text(text) {
    setFixedHeight(22);
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 4, 0);
    layout->setSpacing(4);
    m_label = new QLabel(text, this);
    m_label->setStyleSheet("color: #EEEEEE; font-size: 12px; border: none; background: transparent;");
    m_closeBtn = new QPushButton(this);
    m_closeBtn->setFixedSize(14, 14);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setIcon(UiHelper::getIcon("close", QColor("#B0B0B0"), 12));
    m_closeBtn->setIconSize(QSize(10, 10));
    m_closeBtn->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: #3E3E42; border-radius: 2px; }");
    layout->addWidget(m_label);
    layout->addWidget(m_closeBtn);
    connect(m_closeBtn, &QPushButton::clicked, [this]() { emit deleteRequested(m_text); });
    setData(text);
}

void TagPill::setData(const QString& text) {
    m_text = text;
    setProperty("tagText", text);
    m_label->setText(text);
    QFontMetrics fm(m_label->font());
    setFixedWidth(fm.horizontalAdvance(text) + 30);
}

void TagPill::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#2B2B2B"));
    painter.setPen(QPen(QColor("#3c3c3c"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 11, 11);
}

// --- FlowLayout 实现 ---
FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) { setContentsMargins(margin, margin, margin, margin); }
FlowLayout::~FlowLayout() { QLayoutItem *item; while ((item = takeAt(0))) delete item; }
void FlowLayout::addItem(QLayoutItem *item) { itemList.append(item); invalidate(); }
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

// --- MetaPanel 实现 ---
MetaPanel::MetaPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("MetadataContainer"); 
    setAttribute(Qt::WA_StyledBackground, true); 
    setMinimumWidth(230); 
    setStyleSheet("color: #EEEEEE;");
    m_mainLayout = new QVBoxLayout(this); 
    m_mainLayout->setContentsMargins(0, 0, 0, 0); 
    m_mainLayout->setSpacing(0);
    
    m_adjustTimer = new QTimer(this);
    m_adjustTimer->setSingleShot(true);
    m_adjustTimer->setInterval(50);
    connect(m_adjustTimer, &QTimer::timeout, this, &MetaPanel::adjustFlowHeights);

    setContextMenuPolicy(Qt::CustomContextMenu);
    initUi();
}

void MetaPanel::initUi() {
    QWidget* header = new QWidget(this); 
    header->setObjectName("ContainerHeader"); 
    header->setFixedHeight(32);
    header->setStyleSheet("QWidget#ContainerHeader { background-color: #252526; border-bottom: 1px solid #333; }");
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 0, 5, 0);
    headerLayout->setSpacing(5);
    QLabel* iconLabel = new QLabel(header); 
    iconLabel->setPixmap(UiHelper::getIcon("all_data", QColor("#4a90e2"), 18).pixmap(18, 18)); 
    headerLayout->addWidget(iconLabel);
    QLabel* titleLabel = new QLabel("元数据", header); 
    titleLabel->setStyleSheet("font-size: 12px; color: #4a90e2; background: transparent; border: none;"); 
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    m_mainLayout->addWidget(header);

    m_scrollArea = new QScrollArea(this); 
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); 
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setWidgetResizable(true); 
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    
    m_container = new QWidget(m_scrollArea); 
    m_containerLayout = new QVBoxLayout(m_container); 
    m_containerLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    m_containerLayout->setContentsMargins(10, 10, 10, 10); 
    m_containerLayout->setSpacing(8);
    
    m_paletteBox = new QWidget(m_container);
    m_paletteBox->setObjectName("PaletteBox");
    m_paletteBox->setMinimumHeight(28);
    m_paletteBox->setStyleSheet("QWidget#PaletteBox { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; }");
    
    m_paletteFlowLayout = new FlowLayout(m_paletteBox, 6, 6, 6);
    m_paletteFlowLayout->setContentsMargins(10, 6, 10, 6);
    m_containerLayout->addWidget(m_paletteBox);

    m_nameEdit = new ElasticEdit(m_container);
    m_nameEdit->setPlaceholderText("文件名称...");
    m_nameEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #EEEEEE; font-weight: normal; }");
    m_nameEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_nameEdit);

    m_noteEdit = new ElasticEdit(m_container);
    m_noteEdit->setPlaceholderText("添加备注说明...");
    m_noteEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #AAAAAA; font-weight: normal; }");
    m_noteEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_noteEdit);

    m_linkEdit = new ElasticEdit(m_container);
    m_linkEdit->setPlaceholderText("添加链接...");
    m_linkEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #4a90e2; font-weight: normal; }");
    m_linkEdit->installEventFilter(this);
    m_containerLayout->addWidget(m_linkEdit);

    m_tagBox = new QWidget(m_container);
    QVBoxLayout* tagL = new QVBoxLayout(m_tagBox);
    tagL->setContentsMargins(0, 0, 0, 0);
    tagL->setSpacing(8);
    
    m_tagContainer = new QWidget(m_tagBox);
    m_tagFlowLayout = new FlowLayout(m_tagContainer, 0, 4, 4);
    tagL->addWidget(m_tagContainer);

    m_tagEdit = new ElasticEdit(m_tagBox);
    m_tagEdit->setPlaceholderText("输入标签...");
    m_tagEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 10px; font-size: 12px; color: #AAAAAA; font-weight: normal; }");
    connect(m_tagEdit, &ElasticEdit::returnPressed, this, &MetaPanel::onTagAdded);
    m_tagEdit->installEventFilter(this);
    tagL->addWidget(m_tagEdit);
    m_containerLayout->addWidget(m_tagBox);

    m_categoryEdit = new ElasticEdit(m_container);
    m_categoryEdit->setReadOnly(true);
    m_categoryEdit->setPlaceholderText("所属分类...");
    m_categoryEdit->setStyleSheet("QTextEdit { background: #252526; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 8px; font-size: 12px; color: #EEEEEE; font-weight: normal; }");
    m_containerLayout->addWidget(m_categoryEdit);

    m_containerLayout->addWidget(createSeparator());

    addInfoRow("类型", lblType); 
    addInfoRow("大小", lblSize);
    addInfoRow("尺寸", lblDimensions);
    addInfoRow("创建时间", lblCtime); 
    addInfoRow("修改时间", lblMtime); 
    addInfoRow("访问时间", lblAtime);
    
    QWidget* pathRow = new QWidget(m_container); 
    QHBoxLayout* pathL = new QHBoxLayout(pathRow);
    pathL->setContentsMargins(0, 2, 0, 2); 
    pathL->setSpacing(8);
    QLabel* pathKey = new QLabel("物理路径", pathRow);
    pathKey->setFixedWidth(80);
    pathKey->setStyleSheet("font-size: 12px; color: #888888;");
    pathL->addWidget(pathKey, 0, Qt::AlignTop);
    
    m_pathEdit = new ElasticEdit(pathRow);
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setStyleSheet("QTextEdit { background: transparent; border: none; padding: 0; font-size: 12px; color: #CCCCCC; }");
    pathL->addWidget(m_pathEdit, 1);
    m_containerLayout->addWidget(pathRow);

    addInfoRow("加密状态", lblEncrypted);

    m_containerLayout->addStretch(1);
    m_scrollArea->setWidget(m_container);
    m_mainLayout->addWidget(m_scrollArea);
}

void MetaPanel::addInfoRow(const QString& label, QLabel*& valueLabel) {
    QWidget* row = new QWidget(m_container); 
    QHBoxLayout* rl = new QHBoxLayout(row); 
    rl->setContentsMargins(0, 2, 0, 2); 
    rl->setSpacing(8); 
    
    QLabel* kl = new QLabel(label, row); 
    kl->setFixedWidth(80);
    kl->setStyleSheet("font-size: 12px; color: #888888;"); 
    rl->addWidget(kl, 0, Qt::AlignTop);

    valueLabel = new QLabel("-", row); 
    valueLabel->setWordWrap(true); 
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setStyleSheet("font-size: 12px; color: #CCCCCC; line-height: 1.5;");
    valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop); 
    rl->addWidget(valueLabel, 1); 
    
    m_containerLayout->addWidget(row);
}

QFrame* MetaPanel::createSeparator() {
    QFrame* l = new QFrame(this); 
    l->setFrameShape(QFrame::HLine); 
    l->setFixedHeight(1); 
    l->setStyleSheet("background-color: #333333; border: none; margin: 4px 0;"); 
    return l; 
}

void MetaPanel::onTagAdded() {
    QString text = m_tagEdit->toPlainText().trimmed();
    if (!text.isEmpty() && !m_selectedPaths.isEmpty()) {
        emit tagsChanged(m_selectedPaths, QStringList() << text);
        m_tagEdit->clear();
        m_tagEdit->adjustHeight();
    }
}

void MetaPanel::onTagDeleted(const QString& text) {
    if (m_selectedPaths.isEmpty()) return;
    
    for (int i = 0; i < m_tagFlowLayout->count(); ++i) {
        QLayoutItem* item = m_tagFlowLayout->itemAt(i);
        TagPill* pill = qobject_cast<TagPill*>(item->widget());
        if (pill && pill->property("tagText").toString() == text) {
            m_tagFlowLayout->takeAt(i);
            pill->deleteLater();
            delete item;
            break;
        }
    }
    m_adjustTimer->start();
    
    emit tagsChanged(m_selectedPaths, QStringList() << "-" + text);
}

void MetaPanel::resizeEvent(QResizeEvent* event) {
    QFrame::resizeEvent(event);
    QTimer::singleShot(0, this, [this]() {
        if (!m_scrollArea || !m_container) return;
        int viewportW = m_scrollArea->viewport()->width();
        if (viewportW < 100) return;

        if (m_container->width() != viewportW) {
            m_container->setFixedWidth(viewportW);
        }
        
        int maxW = viewportW - 20; 
        if (maxW > 50) {
            auto syncWidthAndHeight = [maxW](ElasticEdit* edit) {
                if (edit && edit->width() != maxW) {
                    edit->setFixedWidth(maxW);
                    edit->adjustHeight();
                }
            };

            syncWidthAndHeight(m_nameEdit);
            syncWidthAndHeight(m_noteEdit);
            syncWidthAndHeight(m_linkEdit);
            syncWidthAndHeight(m_tagEdit);
            syncWidthAndHeight(m_categoryEdit);
            
            int pathW = maxW - 88;
            if (m_pathEdit && pathW > 0) {
                m_pathEdit->setFixedWidth(pathW);
                m_pathEdit->adjustHeight();
            }
            
            if (m_paletteBox) m_paletteBox->setFixedWidth(maxW);
            if (m_tagBox) m_tagBox->setFixedWidth(maxW);
            if (m_tagContainer) m_tagContainer->setFixedWidth(maxW);
            
            adjustFlowHeights();
            m_container->adjustSize();
        }
    });
}

void MetaPanel::adjustFlowHeights() {
    if (m_paletteBox && m_paletteFlowLayout) {
        int contentH = m_paletteFlowLayout->heightForWidth(m_paletteBox->width());
        int newH = qMax(28, contentH);
        if (m_paletteBox->height() != newH) {
            m_paletteBox->setFixedHeight(newH);
        }
        m_paletteFlowLayout->activate();
    }
    if (m_tagContainer && m_tagFlowLayout) {
        int contentH = m_tagFlowLayout->heightForWidth(m_tagContainer->width());
        if (m_tagContainer->height() != contentH) {
            m_tagContainer->setFixedHeight(contentH);
        }
        m_tagFlowLayout->activate();
    }
}

void MetaPanel::showEvent(QShowEvent* event) {
    QFrame::showEvent(event);
    QResizeEvent e(size(), size());
    MetaPanel::resizeEvent(&e);
}

void MetaPanel::updateInfo(const QString& n, const QString& t, const QString& s, 
                            const QString& ct, const QString& mt, const QString& at, 
                            const QString& p, bool e, int width, int height) {
    m_isInternalUpdating = true;
    
    QFileInfo info(n);
    m_nameEdit->setPlainText(info.completeBaseName());
    m_nameEdit->adjustHeight();
    m_nameEdit->setProperty("oldPath", p);
    m_nameEdit->setProperty("suffix", info.suffix());
    
    lblType->setText(t); 
    lblSize->setText(s); 
    lblCtime->setText(ct); 
    lblMtime->setText(mt); 
    lblAtime->setText(at); 
    
    m_pathEdit->setPlainText(p);
    m_pathEdit->adjustHeight();

    lblEncrypted->setText(e ? "已加密" : "未加密");
    
    if (width > 0 && height > 0) {
        lblDimensions->setText(QString("%1 x %2 像素").arg(width).arg(height));
        if (lblDimensions->parentWidget()) lblDimensions->parentWidget()->show();
    } else {
        lblDimensions->setText("-");
        if (lblDimensions->parentWidget()) lblDimensions->parentWidget()->hide();
    }

    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setTags(const QStringList& tags) {
    while (QLayoutItem* item = m_tagFlowLayout->takeAt(0)) {
        TagPill* pill = qobject_cast<TagPill*>(item->widget());
        if (pill) {
            pill->hide();
            m_tagPool.append(pill);
        }
        delete item;
    }

    for (const QString& tag : tags) {
        TagPill* pill = nullptr;
        if (!m_tagPool.isEmpty()) {
            pill = m_tagPool.takeFirst();
            pill->setData(tag);
        } else {
            pill = new TagPill(tag, m_tagContainer);
            connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted);
        }
        pill->show();
        m_tagFlowLayout->addWidget(pill);
    }
    m_adjustTimer->start();
}

void MetaPanel::setNote(const QString& note) { 
    m_isInternalUpdating = true;
    m_noteEdit->setPlainText(note); 
    m_noteEdit->adjustHeight();
    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setNote(const std::wstring& note) {
    setNote(QString::fromStdWString(note));
}

void MetaPanel::setURL(const QString& url) { 
    m_isInternalUpdating = true;
    m_linkEdit->setPlainText(url); 
    m_linkEdit->adjustHeight();
    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setURL(const std::wstring& url) {
    setURL(QString::fromStdWString(url));
}

void MetaPanel::setCategory(const QString& category) { 
    m_isInternalUpdating = true;
    m_categoryEdit->setPlainText(category); 
    m_categoryEdit->adjustHeight();
    if (m_container) m_container->adjustSize();
    m_isInternalUpdating = false;
}

void MetaPanel::setPalettes(const QVector<QPair<QColor, float>>& palette) {
    if (!m_paletteFlowLayout) return;

    while (QLayoutItem* item = m_paletteFlowLayout->takeAt(0)) {
        ColorPill* pill = qobject_cast<ColorPill*>(item->widget());
        if (pill) {
            pill->hide();
            m_colorPool.append(pill);
        }
        delete item;
    }

    for (const auto& entry : palette) {
        ColorPill* pill = nullptr;
        if (!m_colorPool.isEmpty()) {
            pill = m_colorPool.takeFirst();
            pill->setData(entry.first, entry.second);
        } else {
            pill = new ColorPill(entry.first, entry.second, m_paletteBox);
            pill->setStyleSheet("background: transparent; border: none;");
            connect(pill, &ColorPill::colorSelected, [this](const QColor& c){ emit searchByColor(c); });
            connect(pill, &ColorPill::requestSetAsPrimary, this, &MetaPanel::setAsPrimaryColor);
        }
        pill->show();
        m_paletteFlowLayout->addWidget(pill);
    }

    m_paletteFlowLayout->invalidate();
    m_paletteBox->update();
    m_adjustTimer->start();
}

bool MetaPanel::eventFilter(QObject* watched, QEvent* event) {
    if (m_isInternalUpdating) return QFrame::eventFilter(watched, event);

    if (watched == m_noteEdit && event->type() == QEvent::FocusOut) {
        if (!m_selectedPaths.isEmpty()) {
            QString newNote = m_noteEdit->toPlainText();
            emit noteEdited(m_selectedPaths, newNote);
        }
    } else if (watched == m_linkEdit && event->type() == QEvent::FocusOut) {
        if (!m_selectedPaths.isEmpty()) {
            QString newUrl = m_linkEdit->toPlainText();
            emit linkEdited(m_selectedPaths, newUrl);
        }
    } else if (watched == m_nameEdit && event->type() == QEvent::FocusOut) {
        QString oldPath = m_nameEdit->property("oldPath").toString();
        QString newName = m_nameEdit->toPlainText().trimmed();
        
        static const QRegularExpression illegalRegex("[\\\\/:*?\"<>|]");
        newName.remove(illegalRegex);
        m_nameEdit->setPlainText(newName);

        QString suffix = m_nameEdit->property("suffix").toString();
        if (!oldPath.isEmpty() && !newName.isEmpty()) {
            QFileInfo oldInfo(oldPath);
            if (newName != oldInfo.completeBaseName()) {
                QString newPath = oldInfo.absolutePath() + "/" + newName + (suffix.isEmpty() ? "" : "." + suffix);
                newPath = QDir::toNativeSeparators(newPath);
                
                if (QFile::exists(newPath)) {
                    m_nameEdit->setPlainText(oldInfo.completeBaseName());
                    return true;
                }

                emit renameRequested(oldPath, newPath);
            }
        }
    }
    return QFrame::eventFilter(watched, event);
}

void MetaPanel::setAsPrimaryColor(const QColor& color) {
    QString currentPath = m_pathEdit->toPlainText().trimmed();
    if (!currentPath.isEmpty()) {
        emit primaryColorChanged(currentPath, color);
    }
}

} // namespace ArcMeta