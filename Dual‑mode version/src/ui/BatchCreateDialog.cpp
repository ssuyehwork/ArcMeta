#include "BatchCreateDialog.h"
#include "ToolTipOverlay.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include "../core/AppConfig.h"
#include "../meta/MetadataManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace ArcMeta {

BatchCreateDialog::BatchCreateDialog(const QString& currentDirectory, bool isMemoryMode, QWidget* parent)
    : FramelessDialog("批量创建 - ArcMeta", parent), m_currentDir(currentDirectory), m_isMemoryMode(isMemoryMode) {
    resize(550, 420);
    initContent();
    applyTheme();

    // 扫描并填充 m_libraryCombo 数据
    if (m_libraryCombo) {
        for (const QFileInfo& drive : QDir::drives()) {
            QString letter = drive.absolutePath().left(1).toUpper();
            std::wstring volSerial = MetadataManager::getVolumeSerialNumber(drive.absolutePath().toStdWString());
            std::wstring managedW = MetadataManager::getManagedLibraryPath(volSerial, letter);
            QString managedPath = QString::fromStdWString(managedW);
            if (managedPath.isEmpty()) {
                QString defaultRel = QDir::toNativeSeparators(QString("%1:/ArcMeta.Library_%2").arg(letter).arg(letter));
                if (QFileInfo::exists(defaultRel)) {
                    managedPath = defaultRel;
                }
            }
            if (!managedPath.isEmpty() && QDir(managedPath).exists()) {
                m_libraryCombo->addItem(QString("ArcMeta.Library_%1").arg(letter), managedPath);
            }
        }
        QString lastLibPath = AppConfig::instance().getValue("BatchCreate/LastLibraryPath").toString();
        if (!lastLibPath.isEmpty()) {
            int idx = m_libraryCombo->findData(lastLibPath);
            if (idx != -1) m_libraryCombo->setCurrentIndex(idx);
        }
    }

    // 1. 初始化自动保存防抖定时器
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &BatchCreateDialog::doAutoSave);

    // 2. 还原上次配置（类型、后缀名、数量）
    int lastType = AppConfig::instance().getValue("BatchCreate/LastType", 0).toInt();
    QString lastSuffix = AppConfig::instance().getValue("BatchCreate/LastSuffix", "md").toString();
    // 自动清洗点号，确保输入框中不含点号
    while (lastSuffix.startsWith(".")) {
        lastSuffix.remove(0, 1);
    }
    int lastCount = AppConfig::instance().getValue("BatchCreate/LastCount", 5).toInt();

    m_typeCombo->setCurrentIndex(lastType);
    m_suffixEdit->setText(lastSuffix);
    m_countSpin->setValue(lastCount);

    // 显式触发一次后缀名和点号的启用状态同步，避免由于 QComboBox 索引未实质改变导致状态错步
    m_suffixEdit->setEnabled(lastType == 1);

    // 3. 还原上次命名规则管道
    QString lastRules = AppConfig::instance().getValue("BatchCreate/LastRules").toString();
    if (!lastRules.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(lastRules.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const auto& v : arr) {
                onInsertRowAfter(nullptr);
                QJsonObject obj = v.toObject();
                RenameRule rule;
                QString typeStr = obj["type"].toString();
                if (typeStr == "Text") rule.type = RenameComponentType::Text;
                else if (typeStr == "Sequence") rule.type = RenameComponentType::Sequence;
                else if (typeStr == "OriginalName") rule.type = RenameComponentType::OriginalName;
                else if (typeStr == "Date") rule.type = RenameComponentType::Date;
                
                rule.value = obj["value"].toString();
                rule.start = obj["start"].toInt();
                rule.padding = obj["padding"].toInt();
                m_ruleRows.last()->setRule(rule);
            }
        }
    }

    if (m_ruleRows.isEmpty()) {
        onInsertRowAfter(nullptr); 
    }

    // 绑定控件改变自动保存
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BatchCreateDialog::scheduleAutoSave);
    connect(m_suffixEdit, &QLineEdit::textChanged, this, &BatchCreateDialog::scheduleAutoSave);
    connect(m_countSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BatchCreateDialog::scheduleAutoSave);
    if (m_libraryCombo) {
        connect(m_libraryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BatchCreateDialog::scheduleAutoSave);
    }
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BatchCreateDialog::updateLibraryControlState);

    updateLibraryControlState();
}

