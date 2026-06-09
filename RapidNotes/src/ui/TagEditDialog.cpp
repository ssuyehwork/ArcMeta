#include "TagEditDialog.h"
#include "AdvancedTagSelector.h"
#include "../core/DatabaseManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>

TagEditDialog::TagEditDialog(const QString& currentTags, QWidget* parent) 
    : FramelessDialog("设置预设标签", parent) 
{
    // 2026-04-xx 按照用户要求：模态标签对话框不需要置顶、最小化、最大化按钮
    if (m_btnPin) m_btnPin->hide();
    if (m_minBtn) m_minBtn->hide();
    if (m_maxBtn) m_maxBtn->hide();

    // 1. 严格执行 500x350 规格要求
    resize(500, 350);
    setMinimumSize(500, 300);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(12);

    auto* lbl = new QLabel("标签 (胶囊化展示):");
    lbl->setStyleSheet("color: #aaa; font-size: 13px; font-weight: bold;");
    layout->addWidget(lbl);

    // 2. 集成胶囊标签编辑器
    m_tagEditor = new TagEditorWidget(this);
    QStringList tagList = currentTags.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
    for(QString& t : tagList) t = t.trimmed();
    m_tagEditor->setTags(tagList);
    
    // 连接双击信号，唤起高级选择器
    connect(m_tagEditor, &TagEditorWidget::doubleClicked, this, &TagEditDialog::openTagSelector);
    layout->addWidget(m_tagEditor);

    auto* tips = new QLabel("提示：双击空白区域可打开高级标签面板");
    tips->setStyleSheet("color: #666; font-size: 11px;");
    layout->addWidget(tips);

    layout->addStretch();

    // 3. 底部按钮
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    auto* btnCancel = new QPushButton("取消");
    btnCancel->setFixedSize(80, 32);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet("QPushButton { background-color: #333; color: #EEE; border: none; border-radius: 4px; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnCancel);

    auto* btnOk = new QPushButton("确定");
    btnOk->setFixedSize(80, 32);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setStyleSheet("QPushButton { background-color: #4a90e2; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
    connect(btnOk, &QPushButton::clicked, this, [this](){
        emit tagsConfirmed(getTags());
        accept();
    });
    btnLayout->addWidget(btnOk);

    layout->addLayout(btnLayout);
}

QString TagEditDialog::getTags() const {
    return m_tagEditor->tags().join(", ");
}

void TagEditDialog::openTagSelector() {
    auto* selector = new AdvancedTagSelector(this);
    
    // 准备数据
    auto recentTags = DatabaseManager::instance().getRecentTagsWithCounts(20);
    QStringList allTags = DatabaseManager::instance().getAllTags();
    QStringList selected = m_tagEditor->tags();

    selector->setup(recentTags, allTags, selected);
    
    // 监听确认并更新胶囊
    connect(selector, &AdvancedTagSelector::tagsConfirmed, [this](const QStringList& tags){
        m_tagEditor->setTags(tags);
    });

    selector->showAtCursor();
}
