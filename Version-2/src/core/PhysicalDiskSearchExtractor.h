#pragma once

#include <QString>
#include <QStringList>
#include <string>
#include <unordered_set>
#include <functional>
#include <atomic>

namespace ArcMeta {

class PhysicalDiskSearchExtractor {
public:
    /**
     * @brief 执行具体的物理磁盘搜索，QDirIterator 迭代及攒批限速
     * @return 本次物理搜索新增的结果项目数量
     */
    static int performDiskSearch(
        const QString& parentPath,
        const QString& keyword,
        const std::atomic<bool>& abortFlag,
        const std::atomic<int>& currentSearchId,
        int searchId,
        std::unordered_set<std::wstring>& seenPaths,
        std::function<void(const QStringList&)> batchCallback
    );
};

} // namespace ArcMeta
