#include "PasswordGeneratorWindow.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include "../core/DatabaseManager.h"
#include "../core/ClipboardMonitor.h"
#include <QMouseEvent>
#include <QApplication>
#include <QClipboard>
#include <QRandomGenerator>
#include <QTimer>
#include <QGraphicsDropShadowEffect>
#include <QSettings>

PasswordGeneratorWindow::PasswordGeneratorWindow(QWidget* parent) : FramelessDialog("密码生成器", parent) {
    setObjectName("PasswordGeneratorWindow");
    // [CRITICAL] 增加窗口高度从 400 到 420，防止底部控件截断
    setFixedSize(570, 420);

    loadWindowSettings();
    initUI();
}

PasswordGeneratorWindow::~PasswordGeneratorWindow() {
}

void PasswordGeneratorWindow::initUI() {
    auto* innerLayout = new QVBoxLayout(m_contentArea);
    innerLayout->setContentsMargins(20, 10, 20, 20);
    innerLayout->setSpacing(10);

    m_usageEntry = new QLineEdit();
    m_usageEntry->setPlaceholderText("Account / Usage (e.g. GitHub, Gmail...)");
    m_usageEntry->setFixedHeight(36);
    m_usageEntry->setStyleSheet("QLineEdit { background-color: #252525; border: 1px solid #333333; border-radius: 8px; color: #cccccc; font-size: 13px; padding-left: 10px; } QLineEdit:focus { border-color: #3b8ed0; }");
    innerLayout->addWidget(m_usageEntry);

    innerLayout->addWidget(createDisplayArea());
    innerLayout->addWidget(createControlsArea());

    auto* generateBtn = new QPushButton("Generate Password");
    generateBtn->setFixedHeight(40);
    generateBtn->setFixedWidth(200);
    generateBtn->setStyleSheet("QPushButton { background-color: #2cc985; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: bold; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
    connect(generateBtn, &QPushButton::clicked, this, &PasswordGeneratorWindow::generatePassword);
    innerLayout->addWidget(generateBtn, 0, Qt::AlignCenter);

    m_statusLabel = new QLabel("");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("color: gray; font-size: 9px;");
    innerLayout->addWidget(m_statusLabel);

}

QWidget* PasswordGeneratorWindow::createDisplayArea() {
    auto* frame = new QWidget();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_passEntry = new QLineEdit();
    m_passEntry->setFixedHeight(44);
    m_passEntry->setAlignment(Qt::AlignCenter);
    m_passEntry->setReadOnly(true);
    m_passEntry->setStyleSheet("QLineEdit { background-color: #2b2b2b; border: none; border-radius: 10px; color: #e0e0e0; font-family: Consolas; font-size: 15px; }");

    m_strengthBar = new QProgressBar();
    m_strengthBar->setFixedHeight(3);
    m_strengthBar->setTextVisible(false);
    m_strengthBar->setStyleSheet("QProgressBar { border: none; background-color: #2b2b2b; border-radius: 1.5px; } QProgressBar::chunk { background-color: #4ade80; border-radius: 1.5px; }");
    m_strengthBar->setRange(0, 100);
    m_strengthBar->setValue(0);

    layout->addWidget(m_passEntry);
    layout->addWidget(m_strengthBar);
    return frame;
}

QWidget* PasswordGeneratorWindow::createControlsArea() {
    auto* frame = new QWidget();
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 5, 0, 5);
    layout->setSpacing(10);

    m_lengthLabel = new QLabel("Length: 16");
    m_lengthLabel->setStyleSheet("font-size: 12px; font-weight: bold; color: #cccccc;");

    m_lengthSlider = new QSlider(Qt::Horizontal);
    m_lengthSlider->setRange(8, 64);
    
    QSettings settings("RapidNotes", "PasswordGenerator");
    int savedLen = settings.value("length", 16).toInt();
    m_lengthSlider->setValue(savedLen);
    m_lengthLabel->setText(QString("Length: %1").arg(savedLen));

    connect(m_lengthSlider, &QSlider::valueChanged, [this](int v) {
        m_lengthLabel->setText(QString("Length: %1").arg(v));
        QSettings("RapidNotes", "PasswordGenerator").setValue("length", v);
    });
    m_lengthSlider->setStyleSheet("QSlider::groove:horizontal { border: 1px solid #444; height: 4px; background: #333; margin: 2px 0; border-radius: 2px; } "
                                  "QSlider::handle:horizontal { background-color: #3b8ed0; border: 5px solid #1e1e1e; width: 18px; height: 18px; margin: -7px 0; border-radius: 9px; } "
                                  "QSlider::sub-page:horizontal { background: #3b8ed0; border-radius: 2px; }");

    auto* checksFrame = new QWidget();
    auto* checksLayout = new QHBoxLayout(checksFrame);
    checksLayout->setContentsMargins(10, 0, 10, 0);

    QString cbStyle = "QCheckBox { spacing: 8px; font-size: 12px; font-weight: bold; color: #cccccc; } "
                      "QCheckBox::indicator { width: 18px; height: 18px; border: 2px solid #555; border-radius: 5px; background-color: transparent; } "
                      "QCheckBox::indicator:hover { border-color: #2cc985; } "
                      "QCheckBox::indicator:checked { background-color: #2cc985; border-color: #2cc985; }";

    m_checkUpper = new QCheckBox("A-Z"); m_checkUpper->setChecked(true); m_checkUpper->setStyleSheet(cbStyle);
    m_checkLower = new QCheckBox("a-z"); m_checkLower->setChecked(true); m_checkLower->setStyleSheet(cbStyle);
    m_checkDigits = new QCheckBox("0-9"); m_checkDigits->setChecked(true); m_checkDigits->setStyleSheet(cbStyle);
    m_checkSymbols = new QCheckBox("@#$"); m_checkSymbols->setChecked(true); m_checkSymbols->setStyleSheet(cbStyle);

    checksLayout->addStretch();
    checksLayout->addWidget(m_checkUpper);
    checksLayout->addStretch();
    checksLayout->addWidget(m_checkLower);
    checksLayout->addStretch();
    checksLayout->addWidget(m_checkDigits);
    checksLayout->addStretch();
    checksLayout->addWidget(m_checkSymbols);
    checksLayout->addStretch();

    m_excludeAmbiguous = new QCheckBox("排除相似字符 (0O1lI)");
    m_excludeAmbiguous->setChecked(settings.value("excludeAmbiguous", false).toBool());
    // [CRITICAL] 增加 min-height 并调整 margin 确保复选框完整显示不被遮挡
    m_excludeAmbiguous->setStyleSheet("QCheckBox { spacing: 8px; font-size: 11px; color: #cccccc; min-height: 22px; margin-top: 5px; } "
                                     "QCheckBox::indicator { width: 16px; height: 16px; border: 2px solid #555; border-radius: 4px; background-color: transparent; } "
                                     "QCheckBox::indicator:hover { border-color: #3b8ed0; } "
                                     "QCheckBox::indicator:checked { background-color: #3b8ed0; border-color: #3b8ed0; }");
    connect(m_excludeAmbiguous, &QCheckBox::toggled, [](bool checked){
        QSettings("RapidNotes", "PasswordGenerator").setValue("excludeAmbiguous", checked);
    });

    layout->addWidget(m_lengthLabel);
    layout->addWidget(m_lengthSlider);
    layout->addWidget(checksFrame);
    layout->addWidget(m_excludeAmbiguous);

    return frame;
}

