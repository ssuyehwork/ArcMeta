#include "OCRWindow.h"
#include "IconHelper.h"
#include "../core/OCRManager.h"
#include "../core/ClipboardMonitor.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QFileDialog>
#include <QDropEvent>
#include <QFileInfo>
#include <QDateTime>
#include <QThread>
#include <utility>

OCRWindow::OCRWindow(QWidget* parent) : FramelessDialog("截图取文", parent) {
    setObjectName("OCRWindow");
    setFixedSize(800, 500);
    setAcceptDrops(true);

    loadWindowSettings();
    initUI();
    onClearResults();
    
    qDebug() << "[OCR] OCRWindow 初始化完成，使用顺序处理模式";
    
    connect(&OCRManager::instance(), &OCRManager::recognitionFinished, 
            this, &OCRWindow::onRecognitionFinished, 
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
}

OCRWindow::~OCRWindow() {
    m_processingQueue.clear();
    m_isProcessing = false;
}

void OCRWindow::initUI() {
    auto* mainHLayout = new QHBoxLayout(m_contentArea);
    mainHLayout->setContentsMargins(10, 10, 10, 10);
    mainHLayout->setSpacing(10);

    // --- 左侧：操作面板 ---
    auto* leftPanel = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    auto* btnBrowse = new QPushButton(" 本地浏览");
    btnBrowse->setIcon(IconHelper::getIcon("folder", "#ffffff"));
    btnBrowse->setFixedHeight(36);
    btnBrowse->setStyleSheet("QPushButton { background: #4a90e2; color: white; border-radius: 4px; padding: 0 10px; text-align: left; } QPushButton:hover { background: #3e3e42; }"); // 2026-03-xx 统一悬停色
    connect(btnBrowse, &QPushButton::clicked, this, &OCRWindow::onBrowseAndRecognize);
    leftLayout->addWidget(btnBrowse);

    auto* btnPaste = new QPushButton(" 粘贴识别");
    btnPaste->setIcon(IconHelper::getIcon("copy", "#ffffff"));
    btnPaste->setFixedHeight(36);
    btnPaste->setStyleSheet("QPushButton { background: #2ecc71; color: white; border-radius: 4px; padding: 0 10px; text-align: left; } QPushButton:hover { background: #27ae60; }");
    connect(btnPaste, &QPushButton::clicked, this, &OCRWindow::onPasteAndRecognize);
    leftLayout->addWidget(btnPaste);

    leftLayout->addSpacing(10);

    auto* btnClear = new QPushButton(" 清空列表");
    btnClear->setIcon(IconHelper::getIcon("trash", "#e74c3c")); // 2026-03-13 统一 Trash 图标颜色为红色
    btnClear->setFixedHeight(36);
    btnClear->setStyleSheet("QPushButton { background: #333; color: #ccc; border: 1px solid #444; border-radius: 4px; padding: 0 10px; text-align: left; } QPushButton:hover { background: #444; color: #fff; }");
    connect(btnClear, &QPushButton::clicked, this, &OCRWindow::onClearResults);
    leftLayout->addWidget(btnClear);

    leftLayout->addStretch();

    auto* btnCopy = new QPushButton(" 复制文字");
    btnCopy->setIcon(IconHelper::getIcon("copy", "#ffffff"));
    btnCopy->setFixedHeight(36);
    btnCopy->setStyleSheet("QPushButton { background: #3d3d3d; color: #eee; border: 1px solid #4a4a4a; border-radius: 4px; padding: 0 10px; text-align: left; } QPushButton:hover { background: #4d4d4d; }");
    connect(btnCopy, &QPushButton::clicked, this, &OCRWindow::onCopyResult);
    leftLayout->addWidget(btnCopy);

    leftPanel->setFixedWidth(120);
    mainHLayout->addWidget(leftPanel);

    // --- 中间：项目列表 ---
    auto* middlePanel = new QWidget();
    auto* middleLayout = new QVBoxLayout(middlePanel);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(5);

    auto* listLabel = new QLabel("上传的项目");
    listLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    middleLayout->addWidget(listLabel);

    m_itemList = new QListWidget();
    m_itemList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_itemList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_itemList->setStyleSheet(
        "QListWidget { background: #222; border: 1px solid #333; border-radius: 4px; color: #ddd; }"
        "QListWidget::item { height: 32px; padding-left: 5px; }"
        "QListWidget::item:selected { background: #3e3e42; color: white; border-radius: 2px; }" // 2026-03-xx 统一选中色
    );
    connect(m_itemList, &QListWidget::itemSelectionChanged, this, &OCRWindow::onItemSelectionChanged);
    middleLayout->addWidget(m_itemList);

    middlePanel->setFixedWidth(180);
    mainHLayout->addWidget(middlePanel);

    // --- 右侧：结果展示 ---
    auto* rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(5);

    auto* resultLabel = new QLabel("识别结果");
    resultLabel->setStyleSheet("color: #888; font-size: 11px; font-weight: bold;");
    rightLayout->addWidget(resultLabel);

    
    // 进度条
    m_progressBar = new QProgressBar();
    m_progressBar->setTextVisible(true);
    m_progressBar->setAlignment(Qt::AlignCenter);
    m_progressBar->setStyleSheet("QProgressBar { border: none; background: #333; border-radius: 4px; height: 6px; text-align: center; color: transparent; } QProgressBar::chunk { background-color: #4a90e2; border-radius: 4px; }");
    m_progressBar->hide();
    rightLayout->addWidget(m_progressBar);

    m_ocrResult = new QTextEdit();
    m_ocrResult->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ocrResult->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ocrResult->setReadOnly(true);
    m_ocrResult->setPlaceholderText("选择左侧项目查看识别结果...");
    m_ocrResult->setStyleSheet("QTextEdit { background: #1a1a1a; border: 1px solid #333; border-radius: 6px; color: #eee; font-size: 13px; padding: 10px; line-height: 1.4; }");
    rightLayout->addWidget(m_ocrResult);

    mainHLayout->addWidget(rightPanel);
}

void OCRWindow::onPasteAndRecognize() {
    qDebug() << "[OCR] 粘贴识别: 开始";
    
    // 自动清空之前的数据
    onClearResults();
    
    const QClipboard* clipboard = QApplication::clipboard();
    const QMimeData* mimeData = clipboard->mimeData();

    QList<QPair<QImage, QString>> imageData;

    if (!mimeData) return;

    if (mimeData->hasImage()) {
        QImage img = qvariant_cast<QImage>(mimeData->imageData());
        if (!img.isNull()) {
            imageData.append({img, "粘贴的图片"});
        }
    } else if (mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            QString path = url.toLocalFile();
            if (!path.isEmpty()) {
                QImage img(path);
                if (!img.isNull()) {
                    imageData.append({img, QFileInfo(path).fileName()});
                }
            }
        }
    }

    if (!imageData.isEmpty()) {
        // 限制最多 10 张图片
        if (imageData.size() > 10) {
            qDebug() << "[OCR] 粘贴识别: 图片数量超过限制，仅处理前 10 张";
            imageData = imageData.mid(0, 10);
        }
        
        qDebug() << "[OCR] 粘贴识别: 开始处理" << imageData.size() << "张图片";
        QList<QImage> imgs;
        for (auto& p : imageData) {
            OCRItem item;
            item.image = p.first;
            item.name = p.second;
            // 【核心修复】使用负数 ID 作为临时任务，避免与数据库中的 noteId 冲突导致误更新
            item.id = -(++m_lastUsedId);
            item.sessionVersion = m_sessionVersion;
            m_items.append(item);
            imgs << p.first;
            qDebug() << "[OCR] 添加任务 ID:" << item.id << "名称:" << item.name;

            auto* listItem = new QListWidgetItem(item.name, m_itemList);
            listItem->setData(Qt::UserRole, item.id);
            listItem->setIcon(IconHelper::getIcon("image", "#888"));
        }
        processImages(imgs);
        
        // 自动选中第一个新加入的项目
        m_itemList->setCurrentRow(m_itemList->count() - imageData.size());
    }
}

