#ifndef OCRWINDOW_H
#define OCRWINDOW_H

#include "FramelessDialog.h"
#include <QTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QListWidget>
#include <QTimer>
#include <QQueue>
#include <QProgressBar>

class OCRWindow : public FramelessDialog {
    Q_OBJECT
public:
    explicit OCRWindow(QWidget* parent = nullptr);
    ~OCRWindow();

    void processImages(const QList<QImage>& images);

private slots:
    void onPasteAndRecognize();
    void onBrowseAndRecognize();
    void onClearResults();
    void onCopyResult();
    void onItemSelectionChanged();
    void processNextImage();  // 处理队列中的下一张图片
    void onRecognitionFinished(const QString& text, int contextId);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void initUI();
    void updateRightDisplay();

    struct OCRItem {
        QImage image;
        QString name;
        QString result;
        bool isFinished = false;
        int id = -1;
        int sessionVersion = 0;
    };

    QListWidget* m_itemList = nullptr;
    QTextEdit* m_ocrResult = nullptr;
    QProgressBar* m_progressBar = nullptr;
    
    QList<OCRItem> m_items;
    int m_lastUsedId = 0;
    int m_sessionVersion = 0;
    
    // 顺序处理队列
    QQueue<int> m_processingQueue;  // 待处理的任务 ID 队列
    bool m_isProcessing = false;  // 是否正在处理
};

#endif // OCRWINDOW_H
