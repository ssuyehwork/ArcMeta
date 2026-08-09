#include "BatchCreateDialog.h"
#include "ToolTipOverlay.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>

namespace ArcMeta {

BatchCreateDialog::BatchCreateDialog(const QString& currentDirectory, QWidget* parent)
    : FramelessDialog("批量创建 - ArcMeta", parent), m_currentDir(currentDirectory) {
    resize(550, 420);
    initContent();
    applyTheme();
}

void BatchCreateDialog::initContent() {
    QWidget* content = getContentArea();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(12);

    // 顶部设置
    QHBoxLayout* topSettingsL = new QHBoxLayout();
    topSettingsL->setSpacing(15);

    // 1. 创建类型
    QLabel* typeLabel = new QLabel("类型:", this);
    typeLabel->setStyleSheet("color: #BBB; font-weight: bold;");
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem("文件夹", 0);
    m_typeCombo->addItem("文件", 1);
    m_typeCombo->setFixedHeight(25);
    m_typeCombo->setFixedWidth(100);

    // 2. 后缀名设置 (仅在创建类型为文件时有效)
    QLabel* suffixLabel = new QLabel("后缀名:", this);
    suffixLabel->setStyleSheet("color: #BBB; font-weight: bold;");
    m_suffixEdit = new QLineEdit(this);
    m_suffixEdit->setPlaceholderText(".txt");
    m_suffixEdit->setText(".txt");
    m_suffixEdit->setFixedHeight(25);
    m_suffixEdit->setFixedWidth(80);
    m_suffixEdit->setEnabled(false); // 默认选择文件夹时禁用

    // 3. 创建数量
    QLabel* countLabel = new QLabel("数量:", this);
    countLabel->setStyleSheet("color: #BBB; font-weight: bold;");
    m_countSpin = new QSpinBox(this);
    m_countSpin->setRange(1, 10000);
    m_countSpin->setValue(5);
    m_countSpin->setFixedHeight(25);
    m_countSpin->setFixedWidth(80);

    topSettingsL->addWidget(typeLabel);
    topSettingsL->addWidget(m_typeCombo);
    topSettingsL->addWidget(suffixLabel);
    topSettingsL->addWidget(m_suffixEdit);
    topSettingsL->addWidget(countLabel);
    topSettingsL->addWidget(m_countSpin);
    topSettingsL->addStretch();
    layout->addLayout(topSettingsL);

    // 规则容器区
    QLabel* ruleLabel = new QLabel("命名规则构造器 (按照添加规则管道生成):", this);
    ruleLabel->setStyleSheet("color: #888; font-size: 11px;");
    layout->addWidget(ruleLabel);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent; border: none;");

    m_rulesContainer = new QWidget(scroll);
    m_rulesContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_rulesLayout = new QVBoxLayout(m_rulesContainer);
    m_rulesLayout->setContentsMargins(0, 0, 0, 0);
    m_rulesLayout->setSpacing(4);

    scroll->setWidget(m_rulesContainer);
    layout->addWidget(scroll, 1);

    // 底部动作
    QHBoxLayout* bottomL = new QHBoxLayout();
    bottomL->addStretch();

    QPushButton* btnCancel = new QPushButton("取消", this);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFixedSize(90, 28);
    btnCancel->setStyleSheet("QPushButton { background: transparent; color: #BBB; border: 1px solid #444; border-radius: 4px; } QPushButton:hover { background: #3E3E42; }");
    bottomL->addWidget(btnCancel);

    QPushButton* btnOk = new QPushButton("开始创建", this);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setFixedSize(100, 28);
    btnOk->setStyleSheet("QPushButton { background: #007ACC; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background: #1C97EA; }");
    bottomL->addWidget(btnOk);
    layout->addLayout(bottomL);

    // 触发联动：类型切换控制后缀可用性
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        m_suffixEdit->setEnabled(index == 1);
    });

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &BatchCreateDialog::onExecute);

    // 初始配置一行规则文本
    onAddRow();
}