void OCRWindow::onBrowseAndRecognize() {
    QStringList files = QFileDialog::getOpenFileNames(this, "选择识别图片", "", "图片文件 (*.png *.jpg *.jpeg *.bmp *.gif)");
    if (files.isEmpty()) return;

    // 自动清空之前的数据
    onClearResults();

    qDebug() << "[OCR] 浏览识别: 选择了" << files.size() << "个文件";
    
    // 限制最多 10 张图片
    if (files.size() > 10) {
        qDebug() << "[OCR] 浏览识别: 文件数量超过限制，仅处理前 10 个";
        files = files.mid(0, 10);
    }
    
    QList<QImage> imgs;
    for (const QString& file : std::as_const(files)) {
        QImage img(file);
        if (!img.isNull()) {
            OCRItem item;
            item.image = img;
            item.name = QFileInfo(file).fileName();
            // 【核心修复】使用负数 ID
            item.id = -(++m_lastUsedId);
            item.sessionVersion = m_sessionVersion;
            m_items.append(item);
            imgs << img;
            qDebug() << "[OCR] 添加任务 ID:" << item.id << "文件:" << file;

            auto* listItem = new QListWidgetItem(item.name, m_itemList);
            listItem->setData(Qt::UserRole, item.id);
            listItem->setIcon(IconHelper::getIcon("image", "#888"));
        }
    }

    if (!imgs.isEmpty()) {
        processImages(imgs);
        m_itemList->setCurrentRow(m_itemList->count() - imgs.size());
    }
}

