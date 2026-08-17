#pragma once
#include <vector>
#include <string>
#include <set>
#include <map>
#include <QString>
#include <functional>

namespace ArcMeta {

struct RenamePair {
    std::wstring oldPath;
    std::wstring newPath;
};

struct RenameExecutionStep {
    std::wstring fromPath;
    std::wstring toPath;
    bool isTemporarySwap = false;
    std::wstring tempUuid;
};

class BatchRenameTransaction {
public:
    static bool validateNameConflicts(const std::vector<RenamePair>& pairs, std::string& outError);
    static std::vector<RenameExecutionStep> buildTopologyExecutionPlan(const std::vector<RenamePair>& pairs);
};

class MemoryBatchRenameService {
public:
    /**
     * @brief 执行内存胶囊模式下的批量重命名（事务隔离与拓扑解耦）
     */
    static void execute(const std::vector<std::wstring>& originalPaths, 
                        const std::vector<std::wstring>& newNames,
                        std::function<void(int successCount)> callback = nullptr);
};

} // namespace ArcMeta
