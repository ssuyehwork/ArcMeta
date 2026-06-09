#include "CategoryLockWidget.h"
#include "IconHelper.h"
#include "../core/DatabaseManager.h"
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QEvent>

CategoryLockWidget::CategoryLockWidget(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setAlignment(Qt::AlignCenter);

    auto* container = new QWidget();
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setAlignment(Qt::AlignCenter);
    layout->setSpacing(8);

    // 1. 锁图标 (精简至 32x32)
    auto* lockIcon = new QLabel();
    // [USER_REQUEST] 2026-03-xx 按照用户要求：分类锁定界面中心图标改为绿色 #00A650，标识为安全
    lockIcon->setPixmap(IconHelper::getIcon("lock_secure", "#00A650").pixmap(32, 32));
    lockIcon->setAlignment(Qt::AlignCenter);
    layout->addWidget(lockIcon);

    // 2. 提示文字 (精简至 13px)
    auto* titleLabel = new QLabel("输入密码查看内容");
    // [USER_REQUEST] 2026-03-xx 按照用户要求：标题文字改为绿色 #00A650
    titleLabel->setStyleSheet("color: #00A650; font-size: 13px; font-weight: bold; background: transparent;");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);

    // 3. 密码提示 (精简至 11px)
    m_hintLabel = new QLabel("密码提示: ");
    m_hintLabel->setStyleSheet("color: #555555; font-size: 11px; background: transparent;");
    m_hintLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_hintLabel);

    layout->addSpacing(2);

    // 4. 密码输入框 (收紧至 180px)
    m_pwdEdit = new QLineEdit();
    m_pwdEdit->setPlaceholderText("输入密码");
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setFixedWidth(180);
    m_pwdEdit->setFixedHeight(28);
    m_pwdEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #121212; border: 1px solid #333; border-radius: 4px;"
        "  padding: 0 8px; color: white; font-size: 12px;"
        "}"
        "QLineEdit:focus { border: 1px solid #3a90ff; }"
    );
    connect(m_pwdEdit, &QLineEdit::returnPressed, this, &CategoryLockWidget::onVerify);
    m_pwdEdit->installEventFilter(this);
    layout->addWidget(m_pwdEdit, 0, Qt::AlignHCenter);

    mainLayout->addWidget(container);

    // 移除强制背景色，使其自然融合到父容器 (#1e1e1e) 中
    setStyleSheet("background: transparent;");
}

void CategoryLockWidget::setCategory(int id, const QString& hint) {
    if (m_catId == id && isVisible()) return; // 关键修复：防止因数据刷新导致的输入框重置
    
    m_catId = id;
    m_hintLabel->setText(QString("密码提示: %1").arg(hint.isEmpty() ? "无" : hint));
    m_pwdEdit->clear();
    m_pwdEdit->setFocus();
}

bool CategoryLockWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_pwdEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            if (!m_pwdEdit->text().isEmpty()) {
                m_pwdEdit->clear();
            } else {
                emit escPressed();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CategoryLockWidget::onVerify() {
    if (m_catId == -1) return;
    
    if (DatabaseManager::instance().verifyCategoryPassword(m_catId, m_pwdEdit->text())) {
        emit unlocked(m_catId);
    } else {
        m_pwdEdit->setStyleSheet(m_pwdEdit->styleSheet() + "border: 1px solid #e74c3c;");
        m_pwdEdit->selectAll();
    }
}
