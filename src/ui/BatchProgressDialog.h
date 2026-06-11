#pragma once

#include "FramelessDialog.h"
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QString>
#include <atomic>

namespace ArcMeta {

/**
 * @brief 批量操作进度对话框
 */
class BatchProgressDialog : public FramelessDialog {
    Q_OBJECT

public:
    explicit BatchProgressDialog(const QString& title, QWidget* parent = nullptr)
        : FramelessDialog(title, parent) {
        setFixedSize(400, 150);
        
        // 2026-06-xx 按照用户要求：彻底移除置顶、最小化、最大化按钮，仅保留关闭按钮
        m_pinBtn->hide();
        m_minBtn->hide();
        m_maxBtn->hide();

        auto* layout = new QVBoxLayout(m_contentArea);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(10);

        m_statusLabel = new QLabel("正在准备...", this);
        m_statusLabel->setStyleSheet("color: #EEE; font-size: 13px;");
        layout->addWidget(m_statusLabel);

        m_progressBar = new QProgressBar(this);
        m_progressBar->setFixedHeight(8);
        m_progressBar->setTextVisible(false);
        m_progressBar->setStyleSheet(
            "QProgressBar { background: #2D2D2D; border: none; border-radius: 4px; }"
            "QProgressBar::chunk { background: #378ADD; border-radius: 4px; }"
        );
        layout->addWidget(m_progressBar);
        
        layout->addStretch();

        m_aborted.store(false);
    }

    /**
     * @brief 2026-06-xx 按照用户要求：点击关闭按钮（或触发 reject）即停止导入
     */
    void reject() override {
        if (m_aborted.load()) return; // 防止重复触发

        m_aborted.store(true);
        m_statusLabel->setText("<b style='color:#e74c3c;'>正在停止操作...</b>");
        m_closeBtn->setEnabled(false);

        // 注意：不立即调用 QDialog::reject()，等待工作线程感知到 m_aborted 后自行结束并清理
    }

    bool isAborted() const {
        return m_aborted.load();
    }

    void setStatus(const QString& text) {
        m_statusLabel->setText(text);
    }

    void setRange(int min, int max) {
        m_progressBar->setRange(min, max);
    }

    void setValue(int value) {
        m_progressBar->setValue(value);
    }

    /**
     * @brief 跨线程安全更新进度
     */
    Q_INVOKABLE void updateProgress(int current, int total, const QString& fileName) {
        if (total <= 0) return;
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(current);
        
        double percent = (double)current / (double)total * 100.0;
        QString status = QString("[%1/%2] %3% - %4")
                            .arg(current)
                            .arg(total)
                            .arg(QString::number(percent, 'f', 1))
                            .arg(fileName);
        m_statusLabel->setText(status);
    }

private:
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    std::atomic<bool> m_aborted;
};

} // namespace ArcMeta
