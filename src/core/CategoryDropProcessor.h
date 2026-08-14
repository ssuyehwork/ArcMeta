#pragma once 

#include <QObject> 
#include <QStringList> 
#include <atomic>
#include "../util/AssetImporter.h"
 
namespace ArcMeta { 
 
class CategoryDropProcessor : public QObject { 
    Q_OBJECT 
public: 
    explicit CategoryDropProcessor(QObject* parent = nullptr); 

    // 一站式入库统一入口
    void executeImportPipeline(const QStringList& paths, int targetCategoryId);

    // 唯一对外暴露的异步处理主接口 
    void processDroppedPathsAsync(const QStringList& paths, int targetCategoryId); 

    // 用户取消当前后台归类/导入
    void cancel();

signals: 
    void progressStarted();

    // 处理完成回调 UI 刷新信号，itemCount 包含落盘/处理成功项数，newlyImportedPaths 包含成功导入的物理文件
    void processingFinished(bool success, int itemCount, const QStringList& newlyImportedPaths = QStringList()); 

    // 实时处理速率、推送当前进度及预计剩余秒数
    void progressUpdated(int processed, int total, int remainingSeconds = -1);

private:
    void triggerDuplicateCheck(const QStringList& paths, int targetCategoryId);

    std::atomic<bool> m_isCancelled{false};
}; 

} // namespace ArcMeta 
