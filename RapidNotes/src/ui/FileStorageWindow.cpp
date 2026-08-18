#include "FileStorageWindow.h"
#include "IconHelper.h"
#include "../core/DatabaseManager.h"
#include "../core/FileStorageHelper.h"
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMouseEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <utility>
#include <QApplication>
#include <QCoreApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QFileDialog>
#include <QMenu>
#include <QDateTime>
#include <QDebug>

FileStorageWindow::FileStorageWindow(QWidget* parent) : FramelessDialog("存储文件", parent) {
    setObjectName("FileStorageWindow");
    loadWindowSettings();
    setAcceptDrops(true);
    resize(450, 430);

    initUI();
}

void FileStorageWindow::initUI() {
    auto* contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(20, 10, 20, 20);
    contentLayout->setSpacing(10);

    // Drop Area
    m_dropHint = new QPushButton("拖拽文件或文件夹到这里\n数据将完整拷贝至存储库");
    m_dropHint->setObjectName("DropArea");
    m_dropHint->setStyleSheet("QPushButton#DropArea { color: #888; font-size: 12px; border: 2px dashed #444; border-radius: 8px; padding: 20px; background: #181818; outline: none; } "
                               "QPushButton#DropArea:hover { border-color: #f1c40f; color: #f1c40f; background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
    m_dropHint->setFixedHeight(100);
    connect(m_dropHint, &QPushButton::clicked, this, &FileStorageWindow::onSelectItems);
    contentLayout->addWidget(m_dropHint);

    // Status List
    m_statusList = new QListWidget();
    m_statusList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_statusList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_statusList->setStyleSheet("QListWidget { background-color: #252526; border: 1px solid #333; border-radius: 6px; color: #BBB; padding: 5px; font-size: 11px; } "
                                "QListWidget::item { padding: 4px; border-bottom: 1px solid #2d2d2d; }");
    contentLayout->addWidget(m_statusList);

    auto* tipLabel = new QLabel("文件将直接复制到 attachments 文件夹");
    tipLabel->setStyleSheet("color: #666; font-size: 10px;");
    tipLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(tipLabel);
}


// ==========================================
// 2. 核心存储逻辑
// ==========================================

void FileStorageWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        m_dropHint->setStyleSheet("QPushButton#DropArea { color: #f1c40f; font-size: 12px; border: 2px dashed #f1c40f; border-radius: 8px; padding: 20px; background-color: rgba(241, 196, 15, 0.05); }");
    }
}

void FileStorageWindow::dragLeaveEvent(QDragLeaveEvent* event) {
    Q_UNUSED(event);
    m_dropHint->setStyleSheet("QPushButton#DropArea { color: #888; font-size: 12px; border: 2px dashed #444; border-radius: 8px; padding: 20px; background: #181818; outline: none; }");
}

void FileStorageWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) paths << url.toLocalFile();
        }
        
        if (!paths.isEmpty()) {
            processStorage(paths);
        }
    }
    m_dropHint->setStyleSheet("QPushButton#DropArea { color: #888; font-size: 12px; border: 2px dashed #444; border-radius: 8px; padding: 20px; background: #181818; outline: none; }");
}

void FileStorageWindow::processStorage(const QStringList& paths) {
    m_statusList->clear();
    if (paths.isEmpty()) return;

    m_statusList->addItem("📦 正在导入 " + QString::number(paths.size()) + " 个项目...");
    QApplication::processEvents();

    int count = FileStorageHelper::processImport(paths, m_categoryId);
    
    if (count > 0) {
        m_statusList->addItem(QString("[OK] 成功导入 %1 个项目").arg(count));
    } else {
        m_statusList->addItem("[ERR] 导入失败");
    }
}

void FileStorageWindow::storeFile(const QString& path) {
    processStorage({path});
}

void FileStorageWindow::storeFolder(const QString& path) {
    processStorage({path});
}

void FileStorageWindow::storeArchive(const QStringList& paths) {
    processStorage(paths);
}


void FileStorageWindow::onSelectItems() {
    QMenu menu(this);
    IconHelper::setupMenu(&menu);
    menu.setStyleSheet("QMenu { background-color: #2D2D2D; color: #EEE; border: 1px solid #444; padding: 4px; } "
                       "QMenu::item { padding: 6px 20px; border-radius: 3px; } "
                       "QMenu::item:selected { background-color: #3e3e42; color: white; }"); // 2026-03-xx 统一菜单悬停色为 #3e3e42

    menu.addAction("选择并存入文件...", [this]() {
        QStringList files = QFileDialog::getOpenFileNames(this, "选择文件", "", "所有文件 (*.*)");
        if (!files.isEmpty()) {
            processStorage(files);
        }
    });

    menu.addAction("选择并存入文件夹...", [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "选择文件夹", "");
        if (!dir.isEmpty()) {
            processStorage({dir});
        }
    });

    menu.exec(QCursor::pos());
}