void PasswordGeneratorWindow::generatePassword() {
    QString usageText = m_usageEntry->text().trimmed();
    if (usageText.isEmpty()) {
        m_usageEntry->setStyleSheet("QLineEdit { background-color: #252525; border: 1px solid #ef4444; border-radius: 8px; color: #cccccc; font-size: 13px; padding-left: 10px; }");
        ToolTipOverlay::instance()->showText(m_usageEntry->mapToGlobal(QPoint(0, m_usageEntry->height())), "请输入账号备注信息！", 700);
        // 2026-03-xx 统一缩短提示时间为 700ms
        QTimer::singleShot(700, [this]() {
            m_usageEntry->setStyleSheet("QLineEdit { background-color: #252525; border: 1px solid #333333; border-radius: 8px; color: #cccccc; font-size: 13px; padding-left: 10px; }");
        });
        return;
    }

    if (!m_checkUpper->isChecked() && !m_checkLower->isChecked() && !m_checkDigits->isChecked() && !m_checkSymbols->isChecked()) {
        // 2026-03-xx 统一缩短提示时间为 700ms
        ToolTipOverlay::instance()->showText(m_passEntry->mapToGlobal(QPoint(0, 0)), "至少选择一种字符类型！", 700);
        return;
    }

    int length = m_lengthSlider->value();
    QString pwd = generateSecurePassword(length, m_checkUpper->isChecked(), m_checkLower->isChecked(), m_checkDigits->isChecked(), m_checkSymbols->isChecked(), m_excludeAmbiguous->isChecked());

    m_passEntry->setText(pwd);
    
    // 1. 手动存入数据库：备注为标题，密码为内容，绑定双标签
    DatabaseManager::instance().addNote(usageText, pwd, {"密码", "密码生成器"}, "", -1, "text");

    // 2. 复制到剪贴板前先屏蔽自动监听，防止重复入库。仅复制密码本体。
    ClipboardMonitor::instance().skipNext();
    QApplication::clipboard()->setText(pwd);

    m_statusLabel->setText(QString("✓ 已保存并复制密码！[%1]").arg(usageText));
    m_statusLabel->setStyleSheet("color: #4ade80; font-size: 9px;");

    // Update strength bar
    if (length < 10) {
        m_strengthBar->setStyleSheet("QProgressBar { border: none; background-color: #2b2b2b; border-radius: 1.5px; } QProgressBar::chunk { background-color: #ef4444; border-radius: 1.5px; }");
        m_strengthBar->setValue(30);
    } else if (length < 16) {
        m_strengthBar->setStyleSheet("QProgressBar { border: none; background-color: #2b2b2b; border-radius: 1.5px; } QProgressBar::chunk { background-color: #f59e0b; border-radius: 1.5px; }");
        m_strengthBar->setValue(60);
    } else {
        m_strengthBar->setStyleSheet("QProgressBar { border: none; background-color: #2b2b2b; border-radius: 1.5px; } QProgressBar::chunk { background-color: #2cc985; border-radius: 1.5px; }");
        m_strengthBar->setValue(100);
    }
}

