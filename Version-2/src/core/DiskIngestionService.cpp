#include "DiskIngestionService.h"
#include "AutoImportManager.h"
#include <QDebug>
#include <QtConcurrent>

namespace ArcMeta {

DiskIngestionService& DiskIngestionService::instance() {
    static DiskIngestionService inst;
    return inst;
}

DiskIngestionService::DiskIngestionService(QObject* parent) : QObject(parent) {}

void DiskIngestionService::handleRecursiveIngestion(const std::wstring& rootPath) {
    // 异步流式拉起物理递归扫描。将此繁重负载完全从 Ingestion Broker (AutoImportManager) 移开。
    (void)QtConcurrent::run([rootPath]() {
        qDebug() << "[Import] DiskIngestionService 开始在后台异步扫描目录对账:" << QString::fromStdWString(rootPath);
        // 执行 1:1 的递归入库流程和分类生成
    });
}

} // namespace ArcMeta
