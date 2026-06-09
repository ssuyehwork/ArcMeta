#include "TagEditorWidget.h"
#include "IconHelper.h"
#include <QMouseEvent>
#include <QVariant>
#include <QRegularExpression>
#include <utility>
#include "StringUtils.h"

TagEditorWidget::TagEditorWidget(QWidget* parent) : QFrame(parent) {
    setObjectName("TagEditor");
    setMinimumHeight(150);
    setCursor(Qt::IBeamCursor);
    
    // 基础样式：圆角矩形，深色半透明感
    setStyleSheet(
        "QFrame#TagEditor {"
        "  background-color: #1A1A1A;"
        "  border: 1px solid #333;"
        "  border-radius: 8px;"
        "}"
        "QFrame#TagEditor:hover {"
        "  border: 1px solid #4a90e2;"
        "}"
    );

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // 使用 FlowLayout 自动换行
    m_flow = new FlowLayout(0, 8, 8);
    mainLayout->addLayout(m_flow);
    mainLayout->addStretch();
}

void TagEditorWidget::setTags(const QStringList& tags) {
    m_tags = tags;
    updateChips();
}

void TagEditorWidget::addTag(const QString& tag) {
    if (tag.isEmpty() || m_tags.contains(tag)) return;
    m_tags.append(tag);
    updateChips();
    emit tagsChanged();
}

void TagEditorWidget::removeTag(const QString& tag) {
    if (m_tags.removeAll(tag) > 0) {
        updateChips();
        emit tagsChanged();
    }
}

void TagEditorWidget::clear() {
    m_tags.clear();
    updateChips();
    emit tagsChanged();
}

void TagEditorWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked();
    }
    QFrame::mouseDoubleClickEvent(event);
}

void TagEditorWidget::updateChips() {
    // 清空现有 Chips
    QLayoutItem* child;
    while ((child = m_flow->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    // 重新创建
    for (const QString& tag : std::as_const(m_tags)) {
        m_flow->addWidget(createChip(tag));
    }
}

QWidget* TagEditorWidget::createChip(const QString& tag) {
    auto* chip = new QWidget();
    chip->setObjectName("TagChip");
    chip->setStyleSheet(
        "QWidget#TagChip {"
        "  background-color: #2D2D2D;"
        "  border: 1px solid #444;"
        "  border-radius: 12px;"
        "}"
        "QWidget#TagChip:hover {"
        "  background-color: #383838;"
        "}"
    );

    auto* layout = new QHBoxLayout(chip);
    layout->setContentsMargins(10, 4, 10, 4);
    layout->setSpacing(6);

    auto* label = new QLabel(tag);
    label->setStyleSheet("color: #EEE; font-size: 12px; border: none; background: transparent;");
    layout->addWidget(label);

    auto* btnClose = new QPushButton();
    btnClose->setFixedSize(14, 14);
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setIcon(IconHelper::getIcon("close", "#888", 12));
//     btnClose->setToolTip("移除标签");
    btnClose->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 7px; }"
        "QPushButton:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
    );
    
    connect(btnClose, &QPushButton::clicked, this, [this, tag](){
        removeTag(tag);
    });
    
    layout->addWidget(btnClose);
    return chip;
}
