#include "AdvancedTagSelector.h"
#include "IconHelper.h"
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QGraphicsDropShadowEffect>
#include <QScrollArea>
#include <QTimer>
#include <QRegularExpression>

AdvancedTagSelector::AdvancedTagSelector(QWidget* parent) 
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) 
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    
    // 1:1 匹配 QuickWindow 的整体架构
    setFixedSize(400, 500); 

    auto* mainLayout = new QVBoxLayout(this);
    // [CRITICAL] 边距调整为 20px 以容纳阴影，防止出现“断崖式”阴影截止
    mainLayout->setContentsMargins(20, 20, 20, 20); 
    mainLayout->setSpacing(0);

    // 内部容器
    auto* container = new QWidget();
    container->setObjectName("container");
    container->setMouseTracking(true);
    container->setStyleSheet(
        "QWidget#container { background: #1E1E1E; border-radius: 10px; border: 1px solid #333; }"
    );

    // 阴影效果: 严格 1:1 复制 QuickWindow 参数 (同步修复模糊截止问题)
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 120));
    shadow->setOffset(0, 4);
    container->setGraphicsEffect(shadow);

    mainLayout->addWidget(container);

    // 容器内部布局
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    // 1. 搜索框
    m_search = new QLineEdit();
    m_search->installEventFilter(this);
    m_search->setPlaceholderText("搜索或新建...");
    m_search->setStyleSheet(
        "QLineEdit {"
        "  background-color: #2D2D2D;"
        "  border: none;"
        "  border-bottom: 1px solid #444;"
        "  border-radius: 4px;"
        "  padding: 8px;"
        "  font-size: 13px;"
        "  color: #DDD;"
        "}"
        "QLineEdit:focus { border-bottom: 1px solid #4a90e2; }"
    );
    
    connect(m_search, &QLineEdit::textChanged, this, &AdvancedTagSelector::updateList);
    connect(m_search, &QLineEdit::returnPressed, this, [this](){
        QString text = m_search->text().trimmed();
        if (!text.isEmpty()) {
            // [CRITICAL] 支持使用中英文逗号分割标签，提升批量输入体验
            QStringList newTags = text.split(QRegularExpression("[,，]"), Qt::SkipEmptyParts);
            bool changed = false;
            for (QString& tag : newTags) {
                tag = tag.trimmed();
                if (!tag.isEmpty() && !m_selected.contains(tag)) {
                    m_selected.append(tag);
                    changed = true;
                }
            }
            
            if (changed) {
                emit tagsChanged();
            }
            // 关键：先清空搜索框，再刷新列表，确保新添加的标签出现在“最近使用”列表首位
            m_search->clear(); 
            updateList();
        }
    });
    layout->addWidget(m_search);

    // 2. 提示标签
    m_tipsLabel = new QLabel("最近使用");
    m_tipsLabel->setStyleSheet("color: #888; font-size: 12px; font-weight: bold; margin-top: 5px;");
    layout->addWidget(m_tipsLabel);

    // 3. 滚动区域
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { width: 6px; background: transparent; }"
        "QScrollBar::handle:vertical { background: #444; border-radius: 3px; }"
    );

    m_tagContainer = new QWidget();
    m_tagContainer->setStyleSheet("background: transparent;");
    
    m_flow = new FlowLayout(m_tagContainer, 0, 8, 8);
    scroll->setWidget(m_tagContainer);
    layout->addWidget(scroll);
}

void AdvancedTagSelector::setup(const QList<QVariantMap>& recentTags, const QStringList& allTags, const QStringList& selectedTags) {
    m_recentTags = recentTags;
    m_allTags = allTags;
    m_selected = selectedTags;
    updateList();
}

void AdvancedTagSelector::setTags(const QStringList& allTags, const QStringList& selectedTags) {
    m_allTags = allTags;
    m_recentTags.clear();
    for (const QString& t : allTags) {
        QVariantMap m;
        m["name"] = t;
        m["count"] = 0;
        m_recentTags.append(m);
    }
    m_selected = selectedTags;
    updateList();
}

