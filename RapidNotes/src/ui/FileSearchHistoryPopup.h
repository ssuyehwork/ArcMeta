#ifndef FILESEARCHHISTORYPOPUP_H
#define FILESEARCHHISTORYPOPUP_H

#include <QWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QMouseEvent>
#include <utility>
#include "IconHelper.h"
#include "StringUtils.h"
#include "FileSearchWidget.h"

// 用户要求：修复由于 Q_OBJECT 类定义在 .cpp 中导致的 MOC 冲突。将相关类移至独立头文件，移除错误的 SvgIcons 命名空间。

class PathChip : public QFrame {
    Q_OBJECT
public:
    PathChip(const QString& text, QWidget* parent = nullptr) : QFrame(parent), m_text(text) {
        setAttribute(Qt::WA_StyledBackground);
        setCursor(Qt::PointingHandCursor);
        setObjectName("PathChip");
        
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 6, 10, 6);
        layout->setSpacing(10);
        
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet("border: none; background: transparent; color: #DDD; font-size: 13px;");
        layout->addWidget(lbl);
        layout->addStretch();
        
        auto* btnDel = new QPushButton();
        btnDel->setIcon(IconHelper::getIcon("close", "#666", 16));
        btnDel->setIconSize(QSize(10, 10));
        btnDel->setFixedSize(16, 16);
        btnDel->setCursor(Qt::PointingHandCursor);
        btnDel->setStyleSheet(
            "QPushButton { background-color: transparent; border-radius: 4px; padding: 0px; }"
            "QPushButton:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
        );
        
        connect(btnDel, &QPushButton::clicked, this, [this](){ emit deleted(m_text); });
        layout->addWidget(btnDel);

        setStyleSheet(
            "#PathChip { background-color: transparent; border: none; border-radius: 4px; }"
            "#PathChip:hover { background-color: #3e3e42; }" // 2026-03-xx 统一悬停色
        );
    }
    
    void mousePressEvent(QMouseEvent* e) override { 
        if(e->button() == Qt::LeftButton) emit clicked(m_text); 
        QFrame::mousePressEvent(e);
    }

signals:
    void clicked(const QString& text);
    void deleted(const QString& text);
private:
    QString m_text;
};

class FileSearchHistoryPopup : public QWidget {
    Q_OBJECT
public:
    enum Type { Path, Filename, Extension };

