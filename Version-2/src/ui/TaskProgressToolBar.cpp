#include "TaskProgressToolBar.h"
#include <QHBoxLayout>

namespace ArcMeta {

TaskProgressToolBar::TaskProgressToolBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(28);
    setStyleSheet("background-color: #252526; border-top: 1px solid #333; color: #CCC;");

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(10);

    m_lblStatus = new QLabel("正在导入项目...", this);
    m_lblStatus->setStyleSheet("font-size: 11px;");
    layout->addWidget(m_lblStatus);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background: #333; border: none; border-radius: 3px; }"
        "QProgressBar::chunk { background: #378ADD; border-radius: 3px; }"
    );
    layout->addWidget(m_progressBar, 1);

    m_lblTime = new QLabel("计算中...", this);
    m_lblTime->setStyleSheet("font-size: 11px; color: #888;");
    layout->addWidget(m_lblTime);

    m_btnCancel = new QPushButton("×", this);
    m_btnCancel->setFixedSize(16, 16);
    m_btnCancel->setCursor(Qt::PointingHandCursor);
    m_btnCancel->setStyleSheet("QPushButton { border: none; color: #888; font-weight: bold; } QPushButton:hover { color: #FFF; }");
    layout->addWidget(m_btnCancel);

    connect(m_btnCancel, &QPushButton::clicked, this, &TaskProgressToolBar::cancelRequested);
}

void TaskProgressToolBar::updateProgress(int processed, int total, int remainingSeconds) {
    if (total <= 0) return;
    int pct = static_cast<int>((double)processed / total * 100.0);
    m_progressBar->setValue(pct);
    m_lblStatus->setText(QString("正在导入项目 (%1/%2)...").arg(processed).arg(total));

    if (remainingSeconds >= 0) {
        m_lblTime->setText(QString("剩余约 %1 秒").arg(remainingSeconds));
    } else {
        m_lblTime->setText("计算中...");
    }
}

void TaskProgressToolBar::showCompleted(int processed, int total) {
    m_progressBar->setValue(100);
    m_lblStatus->setText(QString("处理完成 (%1/%2)").arg(processed).arg(total));
    m_lblTime->setText("已就绪");
}

} // namespace ArcMeta
