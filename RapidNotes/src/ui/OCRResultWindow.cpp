#include "OCRResultWindow.h"
#include "IconHelper.h"
#include "StringUtils.h"
#include "ToolTipOverlay.h"
#include "../core/ClipboardMonitor.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QSettings>
#include <QDebug>
#include <QRegularExpression>

OCRResultWindow::OCRResultWindow(const QImage& image, int contextId, QWidget* parent)
    : FramelessDialog("识别文本", parent), m_image(image), m_contextId(contextId)
{
    setObjectName("OCRResultWindow");
    loadWindowSettings();
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(600, 450);
    
    // 强制更新标题栏样式
    m_titleLabel->clear();
    auto* titleLayout = qobject_cast<QHBoxLayout*>(m_titleLabel->parentWidget()->layout());
    if (titleLayout) {
        // 尝试在标题文字前插个图标
        QLabel* iconLabel = new QLabel(m_titleLabel->parentWidget());
        iconLabel->setPixmap(IconHelper::getIcon("screenshot_ocr", "#007ACC", 20).pixmap(20, 20));
        titleLayout->insertWidget(0, iconLabel);
        
        m_titleLabel->setText("识别文本");
        m_titleLabel->setStyleSheet("font-weight: bold; color: #eee;");
    }
    
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(15, 10, 15, 15);
    layout->setSpacing(12);

    m_textEdit = new QPlainTextEdit();
    m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setPlaceholderText("正在识别中...");
    m_textEdit->setStyleSheet(R"(
        QPlainTextEdit {
            background-color: #1E1E1E;
            color: #D4D4D4;
            border: 1px solid #333333;
            border-radius: 4px;
            padding: 8px;
            font-family: 'Microsoft YaHei', sans-serif;
            font-size: 14px;
        }
        QScrollBar:vertical {
            border: none;
            background: #1E1E1E;
            width: 10px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #333;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )");
    layout->addWidget(m_textEdit);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(10);
    
    m_autoCopyCheck = new QCheckBox("下次自动复制");
    m_autoCopyCheck->setStyleSheet("QCheckBox { color: #999; font-size: 12px; } QCheckBox::indicator { width: 16px; height: 16px; }");
    
    QSettings settings("RapidNotes", "OCR");
    m_autoCopyCheck->setChecked(settings.value("autoCopy", false).toBool());
    
    // 立即保存设置，确保用户勾选后即刻生效
    connect(m_autoCopyCheck, &QCheckBox::toggled, [](bool checked){
        QSettings settings("RapidNotes", "OCR");
        settings.setValue("autoCopy", checked);
    });
    
    bottomLayout->addWidget(m_autoCopyCheck);

    bottomLayout->addStretch(1);

    QPushButton* toSimplifiedBtn = new QPushButton("转简体");
    toSimplifiedBtn->setFlat(true);
    toSimplifiedBtn->setStyleSheet("QPushButton { color: #1abc9c; border: none; font-size: 13px; } QPushButton:hover { color: #2ecc71; }");
    toSimplifiedBtn->setCursor(Qt::PointingHandCursor);
    toSimplifiedBtn->setProperty("tooltipText", "转换为简体中文");
    toSimplifiedBtn->installEventFilter(this);
    connect(toSimplifiedBtn, &QPushButton::clicked, [this]{
        m_textEdit->setPlainText(StringUtils::convertChineseVariant(m_textEdit->toPlainText(), true));
    });
    bottomLayout->addWidget(toSimplifiedBtn);

    QPushButton* toTraditionalBtn = new QPushButton("转繁体");
    toTraditionalBtn->setFlat(true);
    toTraditionalBtn->setStyleSheet("QPushButton { color: #f39c12; border: none; font-size: 13px; } QPushButton:hover { color: #e67e22; }");
    toTraditionalBtn->setCursor(Qt::PointingHandCursor);
    toTraditionalBtn->setProperty("tooltipText", "转换为繁体中文");
    toTraditionalBtn->installEventFilter(this);
    connect(toTraditionalBtn, &QPushButton::clicked, [this]{
        m_textEdit->setPlainText(StringUtils::convertChineseVariant(m_textEdit->toPlainText(), false));
    });
    bottomLayout->addWidget(toTraditionalBtn);

    QPushButton* typesettingBtn = new QPushButton("排版");
    typesettingBtn->setFlat(true);
    typesettingBtn->setStyleSheet("QPushButton { color: #4a90e2; border: none; font-size: 13px; } QPushButton:hover { color: #6ab0ff; }");
    typesettingBtn->setCursor(Qt::PointingHandCursor);
    typesettingBtn->setProperty("tooltipText", "智能合并换行并优化排版");
    typesettingBtn->installEventFilter(this);
    connect(typesettingBtn, &QPushButton::clicked, this, &OCRResultWindow::onTypesettingClicked);
    bottomLayout->addWidget(typesettingBtn);

    QPushButton* copyBtn = new QPushButton("复制");
    copyBtn->setProperty("tooltipText", "复制识别结果并关闭窗口");
    copyBtn->installEventFilter(this);
    copyBtn->setFixedSize(80, 32);
    copyBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #007ACC;
            color: white;
            border: none;
            border-radius: 4px;
            font-weight: bold;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #008be5;
        }
        QPushButton:pressed {
            background-color: #005a9e;
        }
    )");
    connect(copyBtn, &QPushButton::clicked, this, &OCRResultWindow::onCopyClicked);
    bottomLayout->addWidget(copyBtn);

    layout->addLayout(bottomLayout);
}