void OCRWindow::onClearResults() {
    qDebug() << "[OCR] 清空结果";
    
    m_isProcessing = false;
    m_processingQueue.clear();
    m_sessionVersion++; // 递增版本号，使旧的回调失效
    
    m_itemList->clear();
    m_items.clear();
    m_ocrResult->clear();
    
    // 重置进度条
    if (m_progressBar) {
        m_progressBar->hide();
        m_progressBar->setValue(0);
    }
    
    // 不重置 m_lastUsedId，防止异步回调 ID 冲突
    
    auto* summaryItem = new QListWidgetItem("--- 全部结果汇总 ---", m_itemList);
    summaryItem->setData(Qt::UserRole, 0); // 0 代表汇总
    summaryItem->setIcon(IconHelper::getIcon("file_managed", "#1abc9c"));
    m_itemList->setCurrentItem(summaryItem);
}

void OCRWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasImage() || event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void OCRWindow::dropEvent(QDropEvent* event) {
    qDebug() << "[OCR] 拖入识别: 开始";
    
    // 自动清空之前的数据
    onClearResults();
    
    const QMimeData* mime = event->mimeData();
    QList<QImage> imgsToProcess;

    // 限制最多 10 张图片
    int imageCount = 0;
    const int MAX_IMAGES = 10;

    if (mime->hasImage()) {
        QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull() && imageCount < MAX_IMAGES) {
            OCRItem item;
            item.image = img;
            item.name = "拖入的图片";
            // 【核心修复】使用负数 ID
            item.id = -(++m_lastUsedId);
            item.sessionVersion = m_sessionVersion;
            m_items.append(item);
            imgsToProcess << img;
            imageCount++;

            auto* listItem = new QListWidgetItem(item.name, m_itemList);
            listItem->setData(Qt::UserRole, item.id);
            listItem->setIcon(IconHelper::getIcon("image", "#888"));
        }
    }

    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (imageCount >= MAX_IMAGES) {
                qDebug() << "[OCR] 拖入识别: 已达到最大图片数量限制 (10 张)";
                break;
            }
            
            QString path = url.toLocalFile();
            if (!path.isEmpty()) {
                QImage img(path);
                if (!img.isNull()) {
                    OCRItem item;
                    item.image = img;
                    item.name = QFileInfo(path).fileName();
                    // 【核心修复】使用负数 ID
                    item.id = -(++m_lastUsedId);
                    item.sessionVersion = m_sessionVersion;
                    m_items.append(item);
                    imgsToProcess << img;
                    imageCount++;

                    auto* listItem = new QListWidgetItem(item.name, m_itemList);
                    listItem->setData(Qt::UserRole, item.id);
                    listItem->setIcon(IconHelper::getIcon("image", "#888"));
                }
            }
        }
    }

    if (!imgsToProcess.isEmpty()) {
        processImages(imgsToProcess);
        m_itemList->setCurrentRow(m_itemList->count() - imgsToProcess.size());
        event->acceptProposedAction();
    }
}

