#include "FileSearchWindow.h"
#include "FileSearchWidget.h"
#include "ResizeHandle.h"
#include <QVBoxLayout>
#include <QKeyEvent>

FileSearchWindow::FileSearchWindow(QWidget* parent) : FramelessDialog("查找文件", parent) {
    resize(1000, 600);
    m_searchWidget = new FileSearchWidget(m_contentArea);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchWidget);
}

void FileSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}

void FileSearchWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 两段式：如果路径或搜索框不为空，则清空返回。
        // 注意：由于 FileSearchWidget 内部已经处理了 Focus 状态下的 Esc (clearFocus)，
        // 这里主要处理焦点不在输入框但用户想通过一次 Esc 清空筛选结果的情况。
        if (m_searchWidget->currentPath() != "") {
             m_searchWidget->clearAllInputs();
             event->accept();
             return;
        }
    }
    FramelessDialog::keyPressEvent(event);
}
