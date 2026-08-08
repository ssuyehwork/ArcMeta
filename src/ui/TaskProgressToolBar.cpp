#include "TaskProgressToolBar.h"
#include "UiHelper.h"
#include "StyleLibrary.h"
#include <QHBoxLayout>

namespace ArcMeta {

TaskProgressToolBar::TaskProgressToolBar(QWidget* parent) : QFrame(parent) {
    setFixedHeight(28);
    setStyleSheet("QFrame { background-color: #191919; border-top: 1px solid #2D2D2D; }");

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(10);

    // 1. 左侧精细进度条
    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(4);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background: #333333; border: none; border-radius: 2px; }"
        "QProgressBar::chunk { background-color: #378ADD; border-radius: 2px; }"
    );
    layout->addWidget(m_progressBar, 1);

    // 2. 右侧文案及倒计时：正在添加文件... 10/94 (0:05:19)
    m_statusLabel = new QLabel("正在初始化...", this);
    m_statusLabel->setStyleSheet("color: #CCCCCC; font-size: 11px; font-family: 'Segoe UI', Microsoft YaHei;");
    layout->addWidget(m_statusLabel, 0, Qt::AlignVCenter);

    // 3. 最右侧轻量级 '×' 取消按钮
    m_btnCancel = new QPushButton(this);
    m_btnCancel->setFixedSize(18, 18);
    m_btnCancel->setCursor(Qt::PointingHandCursor);
    m_btnCancel->setIcon(UiHelper::getIcon("close", QColor("#AAAAAA"), 14));
    m_btnCancel->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 3px; }"
        "QPushButton:hover { background-color: #E81123; }"
    );
    layout->addWidget(m_btnCancel, 0, Qt::AlignVCenter);

    connect(m_btnCancel, &QPushButton::clicked, this, &TaskProgressToolBar::cancelRequested);
}

void TaskProgressToolBar::updateProgress(int processed, int total, int remainingSeconds, const QString& actionText) {
    if (total <= 0) return;
    int pct = (processed * 100) / total;
    m_progressBar->setValue(pct);

    QString timeStr = "计算中...";
    if (remainingSeconds >= 0) {
        int hours = remainingSeconds / 3600;
        int mins = (remainingSeconds % 3600) / 60;
        int secs = remainingSeconds % 60;
        timeStr = QString("%1:%2:%3")
                    .arg(hours)
                    .arg(mins, 2, 10, QChar('0'))
                    .arg(secs, 2, 10, QChar('0'));
    }

    m_statusLabel->setText(QString("%1 %2/%3 (%4)").arg(actionText).arg(processed).arg(total).arg(timeStr));
}

void TaskProgressToolBar::showCompleted(int total, int successCount) {
    m_progressBar->setValue(100);
    m_statusLabel->setText(QString("处理完成！成功 %1/%2 项").arg(successCount).arg(total));
}

} // namespace ArcMeta
