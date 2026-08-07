#include "CategoryPresetTagsDialog.h"
#include "components/FlowLayout.h"
#include "components/TagPill.h"
#include "TagPickerPopover.h"
#include "UiHelper.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QCursor>

namespace ArcMeta {

CategoryPresetTagsDialog::CategoryPresetTagsDialog(const QString& folderName, 
                                                   const std::vector<std::wstring>& initialTags, 
                                                   QWidget* parent)
    : FramelessDialog("设置自动标签", parent), m_folderName(folderName), m_initialTags(initialTags)
{
    setVisibleButtons(Close);
    resize(480, 260); // 极简独立主界面尺寸：480x260 (对标图 2)
    setMinimumSize(400, 240);

    initLayout();

    // 实例化悬浮选择框（Child Overlay）并连接信号
    m_pickerPopover = new TagPickerPopover(this);
    m_pickerPopover->hide();
    connect(m_pickerPopover, &TagPickerPopover::tagSelected, this, &CategoryPresetTagsDialog::onTagSelectedFromPicker);
}

CategoryPresetTagsDialog::~CategoryPresetTagsDialog() {
}

void CategoryPresetTagsDialog::initLayout() {
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 15);
    layout->setSpacing(10);

    // 1. 文件夹名展示区（只读）
    auto* folderLayout = new QVBoxLayout();
    folderLayout->setSpacing(4);

    auto* folderLabel = new QLabel("文件夹名", m_contentArea);
    folderLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    folderLayout->addWidget(folderLabel);

    auto* folderEdit = new QLineEdit(m_folderName, m_contentArea);
    folderEdit->setEnabled(false);
    folderEdit->setMinimumHeight(30);
    folderEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #252526;"
        "  border: 1px solid #333333;"
        "  border-radius: 4px;"
        "  padding: 0px 8px;"
        "  color: #888888;"
        "  font-size: 12px;"
        "}"
    );
    folderLayout->addWidget(folderEdit);
    layout->addLayout(folderLayout);

    // 2. 自动添加标签区 (胶囊卡片容器区，点击空白调出选择框)
    auto* tagsLayout = new QVBoxLayout();
    tagsLayout->setSpacing(4);

    auto* tagsLabel = new QLabel("自动添加标签", m_contentArea);
    tagsLabel->setStyleSheet("color: #888888; font-size: 11px; font-weight: bold;");
    tagsLayout->addWidget(tagsLabel);

    m_tagsContainer = new QWidget(m_contentArea);
    m_tagsContainer->setObjectName("TagsContainer");
    m_tagsContainer->setMinimumHeight(80);
    m_tagsContainer->setCursor(Qt::PointingHandCursor);
    m_tagsContainer->setStyleSheet(
        "QWidget#TagsContainer {"
        "  background-color: #252526;"
        "  border: 1px solid #3C3C3C;"
        "  border-radius: 4px;"
        "}"
    );

    m_tagsFlow = new FlowLayout(m_tagsContainer, 6, 5, 5);
    m_tagsContainer->setLayout(m_tagsFlow);
    m_tagsContainer->installEventFilter(this); // 安装事件过滤器监听点击呼出选择框

    auto* scrollArea = new QScrollArea(m_contentArea);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; }");
    scrollArea->setWidget(m_tagsContainer);

    tagsLayout->addWidget(scrollArea, 1);
    layout->addLayout(tagsLayout, 1);

    // 初始化已有的预设标签
    for (const auto& tagW : m_initialTags) {
        addTagPill(QString::fromStdWString(tagW));
    }

    // 3. 底部动作按钮
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* btnCancel = new QPushButton("取消", m_contentArea);
    btnCancel->setFixedSize(80, 32);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet(
        "QPushButton { background-color: transparent; color: #888; border: 1px solid #444; border-radius: 4px; } "
        "QPushButton:hover { color: #EEE; background-color: #333; }"
    );
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOk = new QPushButton("保存设置", m_contentArea);
    btnOk->setFixedSize(90, 32);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setDefault(true);
    btnOk->setStyleSheet(
        "QPushButton { background-color: #3498db; color: white; border: none; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover { background-color: #2980b9; }"
    );
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(btnOk);

    layout->addLayout(btnLayout);
}

void CategoryPresetTagsDialog::addTagPill(const QString& tagName) {
    auto* pill = new TagPill(tagName, m_tagsContainer);
    pill->setProperty("tagText", tagName);
    m_tagsFlow->addWidget(pill);

    connect(pill, &TagPill::deleteRequested, this, &CategoryPresetTagsDialog::onRemoveTag);
}

void CategoryPresetTagsDialog::onTagSelectedFromPicker(const QString& tagName) {
    bool exists = false;
    for (int i = 0; i < m_tagsFlow->count(); ++i) {
        auto* item = m_tagsFlow->itemAt(i);
        if (item->widget()) {
            auto* pill = qobject_cast<TagPill*>(item->widget());
            if (pill && pill->property("tagText").toString() == tagName) {
                exists = true;
                break;
            }
        }
    }

    if (!exists) {
        addTagPill(tagName);
        m_tagsContainer->updateGeometry();
    }
}

void CategoryPresetTagsDialog::onRemoveTag(const QString& tagName) {
    for (int i = 0; i < m_tagsFlow->count(); ++i) {
        auto* item = m_tagsFlow->itemAt(i);
        if (item->widget()) {
            auto* pill = qobject_cast<TagPill*>(item->widget());
            if (pill && pill->property("tagText").toString() == tagName) {
                m_tagsFlow->removeWidget(pill);
                pill->deleteLater();
                break;
            }
        }
    }
}

std::vector<std::wstring> CategoryPresetTagsDialog::getPresetTags() const {
    std::vector<std::wstring> tags;
    for (int i = 0; i < m_tagsFlow->count(); ++i) {
        auto* item = m_tagsFlow->itemAt(i);
        if (item->widget()) {
            auto* pill = qobject_cast<TagPill*>(item->widget());
            if (pill) {
                tags.push_back(pill->property("tagText").toString().toStdWString());
            }
        }
    }
    return tags;
}

bool CategoryPresetTagsDialog::eventFilter(QObject* watched, QEvent* event) {
    // 监听胶囊容器空白处鼠标点击，在点击坐标下方弹出悬浮选择框
    if (watched == m_tagsContainer && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            QPoint globalPos = mouseEvent->globalPosition().toPoint();
            m_pickerPopover->showAt(globalPos);
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

} // namespace ArcMeta
