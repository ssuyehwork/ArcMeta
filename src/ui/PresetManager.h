#pragma once

#include <QString>
#include <vector>
#include "../meta/BatchRenameEngine.h"

namespace ArcMeta {

/**
 * @brief 预设管理器，仅负责重命名规则与 JSON 之间的序列化与反序列化
 */
class PresetManager {
public:
    static QString serializeRules(const std::vector<RenameRule>& rules);
    static std::vector<RenameRule> deserializeRules(const QString& jsonStr);
    static bool exportToFile(const QString& filePath, const std::vector<RenameRule>& rules);
    static std::vector<RenameRule> importFromFile(const QString& filePath);
};

} // namespace ArcMeta
