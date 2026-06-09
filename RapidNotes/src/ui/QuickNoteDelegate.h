#ifndef QUICKNOTEDELEGATE_H
#define QUICKNOTEDELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QDateTime>
#include <QFileInfo>
#include "../models/NoteModel.h"
#include "IconHelper.h"
#include "ToolTipOverlay.h"
#include <QHelpEvent>
#include <QAbstractItemView>

class QuickNoteDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit QuickNoteDelegate(QObject* parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

#endif // QUICKNOTEDELEGATE_H
