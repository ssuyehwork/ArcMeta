#include "PasswordVerifyDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QApplication>
#include <QSettings>
#include "ToolTipOverlay.h"
#include "StringUtils.h"

bool PasswordVerifyDialog::verify() {
    // 2026-03-20 按照用户要求，所有导出操作前必须进行身份验证
    QSettings settings("RapidNotes", "QuickWindow");
    QString realPwd = settings.value("appPassword").toString();

    // 1. 如果用户未设置密码，根据方案，引导用户先进行安全设置
    if (realPwd.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            StringUtils::wrapToolTip("<b style='color: #e67e22;'>[安全拦截] 请先在【系统设置】中设置“应用锁定密码”后再执行导出</b>"), 3000);
        return false;
    }

    // 2. 弹出统一的验证对话框
    PasswordVerifyDialog dlg("导出身份验证", "当前操作涉及数据导出，请输入应用锁定密码以继续：", nullptr);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }

    // 3. 校验密码
    if (dlg.password() != realPwd) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), 
            StringUtils::wrapToolTip("<b style='color: #e74c3c;'>❌ 密码验证失败，导出已终止</b>"), 2000);
        return false;
    }

    // 验证通过
    return true;
}

PasswordVerifyDialog::PasswordVerifyDialog(const QString& title, const QString& message, QWidget* parent)
    : FramelessDialog(title, parent)
{
    // 2026-04-xx 按照用户要求：模态验证框不需要置顶、最小化、最大化按钮
    if (m_btnPin) m_btnPin->hide();
    if (m_minBtn) m_minBtn->hide();
    if (m_maxBtn) m_maxBtn->hide();

    setFixedSize(400, 220);

    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(30, 20, 30, 20);
    layout->setSpacing(15);

    // 1. 提示信息
    auto* lblMsg = new QLabel(message);
    lblMsg->setStyleSheet("color: #ccc; font-size: 13px;");
    lblMsg->setWordWrap(true);
    layout->addWidget(lblMsg);

    // 2. 密码输入框
    m_pwdEdit = new QLineEdit();
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setPlaceholderText("请输入应用锁定密码");
    m_pwdEdit->setFixedHeight(36);
    // 2026-04-xx 按照宪法第五定律：输入框圆角统一修正为 6px
    m_pwdEdit->setStyleSheet(
        "QLineEdit {"
        "  background-color: #1a1a1a; border: 1px solid #333; border-radius: 6px;"
        "  padding: 0 10px; color: white; font-size: 14px;"
        "}"
        "QLineEdit:focus { border: 1px solid #3a90ff; }"
    );
    layout->addWidget(m_pwdEdit);

    layout->addStretch();

    // 3. 按钮栏
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    
    auto* btnCancel = new QPushButton("取 消");
    btnCancel->setFixedSize(90, 32);
    btnCancel->setStyleSheet("QPushButton { background: #444; color: #ccc; border-radius: 4px; } QPushButton:hover { background: #555; }");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    
    auto* btnOk = new QPushButton("确 定");
    btnOk->setFixedSize(90, 32);
    btnOk->setStyleSheet("QPushButton { background: #3a90ff; color: white; border-radius: 4px; font-weight: bold; } QPushButton:hover { background: #2b7ae6; }");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    // 默认点击确定
    btnOk->setDefault(true);
    
    btnLayout->addStretch();
    btnLayout->addWidget(btnCancel);
    btnLayout->addWidget(btnOk);
    layout->addLayout(btnLayout);

    // 绑定回车键
    connect(m_pwdEdit, &QLineEdit::returnPressed, this, &QDialog::accept);
}

void PasswordVerifyDialog::showEvent(QShowEvent* event) {
    FramelessDialog::showEvent(event);
    m_pwdEdit->setFocus();
}