void OCRWindow::processImages(const QList<QImage>& images) {
    qDebug() << "[OCR] processImages: 添加" << images.size() << "张图片到队列";
    
    m_processingQueue.clear(); // 清空旧队列
    
    // 将所有图片添加到队列
    int startIdx = m_items.size() - images.size();
    for (int i = 0; i < images.size(); ++i) {
        int taskId = m_items[startIdx + i].id;
        m_processingQueue.enqueue(taskId);
        qDebug() << "[OCR] 添加到队列 ID:" << taskId;
    }
    
    // 显示并初始化进度条
    if (m_progressBar && !images.isEmpty()) {
        m_progressBar->setMaximum(images.size()); // 仅当前批次
        m_progressBar->setValue(0);
        m_progressBar->show();
    }
    
    // 如果当前没有在处理，立即开始处理
    if (!m_isProcessing) {
        qDebug() << "[OCR] 开始顺序处理";
        processNextImage();
    }
}

void OCRWindow::processNextImage() {
    // 检查队列是否为空
    if (m_processingQueue.isEmpty()) {
        qDebug() << "[OCR] 所有任务处理完成";
        m_isProcessing = false;
        updateRightDisplay();
        return;
    }
    
    m_isProcessing = true;
    int taskId = m_processingQueue.dequeue();
    
    qDebug() << "[OCR] 开始处理 ID:" << taskId << "剩余:" << m_processingQueue.size();
    
    // 找到对应的图片
    OCRItem* item = nullptr;
    for (auto& it : m_items) {
        if (it.id == taskId) {
            item = &it;
            break;
        }
    }
    
    if (!item) {
        qDebug() << "[OCR] 错误: 未找到任务 ID:" << taskId;
        // 继续处理下一个
        QMetaObject::invokeMethod(this, &OCRWindow::processNextImage, Qt::QueuedConnection);
        return;
    }
    
    // 启动异步识别
    // 注意：这里我们使用 recognizeAsync，它会在后台线程运行
    // 识别完成后会发射 recognitionFinished 信号，我们在槽函数中触发下一个任务
    qDebug() << "[OCR] 调用 recognizeAsync ID:" << taskId;
    OCRManager::instance().recognizeAsync(item->image, taskId);
}


void OCRWindow::onItemSelectionChanged() {
    updateRightDisplay();
}

void OCRWindow::onRecognitionFinished(const QString& text, int contextId) {
    qDebug() << "[OCR] onRecognitionFinished: 收到识别结果 ID:" << contextId 
             << "线程:" << QThread::currentThread() << "文本长度:" << text.length();
    
    bool found = false;
    for (auto& item : m_items) {
        if (item.id == contextId) {
            // 检查会话版本号
            if (item.sessionVersion != m_sessionVersion) {
                qDebug() << "[OCR] 忽略过期回调 ID:" << contextId 
                         << "任务会话:" << item.sessionVersion 
                         << "当前会话:" << m_sessionVersion;
                // 注意：旧任务的回调不应该触发下一个新任务的处理
                // 因为新任务的处理循环是由新的 processImages 启动的
                return; 
            }
            
            item.result = text.trimmed();
            item.isFinished = true;
            found = true;
            qDebug() << "[OCR] 更新任务状态 ID:" << contextId << "名称:" << item.name;
            break;
        }
    }
    
    if (!found) {
        // 如果未找到任务（可能已被清空），也不要触发下一个
        qDebug() << "[OCR] 警告: 未找到对应的任务 ID:" << contextId;
        return;
    }
    
    // 统计完成进度
    int finished = 0;
    for (const auto& item : std::as_const(m_items)) {
        if (item.isFinished) finished++;
    }
    qDebug() << "[OCR] 识别进度:" << finished << "/" << m_items.size();
    
    // 更新进度条
    if (m_progressBar) {
        m_progressBar->setValue(finished);
        if (finished >= m_items.size()) {
            QTimer::singleShot(700, m_progressBar, &QProgressBar::hide); // 完成1秒后隐藏
        }
    }
    
    // 延迟到主线程更新UI
    QMetaObject::invokeMethod(this, &OCRWindow::updateRightDisplay, Qt::QueuedConnection);
    
    // 延迟到下一个事件循环处理任务，避免栈溢出
    qDebug() << "[OCR] 当前任务完成，准备处理下一个";
    QMetaObject::invokeMethod(this, &OCRWindow::processNextImage, Qt::QueuedConnection);
}

