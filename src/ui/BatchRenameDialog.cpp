#include "BatchRenameDialog.h"
#include "BatchRenamePreviewDialog.h"
#include "RuleRow.h"
#include "UiHelper.h"
#include "DiskBatchRenameService.h"
#include "PresetManager.h"
#include "UndoToastOverlay.h"
#include "../meta/BatchRenameEngine.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include <QHeaderView>
#include "FramelessFileDialog.h"
#include "FramelessDialog.h"
#include "ToolTipOverlay.h"
#include <QFileInfo>
#include <QTimer>
#include <QDir>
#include <QLabel>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTableWidgetItem>
#include <QRadioButton>
#include <QScrollArea>
#include "../core/AppConfig.h"
#include "../core/UndoManager.h"
#include "../core/BasicCommands.h"

namespace ArcMeta {

BatchRenameDialog::BatchRenameDialog(const std::vector<std::wstring>& originalPaths, bool isMirrorSource, QWidget* parent)
    : FramelessDialog("批量重命名 - ArcMeta", parent), m_originalPaths(originalPaths), m_isMirrorSource(isMirrorSource) {
    resize(850, 600); // 2026-04-11 按照用户要求：给予窗口更多弹性空间，提高初始显示质量
    initContent();
    applyTheme();

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setSingleShot(true);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &BatchRenameDialog::doAutoSave);

    // 还原上次规则
    QString lastRules = AppConfig::instance().getValue("LastBatchRenameRules").toString();
    if (!lastRules.isEmpty()) {
        auto rules = PresetManager::deserializeRules(lastRules);
        for (const auto& rule : rules) {
            onAddRow();
            m_ruleRows.last()->setRule(rule);
        }
    }

    if (m_ruleRows.isEmpty()) {
        onAddRow(); 
    }
}

