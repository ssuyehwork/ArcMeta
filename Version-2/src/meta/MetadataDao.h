#ifndef ARCMETA_METADATA_DAO_H
#define ARCMETA_METADATA_DAO_H

#include <string>
#include <vector>
#include "MetadataDefs.h"

namespace ArcMeta {

/**
 * @brief 专职负责实体 metadata 表物理 CRUD SQL 业务拼接与读写的隔离数据访问器
 */
class MetadataDao {
public:
    static bool insertOrUpdateMetadata(const std::wstring& path, const std::string& fid);
    static bool deleteMetadataByPath(const std::wstring& path);
};

} // namespace ArcMeta

#endif // ARCMETA_METADATA_DAO_H