void BatchCreateDialog::initContent() {
    QWidget* content = getContentArea();
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(12);

    // 顶部设置行
    QHBoxLayout* topSettingsL = new QHBoxLayout();
    topSettingsL->setContentsMargins(0, 0, 0, 0);
    topSettingsL->setSpacing(0); // 禁用默认间距，采用精确控制

    // ===== 1. 类型组 =====
    QHBoxLayout* typeGroupL = new QHBoxLayout();
    typeGroupL->setSpacing(4); // 组内标签与控件间距设为 4px（紧密靠拢）
    QLabel* typeLabel = new QLabel("类型:", this);
    typeLabel->setStyleSheet("color: #BBB; font-weight: bold;");
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem("文件夹", 0);
    m_typeCombo->addItem("文件", 1);
    m_typeCombo->setFixedHeight(25);
    m_typeCombo->setFixedWidth(90);
    typeGroupL->addWidget(typeLabel);
    typeGroupL->addWidget(m_typeCombo);

    // ===== 2. 后缀名组 =====
    QHBoxLayout* suffixGroupL = new QHBoxLayout();
    suffixGroupL->setSpacing(4); // 组内间距 4px
    QLabel* suffixLabel = new QLabel("后缀名:", this);
    suffixLabel->setStyleSheet("color: #BBB; font-weight: bold;");
    
    m_suffixEdit = new QLineEdit(this);
    m_suffixEdit->setPlaceholderText("md");
    m_suffixEdit->setText("md");
    m_suffixEdit->setFixedHeight(25);
    m_suffixEdit->setFixedWidth(80);
    m_suffixEdit->setEnabled(false); // 默认初始与“文件夹”选择同步禁用

    suffixGroupL->addWidget(suffixLabel);
    suffixGroupL->addWidget(m_suffixEdit);

    // ===== 3. 数量组 =====
    QHBoxLayout* countGroupL = new QHBoxLayout();
    countGroupL->setSpacing(4); // 组内间距 4px
    QLabel* countLabel = new QLabel("数量:", this);
    countLabel->setStyleSheet("color: #BBB; font-weight: bold;");
    m_countSpin = new QSpinBox(this);
    m_countSpin->setRange(1, 10000);
    m_countSpin->setValue(5);
    m_countSpin->setFixedHeight(25);
    m_countSpin->setFixedWidth(75);
    countGroupL->addWidget(countLabel);
    countGroupL->addWidget(m_countSpin);

    // ===== 4. 资源库组 (内存模式受控选择) =====
    m_libraryGroupWidget = new QWidget(this);
    QHBoxLayout* libraryGroupL = new QHBoxLayout(m_libraryGroupWidget);
    libraryGroupL->setContentsMargins(0, 0, 0, 0);
    libraryGroupL->setSpacing(4);
    QLabel* libraryLabel = new QLabel("资源库:", m_libraryGroupWidget);
    libraryLabel->setStyleSheet("color: #BBB; font-weight: bold;");
    m_libraryCombo = new QComboBox(m_libraryGroupWidget);
    m_libraryCombo->setFixedHeight(25);
    m_libraryCombo->setFixedWidth(140);
    libraryGroupL->addWidget(libraryLabel);
    libraryGroupL->addWidget(m_libraryCombo);

    // 组合至主布局，组间间距保持 18px（清晰视觉隔离）
    topSettingsL->addLayout(typeGroupL);
    topSettingsL->addSpacing(18); // 组间距 18px
    topSettingsL->addLayout(suffixGroupL);
    topSettingsL->addSpacing(18); // 组间距 18px
    topSettingsL->addLayout(countGroupL);
    topSettingsL->addSpacing(18); // 组间距 18px
    topSettingsL->addWidget(m_libraryGroupWidget);
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

    m_btnOk = new QPushButton("开始创建", this);
    m_btnOk->setCursor(Qt::PointingHandCursor);
    m_btnOk->setFixedSize(100, 28);
    m_btnOk->setStyleSheet("QPushButton { background: #007ACC; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background: #1C97EA; } QPushButton:disabled { background: #333333; color: #666666; }");
    bottomL->addWidget(m_btnOk);
    layout->addLayout(bottomL);

    // 触发联动：类型切换控制后缀可用性
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index) {
        m_suffixEdit->setEnabled(index == 1);
    });

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnOk, &QPushButton::clicked, this, &BatchCreateDialog::onExecute);
}

