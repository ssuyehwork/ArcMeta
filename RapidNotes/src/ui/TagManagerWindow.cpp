#include "ToolTipOverlay.h"
#include "TagManagerWindow.h"
#include "StringUtils.h"

#include "IconHelper.h"
#include "../core/DatabaseManager.h"
#include "FramelessDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QSet>
#include <utility>

TagManagerWindow::TagManagerWindow(QWidget* parent) : FramelessDialog("标签管理", parent) {
    setObjectName("TagManagerWindow");

    
    
    initUI();
    refreshData();

    if (m_maxBtn) m_maxBtn->hide();

    loadWindowSettings();
    resize(430, 580);
}

TagManagerWindow::~TagManagerWindow() {
}

void TagManagerWindow::initUI() {
    auto* contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(20, 10, 20, 20);
    contentLayout->setSpacing(12);

    // Search Bar
    m_searchEdit = new QLineEdit();
    m_searchEdit->setPlaceholderText("搜索标签...");
    m_searchEdit->setStyleSheet("QLineEdit { background-color: #2D2D2D; border: 1px solid #444; border-radius: 6px; color: white; padding: 6px 10px; font-size: 13px; } "
                               "QLineEdit:focus { border-color: #f1c40f; }");
    connect(m_searchEdit, &QLineEdit::textChanged, this, &TagManagerWindow::handleSearch);
    contentLayout->addWidget(m_searchEdit);

    // Table
    m_tagTable = new QTableWidget(0, 2);
    m_tagTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tagTable->setHorizontalHeaderLabels({"标签名称", "使用次数"});
    m_tagTable->horizontalHeader()->setStretchLastSection(false);
    m_tagTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tagTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tagTable->verticalHeader()->setVisible(false);

    m_tagTable->verticalHeader()->setDefaultSectionSize(32);
    m_tagTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tagTable->setSelectionMode(QAbstractItemView::ExtendedSelection);

    m_tagTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_tagTable->setStyleSheet(
        "QTableWidget { background-color: #252526; border: 1px solid #333; border-radius: 6px; color: #CCC; gridline-color: #333; outline: none; } "
        "QTableWidget::item { padding: 5px; } "
        "QTableWidget::item:selected { background-color: #3e3e42; color: #FFF; } "
        "QTableWidget QLineEdit { background-color: #1e1e1e; color: white; border: 1px solid #4a90e2; border-radius: 2px; padding: 0px 5px; height: 26px; } "
        "QHeaderView::section { background-color: #2D2D30; color: #888; border: none; height: 30px; font-weight: bold; font-size: 12px; border-bottom: 1px solid #333; }"
    );
    connect(m_tagTable, &QTableWidget::itemChanged, this, &TagManagerWindow::onTagItemChanged);
    contentLayout->addWidget(m_tagTable);

    // Action Buttons
    auto* btnLayout = new QHBoxLayout();

    auto* btnRename = new QPushButton("重命名");
    btnRename->setStyleSheet("QPushButton { background-color: #333; color: #EEE; border: none; border-radius: 4px; padding: 8px 15px; font-weight: bold; } "
                             "QPushButton:hover { background-color: #3e3e42; }");
    connect(btnRename, &QPushButton::clicked, this, &TagManagerWindow::handleRename);
    btnLayout->addWidget(btnRename);

    auto* btnDelete = new QPushButton("删除");
    btnDelete->setStyleSheet("QPushButton { background-color: #e74c3c; color: white; border: none; border-radius: 4px; padding: 8px 15px; font-weight: bold; } "
                             "QPushButton:hover { background-color: #3e3e42; }");
    connect(btnDelete, &QPushButton::clicked, this, &TagManagerWindow::handleDelete);
    btnLayout->addWidget(btnDelete);

    contentLayout->addLayout(btnLayout);
}