void BatchCreateDialog::onAddRow() {
    RuleRow* row = new RuleRow(m_rulesContainer);
    m_rulesLayout->addWidget(row);
    m_ruleRows.append(row);

    connect(row, &RuleRow::addRequested, this, &BatchCreateDialog::onAddRow);
    connect(row, &RuleRow::removeRequested, [this, row]() {
        if (m_ruleRows.size() > 1) {
            m_ruleRows.removeOne(row);
            row->deleteLater();
        }
    });
}

void BatchCreateDialog::applyTheme() {
    static const QString arrowPath = UiHelper::getSvgTempFilePath("dropdown_triangle", QColor("#AAAAAA"));
    setStyleSheet(QString(
        "QDialog { background-color: #1E1E1E; color: #BBB; }"
        "QLineEdit { background: #252526; border: 1px solid #444; border-radius: 4px; padding: 2px 5px; color: #EEE; }"
        "QSpinBox { background: #252526; border: 1px solid #444; border-radius: 4px; color: #EEE; }"
        "QComboBox { background: #252526; border: 1px solid #444; border-radius: 4px; padding: 1px 4px; color: #EEE; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow { image: url(%1); width: 12px; height: 12px; }"
        "QComboBox QAbstractItemView { background-color: #2D2D2D; border: 1px solid #444; selection-background-color: #3E3E42; selection-color: white; color: #EEE; outline: 0; }"
    ).arg(arrowPath));
}

QString BatchCreateDialog::renderOne(int index, const std::vector<RenameRule>& rules) {
    QString name = "";
    for (const auto& rule : rules) {
        switch (rule.type) {
            case RenameComponentType::Text:
                name += rule.value;
                break;
            case RenameComponentType::Sequence: {
                int val = rule.start + index; // 支持递增序列
                name += QString::number(val).rightJustified(rule.padding, '0');
                break;
            }
            case RenameComponentType::Date:
                name += QDateTime::currentDateTime().toString(rule.value.isEmpty() ? "yyyyMMdd" : rule.value);
                break;
            case RenameComponentType::OriginalName:
                // 在新建时若无原有原名，则回退使用 "NewItem" 占位符进行拼接
                name += "NewItem";
                break;
            default:
                break;
        }
    }
    return name;
}

void BatchCreateDialog::onExecute() {
    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) {
        rules.push_back(row->getRule());
    }

    if (rules.empty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "规则不能为空！", 1500, QColor("#E81123"));
        return;
    }

    int createCount = m_countSpin->value();
    bool isFile = m_typeCombo->currentData().toInt() == 1;
    QString rawSuffix = isFile ? m_suffixEdit->text().trimmed() : "";
    if (isFile && !rawSuffix.startsWith(".")) {
        rawSuffix = "." + rawSuffix;
    }

    QDir dir(m_currentDir);
    int itemsCreated = 0;

    for (int i = 0; i < createCount; ++i) {
        QString renderedName = renderOne(i, rules);
        if (renderedName.isEmpty()) {
            renderedName = QString("NewItem_%1").arg(i + 1);
        }

        if (isFile) {
            renderedName += rawSuffix;
        }

        QString targetPath = dir.absoluteFilePath(renderedName);

        if (isFile) {
            // 安全防重名覆盖机制
            QFileInfo fi(targetPath);
            QString base = fi.completeBaseName();
            QString ext = fi.suffix();
            int counter = 1;
            while (QFile::exists(targetPath)) {
                targetPath = dir.absoluteFilePath(QString("%1(%2).%3").arg(base).arg(counter++).arg(ext));
            }
            QFile file(targetPath);
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
                itemsCreated++;
            }
        } else {
            // 文件夹防重名覆盖
            int counter = 1;
            QString baseName = renderedName;
            while (QDir(targetPath).exists()) {
                targetPath = dir.absoluteFilePath(QString("%1(%2)").arg(baseName).arg(counter++));
            }
            if (QDir().mkpath(targetPath)) {
                itemsCreated++;
            }
        }
    }

    QString msg = QString("成功创建 %1 个项目").arg(itemsCreated);
    ToolTipOverlay::instance()->showText(QCursor::pos(), msg, 2000, Style::SuccessGreen);
    accept();
}

} // namespace ArcMeta
