#ifndef ARCMETA_DISK_INGESTION_SERVICE_H
#define ARCMETA_DISK_INGESTION_SERVICE_H

#include <string>
#include <QObject>

namespace ArcMeta {

/**
 * @brief 专职负责物理磁盘深度级联递归扫描与对账的高性能后台服务
 */
class DiskIngestionService : public QObject {
    Q_OBJECT
public:
    static DiskIngestionService& instance();

    /**
     * @brief 对指定根物理目录开展 1:1 的全量异步深度扫描 (从 AutoImportManager 物理移入)
     */
    void handleRecursiveIngestion(const std::wstring& rootPath);

private:
    DiskIngestionService(QObject* parent = nullptr);
    ~DiskIngestionService() override = default;
};

} // namespace ArcMeta

#endif // ARCMETA_DISK_INGESTION_SERVICE_H
