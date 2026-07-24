#pragma once

#include <string>
#include <vector>

namespace ArcMeta {

struct BatchEnumeratedEntry {
    std::wstring name;
    std::wstring fullPath;
    long long fileSize = 0;
    long long ctime = 0;
    long long mtime = 0;
    long long atime = 0;
    bool isDir = false;
    std::string fileId128;
    std::wstring frn;
};

class DirectoryBatchEnumerator {
public:
    /**
     * @brief 批量枚举指定目录下的所有子项基础属性
     * @param dirPath 目录的绝对路径 (标准化)
     * @param outEntries 导出的属性条目列表
     * @return 成功返回 true，若不支持或失败返回 false 触发降级
     */
    static bool enumerate(const std::wstring& dirPath, std::vector<BatchEnumeratedEntry>& outEntries);
};

} // namespace ArcMeta
