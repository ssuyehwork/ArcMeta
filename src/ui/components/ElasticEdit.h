#pragma once
#include <QTextEdit>

namespace ArcMeta {

class ElasticEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit ElasticEdit(QWidget* parent = nullptr);
    void adjustHeight();
signals:
    void returnPressed();
protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
};

} // namespace ArcMeta
