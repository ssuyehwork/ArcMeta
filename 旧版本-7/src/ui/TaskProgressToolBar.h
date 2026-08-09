#pragma once

#include <QFrame>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>

namespace ArcMeta {

class TaskProgressToolBar : public QFrame {
    Q_OBJECT
public:
    explicit TaskProgressToolBar(QWidget* parent = nullptr);

    // 更新进度、完成数量、总数及剩余秒数倒计时
    void updateProgress(int processed, int total, int remainingSeconds, const QString& actionText = "正在添加文件...");
    
    // 设置完成态提示
    void showCompleted(int total, int successCount);

signals:
    // 用户点击最右侧 '×' 触发取消信号
    void cancelRequested();

private:
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_btnCancel = nullptr;
};

} // namespace ArcMeta