void OCRWindow::updateRightDisplay() {
    // 确保在主线程执行
    if (QThread::currentThread() != this->thread()) {
        QMetaObject::invokeMethod(this, &OCRWindow::updateRightDisplay, Qt::QueuedConnection);
        return;
    }

    qDebug() << "[OCR] updateRightDisplay: 开始更新显示 线程:" << QThread::currentThread();
    
    if (!m_itemList) {
        qDebug() << "[OCR] updateRightDisplay: m_itemList 为空";
        return;
    }
    
    if (!m_ocrResult) {
        qDebug() << "[OCR] updateRightDisplay: m_ocrResult 为空";
        return;
    }
    
    auto* current = m_itemList->currentItem();
    if (!current) {
        qDebug() << "[OCR] updateRightDisplay: 没有选中项";
        return;
    }

    int id = current->data(Qt::UserRole).toInt();
    qDebug() << "[OCR] updateRightDisplay: 当前选中 ID:" << id << "m_items 大小:" << m_items.size();

    if (id == 0) {
        // 展示全部
        qDebug() << "[OCR] updateRightDisplay: 展示全部结果";
        QString allText;
        int itemCount = 0;
        for (const auto& item : std::as_const(m_items)) {
            itemCount++;
            if (!allText.isEmpty()) allText += "\n\n";
            allText += QString("【%1】\n").arg(item.name);
            allText += item.isFinished ? item.result : "正在识别管理中...";
            allText += "\n-----------------------------------";
        }
        qDebug() << "[OCR] updateRightDisplay: 处理了" << itemCount << "个项目，文本长度:" << allText.length();
        m_ocrResult->setPlainText(allText);
        qDebug() << "[OCR] updateRightDisplay: setPlainText 完成";
    } else {
        // 展示单个
        qDebug() << "[OCR] updateRightDisplay: 展示单个结果 ID:" << id;
        bool found = false;
        for (const auto& item : std::as_const(m_items)) {
            if (item.id == id) {
                found = true;
                QString text = item.isFinished ? item.result : "正在识别中，请稍候...";
                qDebug() << "[OCR] updateRightDisplay: 找到项目，文本长度:" << text.length();
                m_ocrResult->setPlainText(text);
                qDebug() << "[OCR] updateRightDisplay: setPlainText 完成";
                break;
            }
        }
        if (!found) {
            qDebug() << "[OCR] updateRightDisplay: 警告 - 未找到 ID:" << id;
        }
    }
    qDebug() << "[OCR] updateRightDisplay: 完成";
}

void OCRWindow::onCopyResult() {
    QString text = m_ocrResult->toPlainText();
    if (!text.isEmpty()) {
        // [CRITICAL] 明确标记为 ocr_text 类型，确保通过识别提取的文字入库后显示扫描图标
        ClipboardMonitor::instance().forceNext("ocr_text");
        QApplication::clipboard()->setText(text);
    }
}

void OCRWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 全局策略：OCR 窗口按下 Esc 不再直接关闭
        event->accept();
        return;
    }
    FramelessDialog::keyPressEvent(event);
}

