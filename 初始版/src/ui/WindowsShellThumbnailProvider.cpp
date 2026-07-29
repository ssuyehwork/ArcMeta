#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "WindowsShellThumbnailProvider.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QFileIconProvider>
#include <QMutexLocker>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#ifdef __MINGW32__
#include <shlwapi.h>
#else
#include <shobjidl_core.h>
#include <thumbcache.h>
#endif
#endif

namespace ArcMeta {

WindowsShellThumbnailProvider& WindowsShellThumbnailProvider::instance() {
    static WindowsShellThumbnailProvider inst;
    return inst;
}

WindowsShellThumbnailProvider::WindowsShellThumbnailProvider() {
    connect(this, &WindowsShellThumbnailProvider::requestIconLoad,
            this, &WindowsShellThumbnailProvider::handleIconLoad,
            Qt::QueuedConnection);
}

QMutex& WindowsShellThumbnailProvider::fileIconMutex() {
    static QMutex mutex;
    return mutex;
}

QMap<QString, QIcon>& WindowsShellThumbnailProvider::fileIconCache() {
    static QMap<QString, QIcon> cache;
    return cache;
}

QMutex& WindowsShellThumbnailProvider::loadingMutex() {
    static QMutex mutex;
    return mutex;
}

QSet<QString>& WindowsShellThumbnailProvider::loadingKeys() {
    static QSet<QString> keys;
    return keys;
}

QIcon WindowsShellThumbnailProvider::getFileIcon(const QString& filePath, int size) {
    Q_UNUSED(size);
    QFileInfo info(filePath);
    
    QString key = info.isDir() ? (info.isRoot() ? filePath : "folder") : info.suffix().toLower();
    if (key.length() > 128) key = "unknown";

    {
        QMutexLocker locker(&fileIconMutex());
        if (fileIconCache().contains(key)) {
            return fileIconCache()[key];
        }
    }

    static QIcon s_defaultFileIcon;
    static QIcon s_defaultFolderIcon;
    if (s_defaultFileIcon.isNull() || s_defaultFolderIcon.isNull()) {
        QFileIconProvider provider;
        s_defaultFolderIcon = provider.icon(QFileIconProvider::Folder);
        s_defaultFileIcon = provider.icon(QFileIconProvider::File);
    }
    QIcon placeholderIcon = info.isDir() ? s_defaultFolderIcon : s_defaultFileIcon;

    {
        QMutexLocker lock(&loadingMutex());
        if (loadingKeys().contains(key)) {
            return placeholderIcon;
        }
        loadingKeys().insert(key);
    }

    emit instance().requestIconLoad(filePath, key, info.isDir(), info.isRoot());

    return placeholderIcon;
}

void WindowsShellThumbnailProvider::handleIconLoad(const QString& filePath, const QString& key, bool isDir, bool isRoot) {
    (void)QtConcurrent::run([filePath, key, isDir, isRoot]() {
        // 在子线程中执行 COM 线程环境初始化
#ifdef Q_OS_WIN
        HRESULT hres = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
        QFileIconProvider provider;
        QIcon icon;
        QFileInfo info(filePath);

        if (isDir) {
            if (isRoot) {
                icon = provider.icon(info);
            } else {
                icon = provider.icon(QFileIconProvider::Folder);
            }
        } else {
            icon = provider.icon(QFileInfo("dummy." + key));
            if (icon.isNull()) {
                icon = provider.icon(QFileIconProvider::File);
            }
        }

        {
            QMutexLocker locker(&fileIconMutex());
            fileIconCache()[key] = icon;
        }

        {
            QMutexLocker lock(&loadingMutex());
            loadingKeys().remove(key);
        }

#ifdef Q_OS_WIN
        if (SUCCEEDED(hres)) {
            CoUninitialize();
        }
#endif

        // 使用安全的信号槽在主线程中触发刷新
        QMetaObject::invokeMethod(&IconLoadNotifier::instance(), []() {
            emit IconLoadNotifier::instance().iconLoaded();
        }, Qt::QueuedConnection);
    });
}

struct ComInitializer {
    HRESULT hr;
    ComInitializer() {
#ifdef Q_OS_WIN
        hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#else
        hr = S_OK;
#endif
    }
    ~ComInitializer() {
#ifdef Q_OS_WIN
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
#endif
    }
};

QImage WindowsShellThumbnailProvider::getShellThumbnail(const QString& path, int size) {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString cacheDir = QDir(appData).filePath("thumbs/");
    QDir().mkpath(cacheDir);

    QFileInfo fi(path);
    QString hashKey = QString("%1_%2_%3_%4_v14").arg(path).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch()).arg(size);
    QString safeName = QString::number(qHash(hashKey), 16) + ".png";
    QString cachePath = cacheDir + safeName;

    if (QFile::exists(cachePath)) {
        QImage img;
        if (img.load(cachePath)) return img;
    }

#ifdef Q_OS_WIN
    ComInitializer comInit;
    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(path.toStdWString().c_str(), nullptr, &pidl, 0, nullptr);
    if (FAILED(hr)) return QImage();
    IShellItem* pItem = nullptr;
    hr = SHCreateItemFromIDList(pidl, IID_IShellItem, (void**)&pItem);
    ILFree(pidl);
    if (SUCCEEDED(hr)) {
        IShellItemImageFactory* pFactory = nullptr;
        hr = pItem->QueryInterface(IID_IShellItemImageFactory, (void**)&pFactory);
        if (SUCCEEDED(hr)) {
            SIZE nativeSize = { size, size };
            HBITMAP hBitmap = nullptr;
            hr = pFactory->GetImage(nativeSize, SIIGBF_RESIZETOFIT, &hBitmap);
            if (SUCCEEDED(hr) && hBitmap) {
                BITMAP bmpInfo;
                GetObject(hBitmap, sizeof(bmpInfo), &bmpInfo);
                int w = bmpInfo.bmWidth;
                int h = std::abs(bmpInfo.bmHeight);

                BITMAPINFOHEADER bi = {};
                bi.biSize        = sizeof(BITMAPINFOHEADER);
                bi.biWidth       = w;
                bi.biHeight      = -h;   // 负值 = top-down，方向永远正确
                bi.biPlanes      = 1;
                bi.biBitCount    = 32;
                bi.biCompression = BI_RGB;

                QByteArray pixels(w * h * 4, 0);
                HDC hdc = GetDC(nullptr);
                GetDIBits(hdc, hBitmap, 0, h, pixels.data(),
                          reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
                ReleaseDC(nullptr, hdc);

                // Windows 返回 BGRA，Qt 需要 RGBA，交换 R/B 通道
                uint8_t* p = reinterpret_cast<uint8_t*>(pixels.data());
                for (int i = 0; i < w * h; ++i) {
                    std::swap(p[i * 4 + 0], p[i * 4 + 2]);
                }

                QImage img(p, w, h, w * 4, QImage::Format_RGBA8888);
                img = img.copy(); // 确保数据所有权
                
                (void)QtConcurrent::run([img, cachePath]() {
                    img.save(cachePath, "PNG");
                });

                DeleteObject(hBitmap);
                pFactory->Release();
                pItem->Release();
                return img;
            }
            pFactory->Release();
        }
        pItem->Release();
    }
#else
    Q_UNUSED(path); Q_UNUSED(size);
#endif
    return QImage();
}

} // namespace ArcMeta