QString PasswordGeneratorWindow::generateSecurePassword(int length, bool upper, bool lower, bool digits, bool symbols, bool excludeAmbiguous) {
    QString pool = "";
    QString required = "";
    QString symStr = "!@#$%^&*()-_=+[]{}|;:,.<>?/~`";
    QString ambig = "0O1lI";

    auto addFromSet = [&](bool use, const QString& set) {
        if (!use) return;
        QString filtered = set;
        if (excludeAmbiguous) {
            for (QChar c : ambig) filtered.remove(c);
        }
        if (!filtered.isEmpty()) {
            pool += filtered;
            required += filtered.at(QRandomGenerator::global()->bounded(filtered.length()));
        }
    };

    addFromSet(upper, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    addFromSet(lower, "abcdefghijklmnopqrstuvwxyz");
    addFromSet(digits, "0123456789");
    addFromSet(symbols, symStr);

    if (pool.isEmpty()) return "";

    QString pwd = required;
    while (pwd.length() < length) {
        pwd += pool.at(QRandomGenerator::global()->bounded(pool.length()));
    }

    // Shuffle
    for (int i = 0; i < pwd.length(); ++i) {
        int j = QRandomGenerator::global()->bounded(pwd.length());
        QChar temp = pwd[i];
        pwd[i] = pwd[j];
        pwd[j] = temp;
    }

    return pwd.left(length);
}

