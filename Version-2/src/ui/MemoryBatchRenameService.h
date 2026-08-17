#pragma once
#include <vector>
#include <string>
#include <QString>

#include <functional>

namespace ArcMeta {

class MemoryBatchRenameService {
public:
    /**
     * @brief 执行内存胶囊模式下的批量重命名
     */
    static void execute(const std::vector<std::wstring>& originalPaths, 
                        const std::vector<std::wstring>& newNames,
                        std::function<void(int successCount)> callback = nullptr);
};

} // namespace ArcMeta
