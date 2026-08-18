#pragma once
#include <QString>
#include <QDir>
#include "ShellHelper.h"

namespace QuarkMeta {
class AppDirectoryInitializer {
public:
    static void initializeStoragePath(const QString& baseAppPath) {
        QString metaDir = baseAppPath + "/.QuarkMeta";
        if (QDir().mkpath(metaDir)) {
            // 在专职初始化服务层集中隐藏
            ShellHelper::ensureHidden(metaDir.toStdWString());
        }
    }
};
}
