#include "AddressBar.h"
#include "UiHelper.h"
#include <QHBoxLayout>
#include <QDir>
#include <QPushButton>

namespace ArcMeta {

AddressBar::AddressBar(QWidget* parent) : QWidget(parent) {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_pathStack = new QStackedWidget(this);
    m_pathStack->setFixedHeight(32); 
    m_pathStack->setMinimumWidth(300);
    m_pathStack->setStyleSheet("QStackedWidget { background: #1E1E1E; border: 1px solid #333333; border-radius: 6px; }");

    m_breadcrumbBar = new BreadcrumbBar(m_pathStack);
    m_pathStack->addWidget(m_breadcrumbBar);

    m_pathEdit = new QLineEdit(m_pathStack);
    m_pathEdit->setPlaceholderText("输入路径...");
    m_pathEdit->setFixedHeight(30); // 扣除上下各 1px 边框，确保背景不溢出圆角
    m_pathEdit->setStyleSheet("QLineEdit { background: transparent; border: none; color: #EEEEEE; padding-left: 8px; }");
    m_pathStack->addWidget(m_pathEdit);

    layout->addWidget(m_pathStack);

    m_btnRefresh = new QPushButton(this);
    m_btnRefresh->setFixedSize(30, 30);
    m_btnRefresh->setIcon(UiHelper::getIcon("sync", QColor("#CCCCCC"), 16));
    m_btnRefresh->setToolTip("刷新 (F5)");
    m_btnRefresh->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: #FFFFFF1A; }"
        "QPushButton:pressed { background: #FFFFFF33; }"
    );
    layout->addSpacing(4);
    layout->addWidget(m_btnRefresh);

    connect(m_btnRefresh, &QPushButton::clicked, this, &AddressBar::refreshRequested);
    connect(m_breadcrumbBar, &BreadcrumbBar::blankAreaClicked, this, &AddressBar::onBreadcrumbBlankClicked);
    connect(m_pathEdit, &QLineEdit::editingFinished, this, &AddressBar::onPathEditFinished);
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() {
        QString input = m_pathEdit->text();
        if (QDir(input).exists() || input == "computer://" || input == "此电脑") {
            emit pathChanged(input == "此电脑" ? "computer://" : input);
        } else {
            m_pathEdit->setText(QDir::toNativeSeparators(m_currentPath));
            m_pathStack->setCurrentWidget(m_breadcrumbBar);
        }
    });
    connect(m_breadcrumbBar, &BreadcrumbBar::pathClicked, this, &AddressBar::onBreadcrumbClicked);

    m_pathStack->installEventFilter(this);

    m_historyPanel = new AddressHistoryPanel(this);
    connect(m_historyPanel, &AddressHistoryPanel::historyItemClicked, this, [this](const QString& path) {
        emit pathChanged(path);
        m_historyPanel->hide();
    });
    connect(m_historyPanel, &AddressHistoryPanel::historyItemRemoved, this, [this](const QString& path) {
        QStringList history = AppConfig::instance().getValue("AddressBar/History").toStringList();
        history.removeAll(path);
        AppConfig::instance().setValue("AddressBar/History", history);
        m_historyPanel->setHistory(history);
    });
    connect(m_historyPanel, &AddressHistoryPanel::clearAllRequested, this, [this]() {
        AppConfig::instance().setValue("AddressBar/History", QStringList());
        m_historyPanel->setHistory(QStringList());
    });
}

void AddressBar::setPath(const QString& path) {
    m_currentPath = path;
    QString displayPath = (path == "computer://") ? "此电脑" : QDir::toNativeSeparators(path);
    m_pathEdit->setText(displayPath);
    m_breadcrumbBar->setPath(path);
    m_pathStack->setCurrentWidget(m_breadcrumbBar);
    saveToHistory(path);
}

void AddressBar::saveToHistory(const QString& path) {
    if (path.isEmpty() || path == "computer://" || path.startsWith("分类: ")) return;
    QStringList history = AppConfig::instance().getValue("AddressBar/History").toStringList();
    history.removeAll(path);
    history.prepend(path);
    while (history.size() > 15) history.removeLast();
    AppConfig::instance().setValue("AddressBar/History", history);
}

void AddressBar::onBreadcrumbBlankClicked() {
    m_pathEdit->setText(QDir::toNativeSeparators(m_currentPath));
    m_pathStack->setCurrentWidget(m_pathEdit);
    m_pathEdit->setFocus();
    m_pathEdit->selectAll();
}

void AddressBar::onPathEditFinished() {
    if (m_pathStack->currentWidget() == m_pathEdit) {
        m_pathStack->setCurrentWidget(m_breadcrumbBar);
    }
}

void AddressBar::onBreadcrumbClicked(const QString& path) {
    emit pathChanged(path);
}

bool AddressBar::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_pathStack && event->type() == QEvent::MouseButtonDblClick) {
        QStringList history = AppConfig::instance().getValue("AddressBar/History").toStringList();
        m_historyPanel->setHistory(history);
        m_historyPanel->showBelow(m_pathStack);
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace ArcMeta
