#pragma once
#include <vector>
#include <string>
#include <QString>

namespace ArcMeta {

enum class DiskOperationMode {
    Rename,
    Move,
    Copy
};

class DiskBatchRenameService {
public:
    /**
     * @brief 执行常规磁盘模式下的批量重命名/移动/复制
     * @return 实际成功处理的文件数量
     */
    static int execute(const std::vector<std::wstring>& originalPaths,
                       const std::vector<std::wstring>& newNames,
                       DiskOperationMode mode,
                       const QString& targetDir);
};

} // namespace ArcMeta
