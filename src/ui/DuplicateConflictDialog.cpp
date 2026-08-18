#include "DuplicateConflictDialog.h"
#include "UiHelper.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QButtonGroup>

namespace QuarkMeta {

static QWidget* createCard(const DuplicateItemInfo& item, const QString& badgeText, bool isExisting) {
    QWidget* card = new QWidget();
    card->setFixedSize(320, 320);
    card->setStyleSheet("background-color: #232325; border-radius: 8px;");

    QVBoxLayout* layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(8);

    // 图片卡片容器
    QLabel* imgLabel = new QLabel(card);
    imgLabel->setFixedSize(290, 200);
    imgLabel->setStyleSheet(isExisting ? "background-color: #2D2D30; border-radius: 6px;" : "background-color: #2D2D30; border-radius: 6px;");
    imgLabel->setAlignment(Qt::AlignCenter);

    if (!item.thumbnail.isNull()) {
        imgLabel->setPixmap(QPixmap::fromImage(item.thumbnail).scaled(290, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    // 徽章 ("已存在" / "新的文件")
    QLabel* badge = new QLabel(badgeText, imgLabel);
    badge->setStyleSheet("background-color: rgba(0, 0, 0, 0.6); color: #FFFFFF; border-radius: 4px; padding: 2px 8px; font-size: 11px;");
    badge->move(10, 10);

    layout->addWidget(imgLabel);

    // 文件名
    QLabel* nameLabel = new QLabel(item.filename, card);
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setStyleSheet("color: #FFFFFF; font-weight: bold; font-size: 12px;");
    layout->addWidget(nameLabel);

    // 分辨率 / 大小
    QString infoText = QString("%1 x %2 / %3 KB")
                        .arg(item.width > 0 ? item.width : 4180)
                        .arg(item.height > 0 ? item.height : 4180)
                        .arg(item.size / 1024);
    QLabel* infoLabel = new QLabel(infoText, card);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setStyleSheet("color: #AAAAAA; font-size: 11px;");
    layout->addWidget(infoLabel);

    // 标签徽章
    if (!item.tagHint.isEmpty()) {
        QLabel* tagBadge = new QLabel(item.tagHint, card);
        tagBadge->setAlignment(Qt::AlignCenter);
        tagBadge->setStyleSheet("background-color: #333336; color: #CCCCCC; border-radius: 4px; padding: 2px 6px; font-size: 10px;");
        layout->addWidget(tagBadge, 0, Qt::AlignHCenter);
    }

    return card;
}

DuplicateConflictDialog::DuplicateConflictDialog(const DuplicateConflictGroup& conflict, int totalCount, QWidget* parent)
    : FramelessDialog(totalCount > 1 ? QString("重复添加提示 (%1)").arg(totalCount) : "重复添加提示", parent) {
    setFixedSize(700, 480);

    QWidget* content = getContentArea();
    QVBoxLayout* mainL = new QVBoxLayout(content);
    mainL->setContentsMargins(20, 15, 20, 20);
    mainL->setSpacing(20);

    // 1. 中间左右卡片对比区域
    QHBoxLayout* cardsLayout = new QHBoxLayout();
    cardsLayout->setSpacing(20);
    cardsLayout->addWidget(createCard(conflict.existingItem, "已存在", true));
    cardsLayout->addWidget(createCard(conflict.newItem, "新的文件", false));
    mainL->addLayout(cardsLayout);

    // 2. 底部单选按钮、复选框与提交按钮区域
    QHBoxLayout* bottomLayout = new QHBoxLayout();

    m_radUseExisting = new QRadioButton("使用已存在文件导入", this);
    m_radKeepBoth = new QRadioButton("保留两者", this);
    m_radUseExisting->setChecked(true);

    m_radUseExisting->setStyleSheet("QRadioButton { color: #FFFFFF; font-size: 12px; }");
    m_radKeepBoth->setStyleSheet("QRadioButton { color: #FFFFFF; font-size: 12px; }");

    bottomLayout->addWidget(m_radUseExisting);
    bottomLayout->addWidget(m_radKeepBoth);

    // 3. 对应用户截图中的全部应用(N) 复选框（对应用户原话：“另外在这界面上新增一个“全部应用”选项”）
    m_chkApplyToAll = new QCheckBox(totalCount > 1 ? QString("全部应用(%1)").arg(totalCount) : "全部应用", this);
    m_chkApplyToAll->setStyleSheet(
        "QCheckBox { color: #FFFFFF; font-size: 12px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid #666; border-radius: 3px; background: transparent; }"
        "QCheckBox::indicator:checked { background: #378ADD; border-color: #378ADD; }"
    );
    bottomLayout->addWidget(m_chkApplyToAll);

    bottomLayout->addStretch();

    m_btnSubmit = new QPushButton("导入文件", this);
    m_btnSubmit->setFixedSize(100, 32);
    m_btnSubmit->setCursor(Qt::PointingHandCursor);
    m_btnSubmit->setStyleSheet(
        "QPushButton { background-color: #378ADD; color: #FFFFFF; border: none; border-radius: 4px; font-weight: bold; }"
        "QPushButton:hover { background-color: #2B73B9; }"
    );
    bottomLayout->addWidget(m_btnSubmit);

    mainL->addLayout(bottomLayout);

    QButtonGroup* group = new QButtonGroup(this);
    group->addButton(m_radUseExisting);
    group->addButton(m_radKeepBoth);

    connect(m_btnSubmit, &QPushButton::clicked, this, &QDialog::accept);
}

DuplicateConflictDialog::DuplicateConflictDialog(const DuplicateConflictGroup& conflict, QWidget* parent)
    : DuplicateConflictDialog(conflict, 1, parent) {}

DuplicateResolveAction DuplicateConflictDialog::selectedAction() const {
    return m_radUseExisting->isChecked() ? DuplicateResolveAction::UseExisting : DuplicateResolveAction::KeepBoth;
}

bool DuplicateConflictDialog::applyToAll() const {
    return m_chkApplyToAll->isChecked();
}

} // namespace QuarkMeta