void BatchRenameDialog::initContent() {
    QHBoxLayout* rootL = new QHBoxLayout(m_contentArea);
    rootL->setContentsMargins(20, 20, 20, 20);
    rootL->setSpacing(20);

    // ================= 左侧：配置区 =================
    QVBoxLayout* configL = new QVBoxLayout();
    configL->setSpacing(15); // 物理还原：恢复至原有的舒适组间距

    // 1. 预设区
    QGroupBox* presetGroup = new QGroupBox("预设", this);
    QHBoxLayout* presetL = new QHBoxLayout(presetGroup);
    presetL->setContentsMargins(10, 5, 10, 5);
    presetL->setSpacing(5); // 2026-07-xx 按照用户要求：间距统一保持 5px

    m_presetCombo = new QComboBox(presetGroup);
    m_presetCombo->addItem("默认设置");
    m_presetCombo->setFixedHeight(25); 
    presetL->addWidget(m_presetCombo, 1);

    // 删除预设按钮 "×"
    m_btnDeletePreset = new QPushButton("×", presetGroup);
    m_btnDeletePreset->setFixedSize(20, 20);
    m_btnDeletePreset->setCursor(Qt::PointingHandCursor);
    m_btnDeletePreset->setStyleSheet(
        "QPushButton { background: #3E3E42; color: white; border: none; border-radius: 4px; font-size: 14px; font-weight: bold; }" // 2026-07-xx 按照用户要求：持续显示灰色高亮
        "QPushButton:hover { background: #4E4E52; }"
    );
    presetL->addWidget(m_btnDeletePreset);

    // 导入/导出按钮
    m_btnImportPreset = new QPushButton("导入...", presetGroup);
    m_btnExportPreset = new QPushButton("导出...", presetGroup);
    m_btnImportPreset->setFixedHeight(25);
    m_btnExportPreset->setFixedHeight(25);
    m_btnImportPreset->setFixedWidth(80);
    m_btnExportPreset->setFixedWidth(80);
    
    presetL->addWidget(m_btnImportPreset);
    presetL->addWidget(m_btnExportPreset);
    configL->addWidget(presetGroup);

    // 2. 目标文件夹
    QGroupBox* targetGroup = new QGroupBox("目标文件夹", this);
    QVBoxLayout* targetL = new QVBoxLayout(targetGroup);
    m_rbRename = new QRadioButton("在同一文件夹中重命名", targetGroup);
    m_rbMove = new QRadioButton("移动到其他文件夹", targetGroup);
    m_rbCopy = new QRadioButton("复制到其他文件夹", targetGroup);
    m_rbRename->setChecked(true);
    targetL->addWidget(m_rbRename);
    targetL->addWidget(m_rbMove);
    targetL->addWidget(m_rbCopy);

    if (m_isMirrorSource) {
        m_rbMove->setEnabled(false);
        m_rbCopy->setEnabled(false);
        m_rbRename->setChecked(true);
    }

    QHBoxLayout* pathL = new QHBoxLayout();
    m_targetPathEdit = new QLineEdit(targetGroup);
    m_targetPathEdit->setPlaceholderText("选择目标文件夹...");
    m_targetPathEdit->setFixedHeight(25);
    m_targetPathEdit->setEnabled(false);
    m_btnBrowse = new QPushButton("浏览...", targetGroup);
    m_btnBrowse->setFixedSize(80, 25);
    m_btnBrowse->setEnabled(false);
    pathL->addWidget(m_targetPathEdit);
    pathL->addWidget(m_btnBrowse);
    targetL->addLayout(pathL);

    if (m_isMirrorSource) {
        m_targetPathEdit->hide();
        m_btnBrowse->hide();
    }
    configL->addWidget(targetGroup);

    // 3. 新文件名 (规则构造器)
    QGroupBox* rulesGroup = new QGroupBox("新文件名", this);
    // 物理锁定：仅对该组件进行局部标题及内间距修正，确保不影响全局
    rulesGroup->setStyleSheet("QGroupBox { padding-top: 15px; margin-top: 5px; } QGroupBox::title { top: -2px; left: 8px; }");
    
    QVBoxLayout* rulesGroupL = new QVBoxLayout(rulesGroup);
    rulesGroupL->setContentsMargins(4, 2, 4, 4); // 顶部给予 2px 呼吸感
    rulesGroupL->setSpacing(0);
    
    QScrollArea* scroll = new QScrollArea(rulesGroup);
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignTop); // 物理强行对齐：解决由于容器拉伸导致的首行规则下坠问题
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background: transparent; border: none; padding: 0px; margin: 0px;");
    
    m_rulesContainer = new QWidget(scroll);
    // 2026-04-11 按照用户要求：修复规则行下坠 Bug。容器高度必须自适应内容向上收缩，
    // 而不是撑满 ScrollArea 视口，因此 Policy 必须设置为 Maximum
    m_rulesContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    m_rulesLayout = new QVBoxLayout(m_rulesContainer);
    m_rulesLayout->setContentsMargins(0, 0, 0, 0);
    m_rulesLayout->setSpacing(2);
    
    scroll->setWidget(m_rulesContainer);
    rulesGroupL->addWidget(scroll);
    configL->addWidget(rulesGroup, 2); 

    rootL->addLayout(configL, 1);

    // ================= 右侧：动作按钮列 =================
    QVBoxLayout* actionL = new QVBoxLayout();
    actionL->setSpacing(10);

    m_btnExecute = new QPushButton("重命名", this);
    m_btnCancel = new QPushButton("取消", this);
    m_btnPreview = new QPushButton("预览", this);

    // 2026-04-11 按照用户要求：将右侧操作按钮圆角从 12px 修正为规范的 6px
    auto styleBtn = [](QPushButton* btn, bool primary = false) {
        btn->setMinimumSize(110, 32);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        if (primary) {
            btn->setStyleSheet("QPushButton { background: #444; color: #EEE; border: 1px solid #666; border-radius: 6px; } QPushButton:hover { background: #555; }");
        } else {
            btn->setStyleSheet("QPushButton { background: transparent; color: #BBB; border: 1px solid #444; border-radius: 6px; } QPushButton:hover { background: #3E3E42; }");
        }
    };

    styleBtn(m_btnExecute, true);
    styleBtn(m_btnCancel);
    styleBtn(m_btnPreview);

    actionL->addWidget(m_btnExecute);
    actionL->addWidget(m_btnCancel);
    actionL->addSpacing(15);
    actionL->addWidget(m_btnPreview);
    actionL->addStretch();

    rootL->addLayout(actionL);

    // Connections
    connect(m_rbRename, &QRadioButton::toggled, [this](bool checked){
        m_targetPathEdit->setEnabled(!checked);
        m_btnBrowse->setEnabled(!checked);
    });
    connect(m_btnBrowse, &QPushButton::clicked, this, &BatchRenameDialog::onBrowseTarget);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnExecute, &QPushButton::clicked, this, &BatchRenameDialog::onExecute);
    connect(m_btnPreview, &QPushButton::clicked, this, &BatchRenameDialog::onPreview);
    connect(m_btnImportPreset, &QPushButton::clicked, this, &BatchRenameDialog::onImportPreset);
    connect(m_btnExportPreset, &QPushButton::clicked, this, &BatchRenameDialog::onExportPreset);
    connect(m_btnDeletePreset, &QPushButton::clicked, this, &BatchRenameDialog::onDeleteCurrentPreset);
}

