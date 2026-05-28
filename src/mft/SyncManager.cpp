#include "SyncManager.h"
#include "UsnWatcher.h"

namespace ArcMeta {

SyncManager::SyncManager(QObject* parent) : QObject(parent) {}

SyncManager::~SyncManager() {
    stopAll();
}

void SyncManager::startWatching(const std::wstring& volume, uint64_t startUsn) {
    if (m_watchers.count(volume)) return;

    auto* watcher = new UsnWatcher(volume, startUsn);
    connect(watcher, &UsnWatcher::recordReceived, this, [this, volume](const QByteArray& recordData) {
        emit usnRecordReceived(volume, recordData);
    });
    connect(watcher, &UsnWatcher::journalInvalidated, this, [this, volume]() {
        emit journalInvalidated(volume);
    });

    m_watchers[volume] = watcher;
    watcher->start();
}

void SyncManager::stopWatching(const std::wstring& volume) {
    auto it = m_watchers.find(volume);
    if (it != m_watchers.end()) {
        it->second->stop();
        delete it->second;
        m_watchers.erase(it);
    }
}

void SyncManager::stopAll() {
    for (auto& pair : m_watchers) {
        pair.second->stop();
        delete pair.second;
    }
    m_watchers.clear();
}

} // namespace ArcMeta
