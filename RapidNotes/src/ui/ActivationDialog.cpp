#include "ActivationDialog.h"
#include "ToolTipOverlay.h"
#include "../core/DatabaseManager.h"
#include <QDateTime>
#include <QMessageBox>
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>

ActivationDialog::ActivationDialog(const QString& reason, QWidget* parent)
    : FramelessDialog("软件激活验证", parent)
{
    // 2026-04-xx 按照用户要求：模态激活对话框不需要置顶、最小化、最大化按钮
    if (m_btnPin) m_btnPin->hide();
    if (m_minBtn) m_minBtn->hide();
    if (m_maxBtn) m_maxBtn->hide();

    setFixedSize(420, 280);
    
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(35, 25, 35, 20);
    layout->setSpacing(15);
    
    m_lblReason = new QLabel();
    m_lblReason->setTextFormat(Qt::RichText); // [FIX] 强制指定 HTML 格式，防止代码明文泄漏
    // 2026-03-xx 按照用户要求：正版化移除所有关于“试用”、“过期”的诱导性描述
    m_lblReason->setText(reason);
    m_lblReason->setWordWrap(true);
    m_lblReason->setStyleSheet("color: #ecf0f1; font-size: 13px; font-weight: bold; line-height: 1.4;");
    layout->addWidget(m_lblReason);
    
    m_editKey = new QLineEdit();
    m_editKey->installEventFilter(this);
    m_editKey->setEchoMode(QLineEdit::Password);
    m_editKey->setPlaceholderText("请输入激活码...");
    m_editKey->setStyleSheet("QLineEdit { height: 42px; border: 1px solid #3a90ff; border-radius: 6px; background: #1a1a1a; color: #fff; padding: 0 12px; font-family: 'Consolas'; }");
    layout->addWidget(m_editKey);
    
    // 2026-03-xx 按照用户要求：正版化彻底移除“激活尝试次数”标签及相关提示逻辑
    
    auto* btnRow = new QHBoxLayout();
    auto* btnVerify = new QPushButton("确 认 激 活");
    btnVerify->setFixedHeight(42);
    btnVerify->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 6px; font-weight: bold; font-size: 14px; }"
                             "QPushButton:hover { background: #2b7ae6; }");
    connect(btnVerify, &QPushButton::clicked, this, &ActivationDialog::onVerifyClicked);
    
    auto* btnExit = new QPushButton("退出程序");
    btnExit->setFixedHeight(42);
    btnExit->setStyleSheet("QPushButton { background: #2d2d2d; color: #999; border: 1px solid #444; border-radius: 6px; }"
                            "QPushButton:hover { background: #3d3d3d; color: #fff; }");
    connect(btnExit, &QPushButton::clicked, this, &ActivationDialog::reject);
    
    btnRow->addWidget(btnExit);
    btnRow->addWidget(btnVerify);
    layout->addLayout(btnRow);
    
    auto* lblTlg = new QLabel("联系获取助手：<b style='color: #3a90ff;'>Telegram：TLG_888</b>");
    lblTlg->setAlignment(Qt::AlignCenter);
    lblTlg->setStyleSheet("color: #777; font-size: 12px; margin-top: 8px;");
    layout->addWidget(lblTlg);
    
    layout->addStretch();
}

void ActivationDialog::onVerifyClicked() {
    QString key = m_editKey->text().trimmed();
    if (key.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #f1c40f;'>请输入激活码</b>");
        return;
    }

    if (DatabaseManager::instance().verifyActivationCode(key)) {
        accept(); // 成功激活
    } else {
        m_editKey->clear();
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 激活码错误</b>");
    }
}

bool ActivationDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_editKey && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            m_editKey->setCursorPosition(0);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            m_editKey->setCursorPosition(m_editKey->text().length());
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void ActivationDialog::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 两段式：如果输入框有内容则清空，否则退出程序（激活页逻辑较硬）
        if (!m_editKey->text().isEmpty()) {
            m_editKey->clear();
            event->accept();
            return;
        }
        reject();
        return;
    }

    // 2026-03-xx 按照用户要求：正版化彻底移除 F1 试用重置后门
    
    // 对于其他键盘事件，交由基类处理
    FramelessDialog::keyPressEvent(event);
}
