# 批量创建功能规则化升级与重命名管道引入 —— Modification_Plan-53.md

> 状态：待批准执行（尚未获得用户"批准执行"指令）

## 1. 任务背景
目前，应用的批量创建功能 (`BatchCreateDialog`) 仅支持用户每行输入一个固定的名称，无法像批量重命名那样通过自定义固定文本、序列数字、日期以及保留选中文件原名等管道规则来动态、自适应地批量创建文件夹或文件。为了增强此特性并提高可用性，我们决定对批量创建对话框进行规则化升级，引入重命名类似的管道规则构造器。

## 2. 问题定位
- **UI 单一**：目前的 `BatchCreateDialog` 仅含有一个 `QPlainTextEdit` 输入框，缺少规则添加、选择及参数输入面板。
- **算法单一**：`BatchCreateDialog::onExecute` 仅支持简单的换行符分割和固定文本后缀判断，缺乏动态规则渲染机制。
- **解决方案**：升级 `BatchCreateDialog` 的 UI 交互，提供“创建类型（文件/文件夹）”、“创建数量 $K$”的配置，并复用或新增一个规则构建界面，根据用户添加的规则（Text、Sequence、Date、OriginalName）管道式遍历 $0 \le i < K$ 渲染得到新路径并物理写入，达成用户预期。

## 3. 强制对照表

| 编号 | 用户原话 / 我的理解 | 方案对应点 | 是否一致 |
|------|---------------------|------------|----------|
| 1    | 固定文本（Text）：自定义并输入任意的固定文本（对应用户原话：“固定文本（Text）：用户可以自定义并输入任意的固定文本（如 MyProject_），这是完全可以随意自定义名称的。”） | 在 `BatchCreateDialog` 中集成规则列表，渲染 `RenameComponentType::Text` 的值 | ✅ |
| 2    | 序列数字（Sequence）：支持自定义起始编号、步长、补零位数（对应用户原话：“序列数字（Sequence）：支持自定义起始编号（如 1、100）、步长以及补零位数（如 001、0001）。”） | 集成 `RenameComponentType::Sequence` 渲染规则，计算 `start + i * step` 并根据 padding 补零 | ✅ |
| 3    | 原文件名（OriginalName）：直接保留并拼接文件原有的基础文件名（对应用户原话：“原文件名（OriginalName）：直接保留并拼接文件原有的基础文件名（baseName()）。”） | 集成 `RenameComponentType::OriginalName`，若用户传入了当前选中的文件名列表，则按选中文件的 baseName 批量生成，否则回退为 placeholder | ✅ |
| 4    | 日期格式（Date）：支持诸如 yyyyMMdd、yyyy-MM-dd 等动态日期后缀（对应用户原话：“日期格式（Date）：支持诸如 yyyyMMdd、yyyy-MM-dd 等动态日期后缀。”） | 集成 `RenameComponentType::Date`，获取当前时间并根据选定的格式（如 `yyyy-MM-dd`）渲染 | ✅ |

## 4. 详细解决方案

本部分由执行者 AI 角色直接读取，并严格、机械地按照给出的 Git merge diff 代码块进行物理替换或创建，不得做任何自由发挥或脑补改动。

由于批量重命名已经具有极为成熟的 `RuleRow`（代表规则行）及 `RenameRule` 结构，为了实现高内聚，本方案直接通过在 `BatchCreateDialog` 中创建规则列表，并直接使用/继承 `RuleRow` 的交互，提供极为完美的图形化配置界面：
- 用户可以点击 “+” 或 “-” 动态增加多行规则组件。
- 配置“要创建的数量” $K$。
- 选择创建的是文件夹还是文件（如果是文件，还可输入自定义后缀名）。

### 4.1 物理重构 `BatchCreateDialog.h`

<<<<<<< SEARCH
namespace ArcMeta {

class BatchCreateDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit BatchCreateDialog(const QString& currentDirectory, QWidget* parent = nullptr);
    ~BatchCreateDialog() override = default;

private:
    void initContent();
    void onExecute();

    QString m_currentDir;
    QPlainTextEdit* m_textEdit = nullptr;
};

} // namespace ArcMeta
=======
#include "RuleRow.h"
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>

namespace ArcMeta {

class BatchCreateDialog : public FramelessDialog {
    Q_OBJECT
public:
    explicit BatchCreateDialog(const QString& currentDirectory, QWidget* parent = nullptr);
    ~BatchCreateDialog() override = default;

private:
    void initContent();
    void onExecute();
    void onAddRow();
    void applyTheme();
    QString renderOne(int index, const std::vector<RenameRule>& rules);

