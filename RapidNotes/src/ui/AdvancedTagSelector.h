#ifndef ADVANCEDTAGSELECTOR_H
#define ADVANCEDTAGSELECTOR_H

#include <QWidget>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QScrollArea>
#include <QPushButton>
#include "FlowLayout.h"

class AdvancedTagSelector : public QWidget {
    Q_OBJECT
public:
    explicit AdvancedTagSelector(QWidget* parent = nullptr);
    // 修复构造函数匹配问题，支持初始化数据
    void setup(const QList<QVariantMap>& recentTags, const QStringList& allTags, const QStringList& selectedTags);
    
    void setTags(const QStringList& allTags, const QStringList& selectedTags);
    QStringList selectedTags() const { return m_selected; }

    // 修复缺失的 showAtCursor 方法
    void showAtCursor();

signals:
    void tagsChanged();
    void tagsConfirmed(const QStringList& tags); // 修复缺失的信号

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateList();
    void toggleTag(const QString& tag);
    void updateChipState(QPushButton* btn, bool checked);

    QList<QVariantMap> m_recentTags;
    QStringList m_allTags;
    QStringList m_selected;
    bool m_confirmed = false;
    QLineEdit* m_search;
    QLabel* m_tipsLabel; // 新增提示标签
    QWidget* m_tagContainer;
    FlowLayout* m_flow;
};

#endif // ADVANCEDTAGSELECTOR_H
