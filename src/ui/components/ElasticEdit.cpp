#include "ElasticEdit.h"
#include <QTextDocument>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QtMath>
#include <QLayout>

namespace QuarkMeta {

ElasticEdit::ElasticEdit(QWidget* parent) : QTextEdit(parent) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setLineWrapMode(QTextEdit::WidgetWidth);
    QTextOption opt = document()->defaultTextOption();
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->setDefaultTextOption(opt);
    document()->setDocumentMargin(0);
    connect(this, &QTextEdit::textChanged, this, &ElasticEdit::adjustHeight);
}

void ElasticEdit::adjustHeight() {
    int horizontalPadding = 20;
    int verticalPadding = 8;
    int border = 2;
    int w = width();
    if (w > 50) {
        int textW = w - horizontalPadding - border;
        if (document()->textWidth() != textW) {
            document()->setTextWidth(textW);
        }
    }
    qreal docHeight = document()->size().height();
    int newHeight = qMax(28, (int)qCeil(docHeight + verticalPadding + border)); 
    if (this->height() != newHeight) {
        setFixedHeight(newHeight);
        updateGeometry(); 
        QWidget* p = parentWidget();
        while (p) {
            if (p->layout()) p->layout()->activate();
            if (qobject_cast<QScrollArea*>(p)) break;
            p = p->parentWidget();
        }
    }
}

void ElasticEdit::resizeEvent(QResizeEvent* e) {
    QTextEdit::resizeEvent(e);
    adjustHeight();
}

void ElasticEdit::keyPressEvent(QKeyEvent* e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && !(e->modifiers() & Qt::ShiftModifier)) {
        emit returnPressed();
        clearFocus();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

} // namespace QuarkMeta
