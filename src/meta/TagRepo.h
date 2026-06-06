#pragma once

#include <string>
#include <vector>
#include <map>
#include <shared_mutex>

namespace ArcMeta {

struct TagEntry {
    std::wstring tagName;               // 唯一键
    std::vector<std::string> fileIds;   // 关联的 fileId128 列表
};

class TagRepo {
public:
    // 给文件绑定标签（标签不存在则自动创建）
    static bool bindTag(const std::string& fid,
                        const std::wstring& tag,
                        const std::wstring& pathHint = L"");

    // 解绑标签
    static bool unbindTag(const std::string& fid, const std::wstring& tag);

    // 查询某标签下所有文件的 FID
    static std::vector<std::string> getFidsByTag(const std::wstring& tag);

    // 查询某文件的所有标签
    static std::vector<std::wstring> getTagsByFid(const std::string& fid);

    // 获取所有标签及其文件数量
    static std::vector<std::pair<std::wstring, int>> getAllTagsWithCount();

    // 重命名标签（全局生效）
    static bool renameTag(const std::wstring& oldName,
                          const std::wstring& newName);

    // 删除标签（从所有文件解绑）
    static bool deleteTag(const std::wstring& tag);

    // 获取已标记的文件总数（唯一 FID）
    static int getTaggedFileCount();

    // 显式加载与保存
    static bool load();
    static bool save();

private:
    static std::map<std::wstring, std::vector<std::string>> s_tagToFids;
    static std::map<std::string, std::vector<std::wstring>> s_fidToTags;
    static std::shared_mutex s_mutex;
};

} // namespace ArcMeta
