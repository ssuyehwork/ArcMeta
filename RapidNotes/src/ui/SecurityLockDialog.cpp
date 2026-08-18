#include "SecurityLockDialog.h"
#include "ToolTipOverlay.h"
#include "../core/DatabaseManager.h"
#include <QKeyEvent>
#include <QApplication>
#include <QStyle>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

SecurityLockDialog::SecurityLockDialog(const QString& message, QWidget* parent)
    : FramelessDialog("安全锁定", parent)
{
    setFixedSize(480, 240);
    
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(20);

    auto* row = new QHBoxLayout();
    auto* iconLabel = new QLabel();
    // 使用红色警告图标
    QIcon warnIcon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
    iconLabel->setPixmap(warnIcon.pixmap(48, 48));
    row->addWidget(iconLabel);

    m_lblMessage = new QLabel(message);
    m_lblMessage->setWordWrap(true);
    m_lblMessage->setStyleSheet("color: #ecf0f1; font-size: 14px; line-height: 1.4;");
    row->addWidget(m_lblMessage, 1);
    layout->addLayout(row);

    // 2026-03-xx 按照用户要求：正版化彻底移除“抢救尝试次数”显示

    layout->addStretch();

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* btnOk = new QPushButton("确 定");
    btnOk->setFixedSize(100, 36);
    btnOk->setStyleSheet("QPushButton { background-color: #3A90FF; color: white; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
    connect(btnOk, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(btnOk);
    layout->addLayout(btnLayout);

    // 强制置顶并聚焦
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
}


void SecurityLockDialog::keyPressEvent(QKeyEvent* event) {
    // 2026-03-xx 按照用户要求：正版化彻底移除 Ctrl+Shift+Alt+F10 救援模式
    FramelessDialog::keyPressEvent(event);
}
