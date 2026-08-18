#pragma once
#include <vector>
#include <string>
#include <QString>

namespace QuarkMeta {

enum class DiskOperationMode {
    Rename,
    Move,
    Copy
};

#include <functional>

class DiskBatchRenameService {
public:
    /**
     * @brief 执行常规磁盘模式下的批量重命名/移动/复制
     */
    static void execute(const std::vector<std::wstring>& originalPaths, 
                        const std::vector<std::wstring>& newNames,
                        DiskOperationMode mode,
                        const QString& targetDir,
                        std::function<void(int successCount)> callback = nullptr);
};

} // namespace QuarkMeta