void TagManagerWindow::refreshData() {

    m_tagTable->blockSignals(true);
    m_tagTable->setRowCount(0);

    QVariantMap filterStats = DatabaseManager::instance().getFilterStats();
    QVariantMap tagStats = filterStats.value("tags").toMap();

    QString keyword = m_searchEdit->text().trimmed().toLower();

    // Sort by name
    QStringList tagNames = tagStats.keys();
    tagNames.sort();

    for (const QString& name : std::as_const(tagNames)) {
        if (!keyword.isEmpty() && !name.toLower().contains(keyword)) continue;

        int row = m_tagTable->rowCount();
        m_tagTable->insertRow(row);

        auto* nameItem = new QTableWidgetItem(name);

        nameItem->setData(Qt::UserRole, name);

        auto* countItem = new QTableWidgetItem(tagStats.value(name).toString());
        countItem->setTextAlignment(Qt::AlignCenter);

        countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);

        m_tagTable->setItem(row, 0, nameItem);
        m_tagTable->setItem(row, 1, countItem);
    }
    m_tagTable->blockSignals(false);
}

void TagManagerWindow::handleRename() {
    int row = m_tagTable->currentRow();
    if (row < 0) return;

    m_tagTable->editItem(m_tagTable->item(row, 0));
}

void TagManagerWindow::onTagItemChanged(QTableWidgetItem* item) {
    if (!item || item->column() != 0) return;

    QString oldName = item->data(Qt::UserRole).toString();
    QString newName = item->text().trimmed();

    if (newName.isEmpty() || newName == oldName) {
        
        m_tagTable->blockSignals(true);
        item->setText(oldName);
        m_tagTable->blockSignals(false);
        return;
    }

    
    if (DatabaseManager::instance().renameTagGlobally(oldName, newName)) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 标签已重命名并同步至所有笔记</b>");
        
        refreshData();
    } else {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 重命名失败</b>");
        m_tagTable->blockSignals(true);
        item->setText(oldName);
        m_tagTable->blockSignals(false);
    }
}

void TagManagerWindow::handleDelete() {
    auto selectedItems = m_tagTable->selectedItems();
    if (selectedItems.isEmpty()) return;

    QStringList tagNames;
    // selectedItems contains items for all columns, we only need column 0
    QSet<int> rows;
    for (auto* item : selectedItems) rows.insert(item->row());

    for (int row : rows) {
        tagNames << m_tagTable->item(row, 0)->text();
    }

    if (tagNames.isEmpty()) return;

    QString confirmMsg = tagNames.size() == 1
        ? QString("确定要从所有笔记中移除标签 '%1' 吗？").arg(tagNames.first())
        : QString("确定要从所有笔记中移除选中的 %1 个标签吗？").arg(tagNames.size());

    auto* dlg = new FramelessMessageBox("确认删除", confirmMsg, this);
    connect(dlg, &FramelessMessageBox::confirmed, [this, tagNames](){
        int successCount = 0;
        for (const QString& tagName : tagNames) {
            if (DatabaseManager::instance().deleteTagGlobally(tagName)) {
                successCount++;
            }
        }
        if (successCount > 0) {
            ToolTipOverlay::instance()->showText(QCursor::pos(), tagNames.size() == 1 ? "<b style='color: #2ecc71;'>[OK] 标签已移除</b>" : QString("<b style='color: #2ecc71;'>[OK] 已批量移除 %1 个标签</b>").arg(successCount));
            refreshData();
        } else {
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[ERR] 移除失败，请检查数据库连接</b>");
        }
    });
    dlg->show();
}

void TagManagerWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {

        if (!m_searchEdit->text().isEmpty()) {
            m_searchEdit->clear();
            event->accept();
            return;
        }
        close();
        return;
    }

    if (event->key() == Qt::Key_F2) {
        handleRename();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete) {
        handleDelete();
        event->accept();
    } else {
        FramelessDialog::keyPressEvent(event);
    }
}

bool TagManagerWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            m_searchEdit->setCursorPosition(0);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            m_searchEdit->setCursorPosition(m_searchEdit->text().length());
            return true;
        }
    }
    return FramelessDialog::eventFilter(watched, event);
}

void TagManagerWindow::handleSearch(const QString& text) {
    refreshData();
}