    explicit FileSearchHistoryPopup(FileSearchWidget* searchWidget, QLineEdit* edit, Type type) 
        : QWidget(searchWidget->window(), Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) 
    {
        m_searchWidget = searchWidget;
        m_edit = edit;
        m_type = type;
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        
        auto* rootLayout = new QVBoxLayout(this);
        rootLayout->setContentsMargins(12, 12, 12, 12);
        
        auto* container = new QFrame();
        container->setObjectName("PopupContainer");
        container->setStyleSheet(
            "#PopupContainer { background-color: #252526; border: 1px solid #444; border-radius: 10px; }"
        );
        rootLayout->addWidget(container);

        auto* shadow = new QGraphicsDropShadowEffect(container);
        shadow->setBlurRadius(20); shadow->setXOffset(0); shadow->setYOffset(5);
        shadow->setColor(QColor(0, 0, 0, 120));
        container->setGraphicsEffect(shadow);

        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(10);

        auto* top = new QHBoxLayout();
        QString titleStr = "最近扫描路径";
        if (m_type == Filename) titleStr = "最近搜索文件名";
        else if (m_type == Extension) titleStr = "最近搜索后缀";

        auto* icon = new QLabel();
        icon->setPixmap(IconHelper::getIcon("clock", "#888").pixmap(14, 14));
        icon->setStyleSheet("border: none; background: transparent;");
//         icon->setToolTip(StringUtils::wrapToolTip(titleStr));
        top->addWidget(icon);

        top->addStretch();

        auto* clearBtn = new QPushButton();
        clearBtn->setIcon(IconHelper::getIcon("trash", "#e74c3c", 14)); // 2026-03-13 统一 Trash 图标颜色为红色
        clearBtn->setIconSize(QSize(14, 14));
        clearBtn->setFixedSize(20, 20);
        clearBtn->setCursor(Qt::PointingHandCursor);
//         clearBtn->setToolTip(StringUtils::wrapToolTip("清空历史记录"));
        clearBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background-color: #3e3e42; }"); // 2026-03-xx 统一悬停色
        connect(clearBtn, &QPushButton::clicked, [this](){
            if (m_type == Path) m_searchWidget->clearHistory();
            else if (m_type == Filename) m_searchWidget->clearSearchHistory();
            else if (m_type == Extension) m_searchWidget->clearExtHistory();
            refreshUI();
        });
        top->addWidget(clearBtn);
        layout->addLayout(top);

        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setStyleSheet(
            "QScrollArea { background-color: transparent; border: none; }"
            "QScrollArea > QWidget > QWidget { background-color: transparent; }"
        );
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_chipsWidget = new QWidget();
        m_chipsWidget->setStyleSheet("background-color: transparent;");
        m_vLayout = new QVBoxLayout(m_chipsWidget);
        m_vLayout->setContentsMargins(0, 0, 0, 0);
        m_vLayout->setSpacing(2);
        m_vLayout->addStretch();
        scroll->setWidget(m_chipsWidget);
        layout->addWidget(scroll);

        m_opacityAnim = new QPropertyAnimation(this, "windowOpacity");
        m_opacityAnim->setDuration(200);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            close();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void refreshUI() {
        QLayoutItem* item;
        while ((item = m_vLayout->takeAt(0))) {
            if(item->widget()) item->widget()->deleteLater();
            delete item;
        }
        m_vLayout->addStretch();
        
        QStringList history;
        if (m_type == Path) history = m_searchWidget->getHistory();
        else if (m_type == Filename) history = m_searchWidget->getSearchHistory();
        else if (m_type == Extension) history = m_searchWidget->getExtHistory();

        if(history.isEmpty()) {
            auto* lbl = new QLabel("暂无历史记录");
            lbl->setAlignment(Qt::AlignCenter);
            lbl->setStyleSheet("color: #555; font-style: italic; margin: 20px; border: none;");
            m_vLayout->insertWidget(0, lbl);
        } else {
            for(const QString& val : std::as_const(history)) {
                auto* chip = new PathChip(val);
                chip->setFixedHeight(32);
                connect(chip, &PathChip::clicked, this, [this](const QString& v){ 
                    if (m_type == Path) m_searchWidget->useHistoryPath(v);
                    else m_edit->setText(v);
                    close(); 
                });
                connect(chip, &PathChip::deleted, this, [this](const QString& v){ 
                    if (m_type == Path) m_searchWidget->removeHistoryEntry(v);
                    else if (m_type == Filename) m_searchWidget->removeSearchHistoryEntry(v);
                    else if (m_type == Extension) m_searchWidget->removeExtHistoryEntry(v);
                    refreshUI(); 
                });
                m_vLayout->insertWidget(m_vLayout->count() - 1, chip);
            }
        }
        
        int targetWidth = m_edit->width();
        int contentHeight = 410;
        setFixedWidth(targetWidth + 24);
        resize(targetWidth + 24, contentHeight);
    }

public:
    void showAnimated() {
        refreshUI();
        QPoint pos = m_edit->mapToGlobal(QPoint(0, m_edit->height()));
        move(pos.x() - 12, pos.y() - 7);
        setWindowOpacity(0);
        show();
        m_opacityAnim->setStartValue(0);
        m_opacityAnim->setEndValue(1);
        m_opacityAnim->start();
    }

private:
    FileSearchWidget* m_searchWidget;
    QLineEdit* m_edit;
    Type m_type;
    QWidget* m_chipsWidget;
    QVBoxLayout* m_vLayout;
    QPropertyAnimation* m_opacityAnim;
};

#endif // FILESEARCHHISTORYPOPUP_H
