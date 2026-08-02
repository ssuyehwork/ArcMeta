#include "LibraryAssetModel.h"
#include "UiHelper.h"
#include "ShellIconManager.h"
#include "ModelContract.h"
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QFileIconProvider>

using namespace ArcMeta;

LibraryAssetModel::LibraryAssetModel(QObject* parent) : ItemModelBase(parent) {
    m_iconCache.setMaxCost(500);
}

LibraryAssetModel::~LibraryAssetModel() {}

int LibraryAssetModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_allRecords.size());
}

int LibraryAssetModel::columnCount(const QModelIndex&) const {
    return 7;
}

QVariant LibraryAssetModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(m_allRecords.size())) return QVariant();

    const auto& record = m_allRecords[index.row()];
    QString path = record.path;

    // 分类节点及子分类专用大分支（对应用户原话：“LibraryAssetModel 只处理内存数据库模式条目（包含 isCategory 分支）”）
    if (record.isCategory) {
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            switch (index.column()) {
                case 0: return record.categoryName;
                case 4: return "子分类";
                default: return "";
            }
        } else if (role == CategoryIdRole) {
            return record.categoryId;
        } else if (role == ColorRole) {
            return record.categoryColor;
        } else if (role == RatingRole) {
            return record.rating;
        } else if (role == TypeRole) {
            return "category";
        } else if (role == PathRole) {
            return record.path;
        } else if (role == IsLockedRole || role == PinnedRole) {
            return record.pinned;
        } else if (role == Qt::DecorationRole && index.column() == 0) {
            static QIcon catIcon = QFileIconProvider().icon(QFileIconProvider::Folder);
            return catIcon;
        }
        return QVariant();
    }

    // 内存托管库内已解包条目
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: {
                // 优先使用 ItemRecord 中解包好的真实素材文件名（对应用户原话：“内存模式下彻底解包 .arc 容器，显示真实素材文件名”）
                if (!record.filename.isEmpty()) return record.filename;
                int lastSlash = std::max(path.lastIndexOf('\\'), path.lastIndexOf('/'));
                if (lastSlash == -1) return path;
                QString name = path.mid(lastSlash + 1);
                if (name.isEmpty() && path.length() >= 2 && path[1] == ':') return path;
                return name;
            }
            case 3: {
                if (record.isDir) return "-";
                if (record.width > 0 && record.height > 0) {
                    return QString("%1 x %2").arg(record.width).arg(record.height);
                }
                return "-";
            }
            case 4: {
                if (record.isDir) return "文件夹";
                int lastDot = path.lastIndexOf('.');
                return (lastDot != -1) ? path.mid(lastDot + 1).toUpper() : "";
            }
            case 5: {
                if (record.isDir) return "-";
                if (record.size < 1024) return QString::number(record.size) + " B";
                if (record.size < 1024 * 1024) return QString::number(record.size / 1024.0, 'f', 1) + " KB";
                return QString::number(record.size / (1024.0 * 1024.0), 'f', 1) + " MB";
            }
            case 6: {
                return QDateTime::fromMSecsSinceEpoch(record.mtime).toString("dd-MM-yyyy HH:mm");
            }
        }
    } else if (role == PathRole) {
        return path;
    } else if (role == TypeRole) {
        return record.isDir ? "folder" : "file";
    } else if (role == RatingRole) {
        return record.rating;
    } else if (role == ColorRole) {
        return record.manualColor;
    } else if (role == IsLockedRole || role == PinnedRole) {
        return record.pinned;
    } else if (role == EncryptedRole) {
        return record.encrypted;
    } else if (role == TagsRole) {
        return record.tags;
    } else if (role == ManagedRole) {
        return record.isManaged;
    } else if (role == RegistrationProgressRole) {
        return record.registrationProgress;
    } else if (role == CategoryIdRole) {
        return 0;
    } else if (role == IsEmptyRole) {
        return false; // 内存模式不使用物理空文件夹状态
    } else if (role == AspectRatioRole) {
        if (record.width > 0 && record.height > 0) return (double)record.width / record.height;
        double ratio = m_aspectRatios.value(QDir::toNativeSeparators(path), 1.0);
        return ratio > 0.0 ? ratio : 1.0;
    } else if (role == HasThumbnailRole) {
        static const QStringList iconOnlyExts = {"cur", "ico", "ani"};
        if (iconOnlyExts.contains(record.suffix.toLower())) return false;
        if (record.suffix.toLower() == "ai") {
            QString nativePath = QDir::toNativeSeparators(path);
            if (m_aspectRatios.contains(nativePath)) {
                return m_aspectRatios.value(nativePath) > 0.0;
            }
            return false;
        }
        // .arc 资产包容器穿透（对应用户原话：“内容面板应"穿透" .arc 包”）
        if (record.isDir && path.endsWith(".arc", Qt::CaseInsensitive)) {
            QString nativePath = QDir::toNativeSeparators(path);
            return m_aspectRatios.contains(nativePath) && m_aspectRatios.value(nativePath) > 0.0;
        }
        if (UiHelper::isGraphicsFile(record.suffix)) return true;
        if (record.width > 0 && record.height > 0) return true;
        return m_aspectRatios.contains(QDir::toNativeSeparators(path)) && m_aspectRatios.value(QDir::toNativeSeparators(path)) > 0.0;
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        QString cacheKey = path;
        QIcon* cached = m_iconCache.object(cacheKey);
        if (cached) return *cached;

        QFileInfo info(path);
        QString ext = info.suffix().toLower();
        bool isGraphic = UiHelper::isGraphicsFile(ext) || ext == "svg";

        // .arc 资产包容器包内存在 _thumbnail.png，等待异步加载
        bool isArcContainer = (ext == "arc" && info.isDir());

        if (isGraphic || isArcContainer) return QIcon();
        return ShellIconManager::getFileIcon(path, 128);
    }

    return QVariant();
}
