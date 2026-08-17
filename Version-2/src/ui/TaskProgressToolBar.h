#pragma once

#include <QWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace ArcMeta {

class TaskProgressToolBar : public QWidget {
    Q_OBJECT
public:
    explicit TaskProgressToolBar(QWidget* parent = nullptr);

    void updateProgress(int processed, int total, int remainingSeconds);
    void showCompleted(int processed, int total);

signals:
    void cancelRequested();

private:
    QLabel* m_lblStatus = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_lblTime = nullptr;
    QPushButton* m_btnCancel = nullptr;
};

} // namespace ArcMeta
