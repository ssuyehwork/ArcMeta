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
#include <QTimer>

namespace ArcMeta {

CategoryPresetTagsDialog::CategoryPresetTagsDialog(const QString& folderName, 
                                                   const std::vector<std::wstring>& initialTags, 
                                                   QWidget* parent)
    : FramelessDialog("设置自动标签", parent), m_folderName(folderName), m_initialTags(initialTags)
{
    setVisibleButtons(Close);
    // 允许根据标签弹性变化，不再设定固定或过大的 minimumSize 限制高度
    setMinimumWidth(400);

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
    QSizePolicy sp = m_tagsContainer->sizePolicy();
    sp.setHeightForWidth(true);          // 让容器认领 FlowLayout 的 heightForWidth 能力
    m_tagsContainer->setSizePolicy(sp);
    m_tagsContainer->setMinimumHeight(40); // 弹性的，空状态下最小给 40px 高度
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

    tagsLayout->addWidget(m_tagsContainer); // 去掉滚动条，直接添加容器
    layout->addLayout(tagsLayout);

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

    // 初次布局完成后，调整对话框尺寸
    adjustDialogSize();
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
        adjustDialogSize();
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
    m_tagsContainer->updateGeometry();
    adjustDialogSize();
}

void CategoryPresetTagsDialog::adjustDialogSize() {
    // 异步延时处理，确保所有的 widget 布局计算已更新完毕
    QTimer::singleShot(50, this, [this]() {
        // 固定宽度为 480 像素，只允许高度自适应弹性收缩
        const int dialogWidth = 480;
        setFixedWidth(dialogWidth);

        // 移除多余的最小/最大高度限制
        setMinimumHeight(0);
        setMaximumHeight(16777215);

        // 触发 Qt 自带的高性能布局树大小调整，完美处理 DPI、字体及 margins 自适应
        adjustSize();

        // 确保最终尺寸在安全范围内
        int finalHeight = qBound(220, height(), 600);
        setFixedSize(dialogWidth, finalHeight);
    });
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
