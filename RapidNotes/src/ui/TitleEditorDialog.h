#ifndef TITLEEDITORDIALOG_H
#define TITLEEDITORDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>
#include <QCursor>

/**
 * @brief 通用标题编辑对话框，用于元数据面板和笔记编辑窗口的标题快速编辑。
 */
class TitleEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit TitleEditorDialog(const QString& currentText, QWidget* parent = nullptr) : QDialog(parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Popup | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedSize(400, 170);

        auto* layout = new QVBoxLayout(this);
        // [CRITICAL] 边距调整为 20px 以容纳阴影，防止出现“断崖式”阴影截止
        layout->setContentsMargins(20, 20, 20, 20);

        auto* container = new QWidget(this);
        container->setObjectName("container");
        container->setStyleSheet("QWidget#container { background-color: #1e1e1e; border: 1px solid #333; border-radius: 10px; }");
        layout->addWidget(container);

        auto* innerLayout = new QVBoxLayout(container);
        innerLayout->setContentsMargins(12, 12, 12, 10);
        innerLayout->setSpacing(8);

        m_textEdit = new QTextEdit();
        m_textEdit->setText(currentText);
        m_textEdit->setPlaceholderText("请输入标题...");
        m_textEdit->setStyleSheet("QTextEdit { background-color: #252526; border: 1px solid #444; border-radius: 6px; color: white; font-size: 14px; padding: 8px; } QTextEdit:focus { border: 1px solid #4a90e2; }");
        m_textEdit->installEventFilter(this);
        innerLayout->addWidget(m_textEdit);

        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        auto* btnSave = new QPushButton("完成");
        btnSave->setFixedSize(64, 30);
        btnSave->setCursor(Qt::PointingHandCursor);
        btnSave->setStyleSheet("QPushButton { background-color: #4a90e2; color: white; border: none; border-radius: 4px; font-weight: bold; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
        connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);
        btnLayout->addWidget(btnSave);
        innerLayout->addLayout(btnLayout);

        // 1:1 匹配 QuickWindow 阴影规范 (同步修复模糊截止问题)
        auto* shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(20);
        shadow->setColor(QColor(0, 0, 0, 120));
        shadow->setOffset(0, 4);
        container->setGraphicsEffect(shadow);
    }

    QString getText() const { return m_textEdit->toPlainText().trimmed(); }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }

public:
    void showAtCursor() {
        QPoint pos = QCursor::pos();
        // 尝试居中显示在鼠标点击位置附近
        move(pos.x() - width() / 2, pos.y() - 40);
        show();
        m_textEdit->setFocus();
        m_textEdit->selectAll();
    }

private:
    QTextEdit* m_textEdit;

public:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == m_textEdit && event->type() == QEvent::KeyPress) {
            QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Up) {
                QTextCursor cursor = m_textEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Up);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::Start);
                    m_textEdit->setTextCursor(cursor);
                    return true;
                }
            } else if (keyEvent->key() == Qt::Key_Down) {
                QTextCursor cursor = m_textEdit->textCursor();
                int pos = cursor.position();
                cursor.movePosition(QTextCursor::Down);
                if (cursor.position() == pos) {
                    cursor.movePosition(QTextCursor::End);
                    m_textEdit->setTextCursor(cursor);
                    return true;
                }
            }
        }
        return QDialog::eventFilter(watched, event);
    }
};

#endif // TITLEEDITORDIALOG_H