void BatchCreateDialog::onInsertRowAfter(CreateRuleRow* targetRow) {
    CreateRuleRow* newRow = new CreateRuleRow(m_rulesContainer);

    int insertIndex = m_ruleRows.size(); // 默认插入末尾
    if (targetRow) {
        int targetIdx = m_ruleRows.indexOf(targetRow);
        if (targetIdx != -1) {
            insertIndex = targetIdx + 1; // 精准计算目标行正下方的索引
        }
    }

    // 在布局与列表中指定位置精准插入 Widget，原本下方的行自动顺延下沉
    m_rulesLayout->insertWidget(insertIndex, newRow);
    m_ruleRows.insert(insertIndex, newRow);

    // 绑定信号：点击该新行的 + 号时，在该新行正下方再次插入
    connect(newRow, &CreateRuleRow::addRequested, this, [this, newRow]() {
        onInsertRowAfter(newRow);
    });
    connect(newRow, &CreateRuleRow::removeRequested, [this, newRow]() {
        if (m_ruleRows.size() > 1) {
            m_ruleRows.removeOne(newRow);
            newRow->deleteLater();
            scheduleAutoSave();
        }
    });
    connect(newRow, &CreateRuleRow::changed, this, &BatchCreateDialog::scheduleAutoSave);
    scheduleAutoSave();
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

QString BatchCreateDialog::renderOne(int index, const std::vector<RenameRule>& rules) const {
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

void BatchCreateDialog::scheduleAutoSave() {
    if (m_autoSaveTimer) {
        m_autoSaveTimer->start(300);
    }
}

void BatchCreateDialog::updateLibraryControlState() {
    if (!m_isMemoryMode) {
        if (m_libraryGroupWidget) m_libraryGroupWidget->setVisible(false);
        if (m_btnOk) m_btnOk->setEnabled(true);
        return;
    }

    bool isFileMode = (m_typeCombo->currentData().toInt() == 1);
    if (!isFileMode) {
        if (m_libraryGroupWidget) m_libraryGroupWidget->setVisible(false);
        if (m_btnOk) m_btnOk->setEnabled(true);
    } else {
        if (m_libraryGroupWidget) m_libraryGroupWidget->setVisible(true);
        bool hasValidLibrary = (m_libraryCombo && m_libraryCombo->count() > 0);
        if (m_btnOk) m_btnOk->setEnabled(hasValidLibrary);
    }
}

QString BatchCreateDialog::selectedLibraryPath() const {
    if (!m_isMemoryMode || !m_libraryCombo) return QString();
    return m_libraryCombo->currentData().toString();
}

void BatchCreateDialog::doAutoSave() {
    AppConfig::instance().setValue("BatchCreate/LastType", m_typeCombo->currentIndex());
    AppConfig::instance().setValue("BatchCreate/LastSuffix", m_suffixEdit->text());
    AppConfig::instance().setValue("BatchCreate/LastCount", m_countSpin->value());
    AppConfig::instance().setValue("BatchCreate/LastLibraryPath", selectedLibraryPath());

    QJsonArray arr;
    for (auto* row : m_ruleRows) {
        RenameRule rule = row->getRule();
        QJsonObject obj;
        QString typeStr;
        switch (rule.type) {
            case RenameComponentType::Text: typeStr = "Text"; break;
            case RenameComponentType::Sequence: typeStr = "Sequence"; break;
            case RenameComponentType::OriginalName: typeStr = "OriginalName"; break;
            case RenameComponentType::Date: typeStr = "Date"; break;
            default: typeStr = "Unknown";
        }
        obj["type"] = typeStr;
        obj["value"] = rule.value;
        obj["start"] = rule.start;
        obj["padding"] = rule.padding;
        arr.append(obj);
    }
    AppConfig::instance().setValue("BatchCreate/LastRules", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

bool BatchCreateDialog::isFile() const {
    return m_typeCombo->currentData().toInt() == 1;
}

QString BatchCreateDialog::fileSuffix() const {
    return m_suffixEdit->text();
}

QStringList BatchCreateDialog::renderAllNames() const {
    QStringList names;
    int count = m_countSpin->value();
    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) rules.push_back(row->getRule());

    for (int i = 0; i < count; ++i) {
        QString n = renderOne(i, rules);
        if (n.isEmpty()) n = QString("NewItem_%1").arg(i + 1);
        names << n;
    }
    return names;
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
    
    // 自动清洗点号防呆：无论用户输入 md 还是 .md，统一清洗为 .md
    QString rawSuffix = m_suffixEdit->text().trimmed();
    while (rawSuffix.startsWith(".")) {
        rawSuffix.remove(0, 1); // 清除前导点号
    }
    QString finalSuffix = isFile ? ("." + (rawSuffix.isEmpty() ? "md" : rawSuffix)) : "";

    if (m_isMemoryMode) {
        // 🚨 内存模式下：严禁在此执行 QFile/QDir 本地物理落盘！
        // 仅保留序列号递增、doAutoSave() 和 accept()，将物理写入彻底交由 ContentPanel 处理。
    } else {
        QDir dir(m_currentDir);
        int itemsCreated = 0;

        for (int i = 0; i < createCount; ++i) {
            QString renderedName = renderOne(i, rules);
            if (renderedName.isEmpty()) {
                renderedName = QString("NewItem_%1").arg(i + 1);
            }

            if (isFile) {
                renderedName += finalSuffix;
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
    }

    // 按照用户最新要求：成功创建后，自动递增累加序列数字的起始值，并落盘保存
    for (auto* row : m_ruleRows) {
        RenameRule r = row->getRule();
        if (r.type == RenameComponentType::Sequence) {
            r.start += createCount; // 起始值自动递增累加本次创建的数量
            row->setRule(r);        // 重新应用使 UI 与内部值同步
        }
    }

    doAutoSave();
    accept();
}

} // namespace ArcMeta