    QString m_currentDir;
    
    QSpinBox* m_countSpin = nullptr;
    QComboBox* m_typeCombo = nullptr; // 文件夹 / 文件
    QLineEdit* m_suffixEdit = nullptr; // 后缀名 (.txt 等)
    
    QWidget* m_rulesContainer = nullptr;
    QVBoxLayout* m_rulesLayout = nullptr;
    QList<RuleRow*> m_ruleRows;
};

} // namespace ArcMeta
>>>>>>> REPLACE

### 4.2 物理重构 `BatchCreateDialog.cpp`

完全重写 `BatchCreateDialog.cpp`，提供图形化管道规则构建界面。

<<<<<<< SEARCH
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

namespace ArcMeta {

BatchCreateDialog::BatchCreateDialog(const QString& currentDirectory, QWidget* parent)
    : FramelessDialog("批量创建", parent), m_currentDir(currentDirectory) {
    setFixedWidth(450);
    initContent();
}

void BatchCreateDialog::initContent() {
    QWidget* content = getContentArea();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(12);

    QLabel* hintLabel = new QLabel("请输入要创建的项目名称（每行一个）。若带有后缀（如 .txt）则创建文件，否则创建文件夹：", this);
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet("color: #AAAAAA; font-size: 12px;");
    layout->addWidget(hintLabel);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setPlaceholderText("新建文件夹1\n新建文件2.txt\n文档3.md");
    m_textEdit->setFixedHeight(180);
    m_textEdit->setStyleSheet(
        "QPlainTextEdit { "
        "  background-color: #252526; "
        "  color: #F1F1F1; "
        "  border: 1px solid #3E3E42; "
        "  border-radius: 4px; "
        "  padding: 8px; "
        "  font-family: 'Segoe UI', Microsoft YaHei; "
        "  font-size: 12px; "
        "}"
        "QPlainTextEdit:focus { border: 1px solid #007ACC; }"
    );
    layout->addWidget(m_textEdit);

    QHBoxLayout* bottomL = new QHBoxLayout();
    bottomL->addStretch();

    QPushButton* btnCancel = new QPushButton("取消", this);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setStyleSheet(
        "QPushButton { "
        "  background-color: #3E3E42; "
        "  color: #F1F1F1; "
        "  border: 1px solid #555555; "
        "  border-radius: 4px; "
        "  padding: 6px 16px; "
        "  font-size: 12px; "
        "}"
        "QPushButton:hover { background-color: #4E4E52; }"
    );
    bottomL->addWidget(btnCancel);

    QPushButton* btnOk = new QPushButton("创建", this);
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setStyleSheet(
        "QPushButton { "
        "  background-color: #007ACC; "
        "  color: #FFFFFF; "
        "  border: none; "
        "  border-radius: 4px; "
        "  padding: 6px 20px; "
        "  font-weight: bold; "
        "  font-size: 12px; "
        "}"
        "QPushButton:hover { background-color: #1C97EA; }"
    );
    bottomL->addWidget(btnOk);
    layout->addLayout(bottomL);

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &BatchCreateDialog::onExecute);
}

void BatchCreateDialog::onExecute() {
    QString text = m_textEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "输入内容不能为空！", 1500, QColor("#E81123"));
        return;
    }

    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    int folderCreated = 0;
    int fileCreated = 0;

    QDir dir(m_currentDir);

    for (QString line : lines) {
        QString name = line.trimmed();
        if (name.isEmpty()) continue;

        // 识别是否具有后缀名
        bool hasSuffix = false;
        int dotIdx = name.lastIndexOf('.');
        if (dotIdx > 0 && dotIdx < name.length() - 1) {
            hasSuffix = true;
        }

        QString targetPath = dir.absoluteFilePath(name);

        if (hasSuffix) {
            // 安全避让生成 physical 空白文件
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
                fileCreated++;
            }
        } else {
            // 安全避让生成 physical 子目录
            int counter = 1;
            QString baseName = name;
            while (QDir(targetPath).exists()) {
                targetPath = dir.absoluteFilePath(QString("%1(%2)").arg(baseName).arg(counter++));
            }
            if (QDir().mkpath(targetPath)) {
                folderCreated++;
            }
        }
    }

    QString finishMsg = QString("批量创建成功：文件夹 %1，文件 %2").arg(folderCreated).arg(fileCreated);
    ToolTipOverlay::instance()->showText(QCursor::pos(), finishMsg, 2000, Style::SuccessGreen);
    accept();
}

} // namespace ArcMeta
=======
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
>>>>>>> REPLACE
