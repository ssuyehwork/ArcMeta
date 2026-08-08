#pragma once
#include <vector>
#include <string>
#include <QString>

namespace ArcMeta {

class MemoryBatchRenameService {
public:
    /**
     * @brief 执行内存胶囊模式下的批量重命名
     * @return 实际成功重命名的资产数量
     */
    static int execute(const std::vector<std::wstring>& originalPaths, 
                       const std::vector<std::wstring>& newNames);
};

} // namespace ArcMeta
