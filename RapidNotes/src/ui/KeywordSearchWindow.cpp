#include "KeywordSearchWindow.h"
#include "KeywordSearchWidget.h"
#include <QVBoxLayout>
#include <QKeyEvent>

KeywordSearchWindow::KeywordSearchWindow(QWidget* parent) : FramelessDialog("关键字查找", parent) {
    resize(1100, 750);
    m_searchWidget = new KeywordSearchWidget(m_contentArea);
    auto* layout = new QVBoxLayout(m_contentArea);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_searchWidget);
}

void KeywordSearchWindow::resizeEvent(QResizeEvent* event) {
    FramelessDialog::resizeEvent(event);
}

void KeywordSearchWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 两段式：如果内容存在则清空，否则关闭
        if (m_searchWidget->currentPath() != "") {
            m_searchWidget->clearAllInputs();
            event->accept();
            return;
        }
    }
    FramelessDialog::keyPressEvent(event);
}
