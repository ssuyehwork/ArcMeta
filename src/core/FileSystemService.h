#ifndef ARCMETA_FILE_SYSTEM_SERVICE_H
#define ARCMETA_FILE_SYSTEM_SERVICE_H

#include <QObject>
#include <QStringList>
#include <vector>
#include <string>

namespace ArcMeta {

class FileSystemService : public QObject {
    Q_OBJECT
public:
    static FileSystemService& instance();

    void performCopy(const QStringList& paths, bool cutMode);
    bool performPaste(const QString& currentPath, QStringList& outFromPaths, bool& outIsMove);

private:
    FileSystemService(QObject* parent = nullptr);
    ~FileSystemService() override = default;
};

} // namespace ArcMeta

#endif // ARCMETA_FILE_SYSTEM_SERVICE_H
