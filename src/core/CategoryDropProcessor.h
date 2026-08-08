#pragma once

#include <QObject>
#include <QStringList>

namespace ArcMeta {

class CategoryDropProcessor : public QObject {
    Q_OBJECT
public:
    explicit CategoryDropProcessor(QObject* parent = nullptr);

    // 唯一对外暴露的异步处理主接口
    void processDroppedPathsAsync(const QStringList& paths, int targetCategoryId);

signals:
    // 处理完成回调 UI 刷新信号，itemCount 包含落盘/处理成功项数
    void processingFinished(bool success, int itemCount);
};

} // namespace ArcMeta