void BatchRenameDialog::applyTheme() {
    // 2026-07-xx 按照用户要求：升级下拉框 UI，圆角设计 + 实心三角形箭头
    static const QString arrowPath = UiHelper::getSvgTempFilePath("dropdown_triangle", QColor("#AAAAAA"));

    setStyleSheet(QString(
        "QDialog { background-color: #1E1E1E; color: #BBB; }"
        "QGroupBox { border: 1px solid #333; border-radius: 4px; margin-top: 10px; font-weight: bold; font-size: 11px; color: #888; }" // 还原全局 10px 边距
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
        "QLineEdit { background: #252526; border: 1px solid #444; border-radius: 4px; padding: 2px 5px; color: #EEE; }"
        "QRadioButton { color: #BBB; spacing: 5px; }"
        "QPushButton { background: #333; color: #EEE; border-radius: 4px; }"
        "QComboBox { background: #252526; border: 1px solid #444; border-radius: 4px; padding: 1px 4px; color: #EEE; }"
        "QComboBox::drop-down { border: none; width: 24px; }"
        "QComboBox::down-arrow { image: url(%1); width: 12px; height: 12px; }"
        "QComboBox QAbstractItemView { background-color: #2D2D2D; border: 1px solid #444; selection-background-color: #3E3E42; selection-color: white; color: #EEE; outline: 0; }"
        "QComboBox QAbstractItemView::item { height: 22px; padding: 2px; }" 
    ).arg(arrowPath));
}

void BatchRenameDialog::onAddRow() {
    RuleRow* row = new RuleRow(m_rulesContainer);
    // 2026-04-11 按照用户要求：已移除 Stretch，直接追加至末尾，规则行始终自顶向下紧凑排列
    m_rulesLayout->addWidget(row);
    m_ruleRows.append(row);
    
    connect(row, &RuleRow::changed, this, &BatchRenameDialog::updatePreview);
    connect(row, &RuleRow::changed, this, &BatchRenameDialog::scheduleAutoSave);
    connect(row, &RuleRow::addRequested, this, &BatchRenameDialog::onAddRow);
    connect(row, &RuleRow::addRequested, this, &BatchRenameDialog::scheduleAutoSave);
    connect(row, &RuleRow::removeRequested, [this, row]() {
        if (m_ruleRows.size() > 1) {
            m_ruleRows.removeOne(row);
            row->deleteLater();
            updatePreview();
            scheduleAutoSave();
        }
    });
}

void BatchRenameDialog::updatePreview() {
    // 逻辑占位，后续可在此触发底部的简单文本摘要预防
}

void BatchRenameDialog::onImportPreset() {
    QString path = FramelessFileDialog::getOpenFileName(this, "导入重命名预设", "", "JSON Files (*.json)");
    if (path.isEmpty()) return;

    auto rules = PresetManager::importFromFile(path);
    if (!rules.empty()) {
        // 清空现有规则
        while (m_ruleRows.size() > 0) {
            auto* row = m_ruleRows.takeAt(0);
            row->deleteLater();
        }

        for (const auto& rule : rules) {
            onAddRow();
            m_ruleRows.last()->setRule(rule);
        }
        scheduleAutoSave();
    }
}

void BatchRenameDialog::onExportPreset() {
    QString path = FramelessFileDialog::getSaveFileName(this, "导出重命名预设", "Preset.json", "JSON Files (*.json)");
    if (path.isEmpty()) return;

    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) {
        rules.push_back(row->getRule());
    }

    if (PresetManager::exportToFile(path, rules)) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "预设导出成功", 1500, QColor("#2ecc71"));
    }
}

void BatchRenameDialog::onDeleteCurrentPreset() {
    if (m_presetCombo->count() > 0) {
        int index = m_presetCombo->currentIndex();
        m_presetCombo->removeItem(index);
        ToolTipOverlay::instance()->showText(QCursor::pos(), "预设已删除", 1000);
    }
}

void BatchRenameDialog::scheduleAutoSave() {
    if (m_autoSaveTimer) m_autoSaveTimer->start(500);
}

void BatchRenameDialog::doAutoSave() {
    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) {
        rules.push_back(row->getRule());
    }
    QString compactJson = PresetManager::serializeRules(rules);
    AppConfig::instance().setValue("LastBatchRenameRules", compactJson);
}