void OCRResultWindow::setRecognizedText(const QString& text, int contextId) {
    if (m_contextId != -1 && contextId != m_contextId) return;
    
    m_textEdit->setPlainText(text);
    
    if (m_autoCopyCheck->isChecked()) {
        if (!isVisible()) {
            // 静默模式反馈
            if (text.trimmed().isEmpty() || text.contains("未识别到") || text.contains("错误")) {
                this->show();
                return;
            }
            // 2026-03-13 按照用户要求：提示时长缩短为 700ms
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 识别完成并已复制到剪贴板</b>", 700);
        }
        onCopyClicked();
    } else if (!isVisible()) {
        // [NEW] 静默模式下即使未勾选“自动复制”，也应提示已存入库，确保用户知情
        if (text.trimmed().isEmpty() || text.contains("未识别到") || text.contains("错误")) {
            this->show();
            return;
        }
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 识别已完成并存入灵感库</b>");
    }
}

void OCRResultWindow::onCopyClicked() {
    QString text = m_textEdit->toPlainText();
    if (!text.isEmpty()) {
        // [CRITICAL] 明确标记为 ocr_text 类型，确保通过识别提取的文字入库后显示扫描图标
        ClipboardMonitor::instance().forceNext("ocr_text");
        QApplication::clipboard()->setText(text);
    }
    QSettings settings("RapidNotes", "OCR");
    settings.setValue("autoCopy", m_autoCopyCheck->isChecked());
    
    // 明确关闭窗口。由于设置了 WA_DeleteOnClose，close() 会触发析构
    this->close();
}

void OCRResultWindow::onTypesettingClicked() {
    QString text = m_textEdit->toPlainText();
    if (text.isEmpty()) return;

    // 优化排版逻辑：合并被换行符切断的行，保留真正的段落（连续两个换行或空白行）
    // 处理逻辑：
    // 1. 统一换行符
    text.replace("\r\n", "\n");
    // 2. 识别段落：将两个以上的换行替换为特殊标记
    text.replace(QRegularExpression("\n{2,}"), "[[PARAGRAPH]]");
    // 3. 将剩余的单换行替换为空格（西文）或直接删除（中文）
    // 这里简单处理：删除所有单换行，去除行首尾空格
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    QString merged;
    for (int i = 0; i < lines.size(); ++i) {
        merged += lines[i].trimmed();
    }
    // 4. 恢复段落
    merged.replace("[[PARAGRAPH]]", "\n\n");
    
    m_textEdit->setPlainText(merged);
}
