#include "CategoryPasswordDialog.h"
#include <QTextEdit>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

CategoryPasswordDialog::CategoryPasswordDialog(const QString& title, QWidget* parent)
    : FramelessDialog(title, parent)
{
    setFixedSize(360, 435);
    installEventFilter(this);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(20, 15, 20, 20);
    layout->setSpacing(12);

    QString labelStyle = "color: #eee; font-size: 13px; font-weight: bold;";
    QString editStyle = 
        "QLineEdit, QTextEdit {"
        "  background-color: #121212; border: 1px solid #333; border-radius: 6px;"
        "  padding: 8px; color: white; selection-background-color: #4a90e2;"
        "}"
        "QLineEdit:focus, QTextEdit:focus { border: 1px solid #3a90ff; }";

    // 密码
    auto* lblPwd = new QLabel("密码");
    lblPwd->setStyleSheet(labelStyle);
    layout->addWidget(lblPwd);

    m_pwdEdit = new QLineEdit();
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setStyleSheet(editStyle);
    layout->addWidget(m_pwdEdit);

    // 密码确认
    auto* lblConfirm = new QLabel("密码确认");
    lblConfirm->setStyleSheet(labelStyle);
    layout->addWidget(lblConfirm);

    m_confirmEdit = new QLineEdit();
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setStyleSheet(editStyle);
    layout->addWidget(m_confirmEdit);

    m_pwdEdit->installEventFilter(this);
    m_confirmEdit->installEventFilter(this);

    // 密码提示
    auto* lblHint = new QLabel("密码提示");
    lblHint->setStyleSheet(labelStyle);
    layout->addWidget(lblHint);

    m_hintEdit = new QTextEdit();
    m_hintEdit->setFixedHeight(80);
    m_hintEdit->setStyleSheet(editStyle);
    m_hintEdit->installEventFilter(this);
    layout->addWidget(m_hintEdit);

    layout->addStretch();
    layout->addSpacing(15);

    // 保存按钮
    auto* btnSave = new QPushButton("保存密码设置");
    btnSave->setAutoDefault(false);
    btnSave->setFixedHeight(44);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setStyleSheet(
        "QPushButton {"
        "  background-color: #3a90ff; color: white; border: none; border-radius: 8px;"
        "  font-weight: bold; font-size: 14px;"
        "}"
        "QPushButton:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
        "QPushButton:pressed { background-color: #1a5dbf; }"
    );
    connect(btnSave, &QPushButton::clicked, this, [this](){
        if (m_pwdEdit->text() != m_confirmEdit->text()) {
            // 这里以后可以加个提示，但按用户要求尽量简洁
            m_confirmEdit->setStyleSheet(m_confirmEdit->styleSheet() + "border: 1px solid #e74c3c;");
            return;
        }
        accept();
    });
    layout->addWidget(btnSave);
}

bool CategoryPasswordDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (auto* edit = qobject_cast<QLineEdit*>(watched)) {
            if (keyEvent->key() == Qt::Key_Up) {
                edit->setCursorPosition(0);
                return true;
            } else if (keyEvent->key() == Qt::Key_Down) {
                edit->setCursorPosition(edit->text().length());
                return true;
            }
        } else if (watched == m_hintEdit) {
            if (keyEvent->key() == Qt::Key_Up) {
                QTextCursor cursor = m_hintEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Up);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::Start);
                    m_hintEdit->setTextCursor(cursor);
                    return true;
                }
            } else if (keyEvent->key() == Qt::Key_Down) {
                QTextCursor cursor = m_hintEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Down);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::End);
                    m_hintEdit->setTextCursor(cursor);
                    return true;
                }
            }
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void CategoryPasswordDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 两段式：如果密码框有输入，先清空，否则关闭
        if (!m_pwdEdit->text().isEmpty() || !m_confirmEdit->text().isEmpty()) {
            m_pwdEdit->clear();
            m_confirmEdit->clear();
            event->accept();
            return;
        }
        reject(); // 关闭对话框
        return;
    }
    FramelessDialog::keyPressEvent(event);
}

void CategoryPasswordDialog::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    // 使用 QTimer 确保在窗口完全显示后获取焦点，增加延迟至 100ms 以应对复杂的菜单关闭场景
    QTimer::singleShot(100, m_pwdEdit, qOverload<>(&QWidget::setFocus));
}

void CategoryPasswordDialog::setInitialData(const QString& hint) {
    m_hintEdit->setPlainText(hint);
}

QString CategoryPasswordDialog::password() const {
    return m_pwdEdit->text();
}

QString CategoryPasswordDialog::confirmPassword() const {
    return m_confirmEdit->text();
}

QString CategoryPasswordDialog::passwordHint() const {
    return m_hintEdit->toPlainText();
}