void BatchRenameDialog::onPreview() {
    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) rules.push_back(row->getRule());
    
    auto newNames = BatchRenameEngine::instance().preview(m_originalPaths, rules);
    
    BatchRenamePreviewDialog dlg(this);
    dlg.setPreviewData(m_originalPaths, newNames);
    dlg.exec();
}

void BatchRenameDialog::onBrowseTarget() {
    QString dir = FramelessFileDialog::getExistingDirectory(this, "选择目标文件夹");
    if (!dir.isEmpty()) {
        m_targetPathEdit->setText(dir);
    }
}

void BatchRenameDialog::onExecute() {
    std::vector<RenameRule> rules;
    for (auto* row : m_ruleRows) rules.push_back(row->getRule());
    
    auto newNames = BatchRenameEngine::instance().preview(m_originalPaths, rules);
    if (newNames.empty()) return;

    // 禁用执行按钮以防重复点击
    m_btnExecute->setEnabled(false);

    // 记录本次执行所操作的旧路径、旧物理目标及新物理目标（为撤销快照提供完整物理对账依据）
    bool isCapsule = m_isMirrorSource;
    DiskOperationMode mode = DiskOperationMode::Rename;
    if (m_rbMove->isChecked()) mode = DiskOperationMode::Move;
    else if (m_rbCopy->isChecked()) mode = DiskOperationMode::Copy;

    QString targetDir = m_targetPathEdit->text();
    if (!isCapsule && mode != DiskOperationMode::Rename && targetDir.isEmpty()) {
        FramelessMessageBox::warning(this, "错误", "请先选择目标文件夹");
        m_btnExecute->setEnabled(true);
        return;
    }

    std::vector<std::wstring> oldPathsSnap = m_originalPaths;
    std::vector<std::wstring> newPathsSnap;
    newPathsSnap.reserve(m_originalPaths.size());

    for (size_t i = 0; i < m_originalPaths.size(); ++i) {
        QString oldPath = QString::fromStdWString(m_originalPaths[i]);
        QFileInfo oldInfo(oldPath);
        QString destDir = (mode == DiskOperationMode::Rename || isCapsule) ? oldInfo.absolutePath() : targetDir;
        QString newPathStr = QDir(destDir).absoluteFilePath(QString::fromStdWString(newNames[i]));
        newPathsSnap.push_back(QDir::toNativeSeparators(newPathStr).toStdWString());
    }

    QPointer<BatchRenameDialog> safeThis(this);
    auto onCompletedCallback = [safeThis, isCapsule, mode, oldPathsSnap, newPathsSnap](int successCount) {
        if (!safeThis) return;
        // 确保回到 UI 主线程
        safeThis->m_btnExecute->setEnabled(true);

        if (successCount > 0) {
            // 只有在存在实际成功记录时，才依据真实成功数自增序列号，杜绝跳号
            for (auto* row : safeThis->m_ruleRows) {
                RenameRule rule = row->getRule();
                if (rule.type == RenameComponentType::Sequence) {
                    rule.start = rule.start + successCount * rule.step;
                    row->setRule(rule);
                }
            }
            safeThis->doAutoSave();

            // 成功物理移动或重命名或复制后，向 UndoManager 推送一次完整的原子 BatchRenameCommand
            UndoManager::instance().pushCommand(std::make_unique<BatchRenameCommand>(isCapsule, mode, oldPathsSnap, newPathsSnap));
        }

        std::vector<RenameRule> currentRules;
        for (auto* row : safeThis->m_ruleRows) currentRules.push_back(row->getRule());
        auto finalNames = BatchRenameEngine::instance().preview(safeThis->m_originalPaths, currentRules);
        if (!finalNames.empty()) {
            safeThis->m_firstNewName = QString::fromStdWString(finalNames.front());
        }

        QWidget* mainWindowPtr = nullptr;
        QWidget* parentW = safeThis->parentWidget();
        while (parentW) {
            if (parentW->inherits("ArcMeta::MainWindow") || parentW->objectName() == "MainWindow") {
                mainWindowPtr = parentW;
                break;
            }
            parentW = parentW->parentWidget();
        }

        UndoToastOverlay::instance()->showToast(
            mainWindowPtr,
            QString("成功处理 %1 个项目").arg(successCount),
            [successCount]() {
                if (successCount > 0) {
                    UndoManager::instance().undo();
                }
            },
            5000
        );
        safeThis->accept();
    };

    DiskBatchRenameService::execute(m_originalPaths, newNames, mode, targetDir, onCompletedCallback);
}

} // namespace ArcMeta
