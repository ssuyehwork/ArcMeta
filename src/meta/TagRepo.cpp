#include "TagRepo.h"
#include <QFile>
#include <QDir>
#include <QDataStream>
#include <QStandardPaths>
#include <set>
#include <algorithm>

namespace ArcMeta {

std::map<std::wstring, std::vector<std::string>> TagRepo::s_tagToFids;
std::map<std::string, std::vector<std::wstring>> TagRepo::s_fidToTags;
std::shared_mutex TagRepo::s_mutex;

static QString getStoragePath() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/ArcMeta/tags.index.scch";
    QDir().mkpath(QFileInfo(path).absolutePath());
    return path;
}

bool TagRepo::load() {
    QFile file(getStoragePath());
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly)) return false;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);

    char magic[4];
    if (file.read(magic, 4) != 4) return false;
    if (memcmp(magic, "TAGS", 4) != 0) return false;

    uint32_t version;
    ds >> version;
    if (version != 1) return false;

    uint32_t tagCount;
    ds >> tagCount;

    std::unique_lock<std::shared_mutex> lock(s_mutex);
    s_tagToFids.clear();
    s_fidToTags.clear();

    for (uint32_t i = 0; i < tagCount; ++i) {
        QString tagName;
        ds >> tagName;
        std::wstring wTagName = tagName.toStdWString();

        uint32_t fidCount;
        ds >> fidCount;
        std::vector<std::string>& fids = s_tagToFids[wTagName];
        for (uint32_t j = 0; j < fidCount; ++j) {
            QString fid;
            ds >> fid;
            std::string sFid = fid.toStdString();
            fids.push_back(sFid);
            s_fidToTags[sFid].push_back(wTagName);
        }
    }
    return true;
}

bool TagRepo::save() {
    std::shared_lock<std::shared_mutex> lock(s_mutex);
    QString path = getStoragePath();
    QString tmpPath = path + ".tmp";
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream ds(&file);
    ds.setVersion(QDataStream::Qt_6_0);

    file.write("TAGS", 4);
    ds << (uint32_t)1; // version
    ds << (uint32_t)s_tagToFids.size();

    for (auto const& [tag, fids] : s_tagToFids) {
        ds << QString::fromStdWString(tag);
        ds << (uint32_t)fids.size();
        for (const auto& fid : fids) {
            ds << QString::fromStdString(fid);
        }
    }
    file.close();

    if (QFile::exists(path)) QFile::remove(path);
    return QFile::rename(tmpPath, path);
}

bool TagRepo::bindTag(const std::string& fid, const std::wstring& tag, const std::wstring& pathHint) {
    Q_UNUSED(pathHint);
    if (fid.empty() || tag.empty()) return false;

    std::unique_lock<std::shared_mutex> lock(s_mutex);
    auto& fids = s_tagToFids[tag];
    if (std::find(fids.begin(), fids.end(), fid) == fids.end()) {
        fids.push_back(fid);
        s_fidToTags[fid].push_back(tag);
        lock.unlock();
        save();
        return true;
    }
    return false;
}

bool TagRepo::unbindTag(const std::string& fid, const std::wstring& tag) {
    bool changed = false;
    std::unique_lock<std::shared_mutex> lock(s_mutex);
    auto it = s_tagToFids.find(tag);
    if (it != s_tagToFids.end()) {
        auto& fids = it->second;
        auto fit = std::find(fids.begin(), fids.end(), fid);
        if (fit != fids.end()) {
            fids.erase(fit);
            changed = true;
            if (fids.empty()) s_tagToFids.erase(it);
        }
    }

    auto it2 = s_fidToTags.find(fid);
    if (it2 != s_fidToTags.end()) {
        auto& tags = it2->second;
        auto tit = std::find(tags.begin(), tags.end(), tag);
        if (tit != tags.end()) {
            tags.erase(tit);
            changed = true;
            if (tags.empty()) s_fidToTags.erase(it2);
        }
    }

    if (changed) {
        lock.unlock();
        save();
    }
    return changed;
}

std::vector<std::string> TagRepo::getFidsByTag(const std::wstring& tag) {
    std::shared_lock<std::shared_mutex> lock(s_mutex);
    auto it = s_tagToFids.find(tag);
    return (it != s_tagToFids.end()) ? it->second : std::vector<std::string>();
}

std::vector<std::wstring> TagRepo::getTagsByFid(const std::string& fid) {
    std::shared_lock<std::shared_mutex> lock(s_mutex);
    auto it = s_fidToTags.find(fid);
    return (it != s_fidToTags.end()) ? it->second : std::vector<std::wstring>();
}

std::vector<std::pair<std::wstring, int>> TagRepo::getAllTagsWithCount() {
    std::shared_lock<std::shared_mutex> lock(s_mutex);
    std::vector<std::pair<std::wstring, int>> results;
    for (auto const& [tag, fids] : s_tagToFids) {
        results.push_back({tag, (int)fids.size()});
    }
    return results;
}

bool TagRepo::renameTag(const std::wstring& oldName, const std::wstring& newName) {
    if (oldName == newName) return true;
    std::unique_lock<std::shared_mutex> lock(s_mutex);
    auto it = s_tagToFids.find(oldName);
    if (it == s_tagToFids.end()) return false;

    std::vector<std::string> fids = it->second;
    s_tagToFids.erase(it);

    auto& newFids = s_tagToFids[newName];
    std::set<std::string> merged(newFids.begin(), newFids.end());
    merged.insert(fids.begin(), fids.end());
    newFids.assign(merged.begin(), merged.end());

    for (const auto& fid : fids) {
        auto& tags = s_fidToTags[fid];
        std::replace(tags.begin(), tags.end(), oldName, newName);
        std::sort(tags.begin(), tags.end());
        tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    }

    lock.unlock();
    return save();
}

bool TagRepo::deleteTag(const std::wstring& tag) {
    std::unique_lock<std::shared_mutex> lock(s_mutex);
    auto it = s_tagToFids.find(tag);
    if (it == s_tagToFids.end()) return false;

    std::vector<std::string> fids = it->second;
    s_tagToFids.erase(it);

    for (const auto& fid : fids) {
        auto& tags = s_fidToTags[fid];
        tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
        if (tags.empty()) s_fidToTags.erase(fid);
    }

    lock.unlock();
    return save();
}

int TagRepo::getTaggedFileCount() {
    std::shared_lock<std::shared_mutex> lock(s_mutex);
    return (int)s_fidToTags.size();
}

} // namespace ArcMeta
