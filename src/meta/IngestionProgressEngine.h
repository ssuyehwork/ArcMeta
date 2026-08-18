#ifndef QuarkMeta_INGESTION_PROGRESS_ENGINE_H
#define QuarkMeta_INGESTION_PROGRESS_ENGINE_H

#include <string>
#include <QObject>

namespace QuarkMeta {

/**
 * @brief 专职负责多级目录自动导入与级联对账进度 (Percentage) 原子化计算的独立服务
 */
class IngestionProgressEngine : public QObject {
    Q_OBJECT
public:
    static IngestionProgressEngine& instance();

    /**
     * @brief 计算并持久化指定目录的进度百分比 (从 MetadataManager 物理移入)
     */
    void calculateAndPersistProgress(const std::wstring& folderPath);

    /**
     * @brief 从数据库加载持久化的进度值 (从 MetadataManager 物理移入)
     */
    double getProgressFromDb(const std::wstring& folderPath);

private:
    IngestionProgressEngine(QObject* parent = nullptr);
    ~IngestionProgressEngine() override = default;
};

} // namespace QuarkMeta

#endif // QuarkMeta_INGESTION_PROGRESS_ENGINE_H
