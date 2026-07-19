#include "ShellIconManager.h"
#include <QFileInfo>
#include <QFileIconProvider>
#include <QAbstractFileIconProvider>
#include <QtConcurrent/QtConcurrent>
#include <windows.h>
#include <objbase.h>

namespace ArcMeta {

ShellIconManager::ShellIconManager(QObject* parent) : QObject(parent) {}

ShellIconManager::~ShellIconManager() {
    clearCache();
}

ShellIconManager& ShellIconManager::instance() {
    static ShellIconManager inst;
    return inst;
}

QIcon ShellIconManager::getFileIcon(const QString& filePath, int size) {
    Q_UNUSED(size);
    QFileInfo info(filePath);
    QString key = info.isDir() ? (info.isRoot() ? filePath : "folder") : info.suffix().toLower();
    if (key.length() > 128) key = "unknown";

    {
        std::shared_lock<std::shared_mutex> lock(m_cacheLock);
        if (m_fileIconCache.contains(key)) {
            return m_fileIconCache[key];
        }
    }

    static QIcon s_defaultFileIcon;
    static QIcon s_defaultFolderIcon;
    if (s_defaultFileIcon.isNull() || s_defaultFolderIcon.isNull()) {
        QFileIconProvider provider;
        s_defaultFolderIcon = provider.icon(QAbstractFileIconProvider::Folder);
        s_defaultFileIcon = provider.icon(QAbstractFileIconProvider::File);
    }
    QIcon placeholderIcon = info.isDir() ? s_defaultFolderIcon : s_defaultFileIcon;

    {
        std::lock_guard<std::mutex> lock(m_loadingLock);
        if (m_loadingKeys.contains(key)) {
            return placeholderIcon;
        }
        m_loadingKeys.insert(key);
    }

    // 后台加载，Win32 COM初始化预热与注销
    (void)QtConcurrent::run([this, filePath, key, info]() {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        
        QFileIconProvider provider;
        QIcon icon;
        if (info.isDir()) {
            if (info.isRoot()) {
                icon = provider.icon(info);
            } else {
                icon = provider.icon(QAbstractFileIconProvider::Folder);
            }
        } else {
            icon = provider.icon(QFileInfo("dummy." + key));
            if (icon.isNull()) {
                icon = provider.icon(QAbstractFileIconProvider::File);
            }
        }

        {
            std::lock_guard<std::shared_mutex> lock(m_cacheLock);
            m_fileIconCache[key] = icon;
        }

        {
            std::lock_guard<std::mutex> lock(m_loadingLock);
            m_loadingKeys.remove(key);
        }

        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }

        QMetaObject::invokeMethod(this, [this]() {
            emit iconLoaded();
        }, Qt::QueuedConnection);
    });

    return placeholderIcon;
}

void ShellIconManager::clearCache() {
    std::lock_guard<std::shared_mutex> lock(m_cacheLock);
    m_fileIconCache.clear();
}

} // namespace ArcMeta