void AdvancedTagSelector::updateList() {
    // 清空现有项
    QLayoutItem* child;
    while ((child = m_flow->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    QString filter = m_search->text().trimmed();
    QString filterLower = filter.toLower();
    
    QList<QVariantMap> displayList;

    if (filter.isEmpty()) {
        m_tipsLabel->setText(QString("最近使用 (%1)").arg(m_recentTags.count()));
        
        // 整理显示列表：优先显示已选中的标签（新操作的排在最前）
        QStringList seen;
        
        // 1. 已选中的标签（按最后添加顺序逆序，最新添加的在第一位）
        for (int i = m_selected.size() - 1; i >= 0; --i) {
            QString name = m_selected[i];
            if (!seen.contains(name)) {
                QVariantMap m;
                m["name"] = name;
                int count = 0;
                for (const auto& rm : m_recentTags) {
                    if (rm["name"].toString() == name) {
                        count = rm["count"].toInt();
                        break;
                    }
                }
                m["count"] = count;
                displayList.append(m);
                seen << name;
            }
        }

        // 2. 数据库返回的其他最近标签
        for (const auto& rm : m_recentTags) {
            QString name = rm["name"].toString();
            if (!seen.contains(name)) {
                displayList.append(rm);
                seen << name;
            }
        }
    } else {
        // 搜索模式
        for (const QString& tag : m_allTags) {
            if (tag.toLower().contains(filterLower)) {
                QVariantMap m;
                m["name"] = tag;
                int count = 0;
                for (const auto& rm : m_recentTags) {
                    if (rm["name"].toString() == tag) {
                        count = rm["count"].toInt();
                        break;
                    }
                }
                m["count"] = count;
                displayList.append(m);
            }
        }
        m_tipsLabel->setText(QString("搜索结果 (%1)").arg(displayList.count()));
    }

    for (const auto& tagData : displayList) {
        QString tag = tagData["name"].toString();
        int count = tagData["count"].toInt();

        bool isSelected = m_selected.contains(tag);
        
        auto* btn = new QPushButton();
        btn->setCheckable(true);
        btn->setChecked(isSelected);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setProperty("tag_name", tag);
        btn->setProperty("tag_count", count);
        
        updateChipState(btn, isSelected);
        
        connect(btn, &QPushButton::clicked, this, [this, btn, tag](){ 
            toggleTag(tag); 
        });
        m_flow->addWidget(btn);
    }
}

void AdvancedTagSelector::updateChipState(QPushButton* btn, bool checked) {
    QString name = btn->property("tag_name").toString();
    int count = btn->property("tag_count").toInt();
    
    QString text = name;
    if (count > 0) text += QString(" (%1)").arg(count);
    btn->setText(text);
    
    QIcon icon = checked ? IconHelper::getIcon("select", "#ffffff", 14) 
                         : IconHelper::getIcon("clock", "#bbbbbb", 14);
    btn->setIcon(icon);
    btn->setIconSize(QSize(14, 14));

    if (checked) {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #4a90e2;"
            "  color: white;"
            "  border: 1px solid #4a90e2;"
            "  border-radius: 14px;"
            "  padding: 6px 12px;"
            "  font-size: 12px;"
            "  font-family: 'Segoe UI', 'Microsoft YaHei';"
            "}"
        );
    } else {
        btn->setStyleSheet(
            "QPushButton {"
            "  background-color: #2D2D2D;"
            "  color: #BBB;"
            "  border: 1px solid #444;"
            "  border-radius: 14px;"
            "  padding: 6px 12px;"
            "  font-size: 12px;"
            "  font-family: 'Segoe UI', 'Microsoft YaHei';"
            "}"
            "QPushButton:hover {"
            "  background-color: #383838;"
            "  border-color: #666;"
            "  color: white;"
            "}"
        );
    }
}

void AdvancedTagSelector::toggleTag(const QString& tag) {
    if (m_selected.contains(tag)) {
        m_selected.removeAll(tag);
    } else {
        m_selected.append(tag);
    }
    emit tagsChanged();
    updateList();
    m_search->setFocus();
}

void AdvancedTagSelector::showAtCursor() {
    QPoint pos = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(pos);
    if (screen) {
        QRect geo = screen->geometry();
        int x = pos.x() - 40; 
        int y = pos.y() - 10;
        if (x + width() > geo.right()) x = geo.right() - width();
        if (x < geo.left()) x = geo.left();
        if (y + height() > geo.bottom()) y = geo.bottom() - height();
        if (y < geo.top()) y = geo.top();
        move(x, y);
    }
    m_confirmed = false;
    show();
    raise();
    activateWindow();
    
    // 增加延迟确保焦点获取成功，解决“无法激活”的问题
    QTimer::singleShot(50, m_search, qOverload<>(&QWidget::setFocus));
}

bool AdvancedTagSelector::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Up) {
            m_search->setCursorPosition(0);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            m_search->setCursorPosition(m_search->text().length());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AdvancedTagSelector::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        // [MODIFIED] 由于标签选择器本身就是一个 Popup/编辑辅助窗口，Esc 应直接关闭返回。
        // 但这里为了遵循整体两段式逻辑，如果搜索框有内容则清空。
        if (!m_search->text().isEmpty()) {
            m_search->clear();
            event->accept();
            return;
        }
        close();
    } else if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_W) {
        close();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void AdvancedTagSelector::hideEvent(QHideEvent* event) {
    if (!m_confirmed) {
        m_confirmed = true;
        emit tagsConfirmed(m_selected);
    }
    // 确保隐藏后能够自动销毁，避免内存累积或下次双击无法激活
    deleteLater();
    QWidget::hideEvent(event);
}
