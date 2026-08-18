#pragma once

#include <QString>
#include <QIcon>
#include <QImage>
#include "WindowsShellThumbnailProvider.h"

namespace QuarkMeta {

/**
 * @brief 系统图标及缩略图提取核心管理器
 * 
 * 专责系统图标、缩略图提取与 COM 通信相关逻辑，杜绝误导性命名。
 */
class ShellIconManager {
public:
    static inline void initializeHotIcons() {
        WindowsShellThumbnailProvider::instance();
    }

    static inline QIcon getFileIcon(const QString& filePath, int size = 18) {
        return WindowsShellThumbnailProvider::getFileIcon(filePath, size);
    }

    static inline QIcon getFileIconFast(const QString& filePath, bool isDir, const QString& suffix) {
        return WindowsShellThumbnailProvider::getFileIconFast(filePath, isDir, suffix);
    }

    static inline bool isIconCached(const QString& filePath, bool isDir, const QString& suffix) {
        return WindowsShellThumbnailProvider::isIconCached(filePath, isDir, suffix);
    }

    static inline QImage getShellThumbnail(const QString& path, int size) {
        return WindowsShellThumbnailProvider::getShellThumbnail(path, size);
    }
};

} // namespace QuarkMeta
