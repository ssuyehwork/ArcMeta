#include "PresetTagsDialog.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../meta/CategoryRepo.h"
#include "../meta/MetadataManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QKeyEvent>
#include <QListWidget>
#include <QScrollArea>

namespace QuarkMeta {

// ==========================================
// 设置自动标签对话框主实现 (PresetTagsDialog)
// ==========================================
PresetTagsDialog::PresetTagsDialog(int categoryId, QWidget* parent)
    : FramelessDialog("设置自动标签", parent), m_categoryId(categoryId) {
    // 锁定固定宽度 420px，初始高度自适应
    setFixedWidth(420);
    // 禁用最大化、禁用边框拉伸
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);

    initUi();
    loadTags();
    populateTagPills();
    installEventFilter(this);
}

PresetTagsDialog::~PresetTagsDialog() {}

void PresetTagsDialog::initUi() {
    QVBoxLayout* mainL = new QVBoxLayout(m_contentArea);
    mainL->setContentsMargins(15, 15, 15, 15);
    mainL->setSpacing(12);

    // 文件夹名
    QLabel* folderNameLabel = new QLabel("文件夹名", this);
    folderNameLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    mainL->addWidget(folderNameLabel);

    m_folderNameEdit = new QLineEdit(this);
    m_folderNameEdit->setReadOnly(true);
    m_folderNameEdit->setFixedHeight(28);
    m_folderNameEdit->setStyleSheet(
        "QLineEdit { background: #151515; border: 1px solid #333; border-radius: 4px; padding: 0 8px; color: #AAA; font-size: 12px; }"
    );
    mainL->addWidget(m_folderNameEdit);

    // 自动添加标签区标题
    QLabel* tagLabel = new QLabel("自动添加标签", this);
    tagLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    mainL->addWidget(tagLabel);

    // 自动添加标签边框容器 (图 ① 指示 1 的空槽)
    m_tagContainer = new QFrame(this);
    m_tagContainer->setObjectName("TagContainer");
    m_tagContainer->setFrameShape(QFrame::StyledPanel);
    m_tagContainer->setStyleSheet(
        "QFrame#TagContainer {"
        "  background-color: #151515;"
        "  border: 1px solid #333333;"
        "  border-radius: 6px;"
        "}"
    );
    m_tagContainer->setCursor(Qt::PointingHandCursor);
    m_tagContainer->setMinimumHeight(42);

    m_flowLayout = new FlowLayout(m_tagContainer, 8, 8, 8);
    m_tagContainer->setLayout(m_flowLayout);
    m_tagContainer->installEventFilter(this);

    mainL->addWidget(m_tagContainer, 1);

    // 底部控制按钮
    QHBoxLayout* bottomL = new QHBoxLayout();
    bottomL->setSpacing(10);
    bottomL->addStretch();

    QPushButton* btnSave = new QPushButton("保存设置", this);
    btnSave->setFixedSize(90, 28);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setStyleSheet(
        "QPushButton { background-color: #1C97EA; color: #FFF; border: none; border-radius: 4px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #1886D2; }"
    );
    connect(btnSave, &QPushButton::clicked, this, &PresetTagsDialog::onSaveClicked);
    bottomL->addWidget(btnSave);

    QPushButton* btnCancel = new QPushButton("取消", this);
    btnCancel->setFixedSize(70, 28);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet(
        "QPushButton { background-color: #2D2D30; color: #BBB; border: 1px solid #333; border-radius: 4px; font-size: 11px; }"
        "QPushButton:hover { background-color: #3E3E42; color: #FFF; }"
    );
    connect(btnCancel, &QPushButton::clicked, this, &PresetTagsDialog::onCancelClicked);
    bottomL->addWidget(btnCancel);

    mainL->addLayout(bottomL);
}

void PresetTagsDialog::loadTags() {
    Category cat = CategoryRepo::getById(m_categoryId);
    m_categoryName = QString::fromStdWString(cat.name);
    m_folderNameEdit->setText(m_categoryName);

    m_presetTags.clear();
    for (const auto& ws : cat.presetTags) {
        m_presetTags.append(QString::fromStdWString(ws));
    }
}

void PresetTagsDialog::populateTagPills() {
    // 清理旧胶囊
    QLayoutItem* item;
    while ((item = m_flowLayout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }

    for (const QString& tag : m_presetTags) {
        TagPill* pill = new TagPill(tag, m_tagContainer);
        connect(pill, &TagPill::deleteRequested, this, &PresetTagsDialog::onTagDeleted);
        m_flowLayout->addWidget(pill);
    }

    recalculateAdaptiveHeight();
}

void PresetTagsDialog::onTagDeleted(const QString& tag) {
    m_presetTags.removeAll(tag);
    populateTagPills();
}

void PresetTagsDialog::recalculateAdaptiveHeight() {
    m_flowLayout->activate();
    int flowContentHeight = m_flowLayout->heightForWidth(m_tagContainer->width());
    
    int tagContainerHeight = qMax(42, flowContentHeight + 10);
    m_tagContainer->setFixedHeight(tagContainerHeight);

    layout()->activate();
    int requiredTotalHeight = sizeHint().height();

    setFixedHeight(requiredTotalHeight);
}

void PresetTagsDialog::onTagContainerClicked() {
    if (m_selectorOverlay) {
        m_selectorOverlay->raise();
        m_selectorOverlay->activateWindow();
        return;
    }

    // 实例化独立窗口，父对象传 nullptr 确保作为独立顶层 Tool 弹出
    m_selectorOverlay = new TagSelectorOverlay(m_presetTags, nullptr);
    
    // 计算弹出在红框正下方
    QPoint globalPos = m_tagContainer->mapToGlobal(QPoint(0, m_tagContainer->height() + 4));
    m_selectorOverlay->move(globalPos);
    m_selectorOverlay->show();
    m_selectorOverlay->raise();
    m_selectorOverlay->activateWindow(); // 显式夺取焦点

    connect(m_selectorOverlay, &TagSelectorOverlay::selectionChanged, this, [this](const QStringList& selected) {
        m_presetTags = selected;
        populateTagPills(); // 内部自动触发 recalculateAdaptiveHeight()
    });

    connect(m_selectorOverlay, &TagSelectorOverlay::overlayClosed, this, [this]() {
        m_selectorOverlay = nullptr;
    });
}

void PresetTagsDialog::mousePressEvent(QMouseEvent* event) {
    // 物理阻断，屏蔽基类拖拽移动
    QDialog::mousePressEvent(event);
}

void PresetTagsDialog::mouseMoveEvent(QMouseEvent* event) {
    // 物理阻断，屏蔽基类拖拽移动
    QDialog::mouseMoveEvent(event);
}

void PresetTagsDialog::mouseReleaseEvent(QMouseEvent* event) {
    QDialog::mouseReleaseEvent(event);
}

void PresetTagsDialog::onSaveClicked() {
    Category cat = CategoryRepo::getById(m_categoryId);
    cat.presetTags.clear();
    for (const QString& tag : m_presetTags) {
        cat.presetTags.push_back(tag.toStdWString());
    }
    
    if (CategoryRepo::update(cat)) {
        accept();
    }
}

void PresetTagsDialog::onCancelClicked() {
    reject();
}

bool PresetTagsDialog::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_tagContainer && event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            onTagContainerClicked();
            return true;
        }
    }
    return FramelessDialog::eventFilter(obj, event);
}

} // namespace QuarkMeta
