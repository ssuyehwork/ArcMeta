#include "FileStorageHelper.h"
#include "DatabaseManager.h"
#include "FileCryptoHelper.h"
#include "../ui/FramelessDialog.h"
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QApplication>
#include <QFileDialog>
#include <QTextStream>
#include <QRegularExpression>
#include <QStringConverter>
#include <functional>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include "../ui/ToolTipOverlay.h"

static bool copyRecursively(const QString& srcPath, const QString& dstPath) {
    QFileInfo srcInfo(srcPath);
    if (srcInfo.isDir()) {
        if (!QDir().mkpath(dstPath)) return false;
        QDir srcDir(srcPath);
        QStringList entries = srcDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            if (!copyRecursively(srcPath + "/" + entry, dstPath + "/" + entry)) return false;
        }
        return true;
    } else {
        return QFile::copy(srcPath, dstPath);
    }
}

/**
 * @brief 通用导出逻辑：将导航及其附件导出到指定目录
 */
static void exportNotesToDirectory(const QList<QVariantMap>& notes, const QString& exportPath, FramelessProgressDialog* progress = nullptr, int* processedCount = nullptr) {
    QDir().mkpath(exportPath);
    QFile csvFile(exportPath + "/notes.csv");
    bool csvOpened = false;
    QTextStream out(&csvFile);
    out.setEncoding(QStringConverter::Utf8);

    QSet<QString> usedFileNames;
    for (const auto& note : notes) {
        if (progress && progress->wasCanceled()) break;

        QString type = note.value("item_type").toString();
        QString title = note.value("title").toString();
        QString content = note.value("content").toString();
        QByteArray blob = note.value("data_blob").toByteArray();

        if (progress && processedCount) {
            progress->setValue((*processedCount)++);
            progress->setLabelText(QString("正在导出: %1").arg(title.left(30)));
        }

        if (type == "image" || type == "file" || type == "folder") {
            QString fileName = title;
            if (type == "image" && !QFileInfo(fileName).suffix().isEmpty()) {
                // 保留原有后缀
            } else if (type == "image") {
                fileName += ".png";
            }
            
            QString base = QFileInfo(fileName).completeBaseName();
            QString suffix = QFileInfo(fileName).suffix();
            QString finalName = fileName;
            int i = 1;
            while (usedFileNames.contains(finalName.toLower())) {
                finalName = suffix.isEmpty() ? base + QString(" (%1)").arg(i++) : base + QString(" (%1)").arg(i++) + "." + suffix;
            }
            usedFileNames.insert(finalName.toLower());

            QFile f(exportPath + "/" + finalName);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(blob);
                f.close();
            }
        } else if (type == "local_file" || type == "local_folder" || type == "local_batch") {
            QString fullPath = QCoreApplication::applicationDirPath() + "/" + content;
            QFileInfo fi(fullPath);
            if (fi.exists()) {
                QString finalName = fi.fileName();
                int i = 1;
                while (usedFileNames.contains(finalName.toLower())) {
                    finalName = fi.suffix().isEmpty() ? fi.completeBaseName() + QString(" (%1)").arg(i++) : fi.completeBaseName() + QString(" (%1)").arg(i++) + "." + fi.suffix();
                }
                usedFileNames.insert(finalName.toLower());
                
                if (fi.isFile()) {
                    QFile::copy(fullPath, exportPath + "/" + finalName);
                } else {
                    copyRecursively(fullPath, exportPath + "/" + finalName);
                }
            }
        } else {
            if (!csvOpened) {
                if (csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    out << "Title,Content,Tags,Time\n";
                    csvOpened = true;
                }
            }
            if (csvOpened) {
                auto escape = [](QString s) {
                    s.replace("\"", "\"\"");
                    return "\"" + s + "\"";
                };
                out << escape(title) << "," 
                    << escape(content) << "," 
                    << escape(note.value("tags").toString()) << ","
                    << escape(note.value("created_at").toDateTime().toString("yyyy-MM-dd HH:mm:ss")) << "\n";
            }
        }
        QCoreApplication::processEvents();
    }
    if (csvOpened) csvFile.close();
}

int FileStorageHelper::processImport(const QStringList& paths, int targetCategoryId, bool fromClipboard) {
    if (paths.isEmpty()) return 0;

    // [OPTIMIZATION] 开启批量导入模式，大幅提升包含大量小文件的文件夹导入速度
    DatabaseManager::instance().beginBatch();

    QList<int> createdNoteIds;
    QList<int> createdCatIds;

    ItemStats stats = calculateItemsStats(paths);
    qint64 processedSize = 0;
    
    FramelessProgressDialog* progress = nullptr;
    const qint64 sizeThreshold = 50 * 1024 * 1024; // 50MB
    const int countThreshold = 50; // 50个项目

    if (stats.totalSize >= sizeThreshold || stats.totalCount >= countThreshold) {
        // 使用 1000 作为精度，防止 qint64 字节数超出进度条的 int 范围
        progress = new FramelessProgressDialog("导入进度", "正在导入文件和目录结构...", 0, 1000);
        progress->setProperty("totalSize", stats.totalSize);
        progress->setWindowModality(Qt::WindowModal);
        progress->show();
    }

    bool canceled = false;
    int totalCount = 0;
    for (const QString& path : paths) {
        if (progress && progress->wasCanceled()) {
            canceled = true;
            break;
        }

        QFileInfo info(path);
        if (info.isDir()) {
            // [CRITICAL] 文件夹导入遵循用户要求：创建为分类
            // 如果 targetCategoryId 为 -1，则作为顶级分类；否则作为其子分类。
            totalCount += importFolderRecursive(path, targetCategoryId, createdNoteIds, createdCatIds, progress, &processedSize, fromClipboard);
        } else if (info.suffix().toLower() == "csv") {
            // 独立 CSV 文件导入：直接解析为笔记
            totalCount += parseCsvFile(path, targetCategoryId, &createdNoteIds);
        } else {
            if (storeFile(path, targetCategoryId, createdNoteIds, progress, &processedSize, fromClipboard)) {
                totalCount++;
            }
        }

        if (progress && progress->wasCanceled()) {
            canceled = true;
            break;
        }
    }

    if (canceled) {
        qDebug() << "[Import] 正在回滚已导入的数据...";
        // 1. 清理物理文件
        for (int id : createdNoteIds) {
            QVariantMap note = DatabaseManager::instance().getNoteById(id);
            QString relativePath = note["content"].toString();
            if (note["item_type"].toString() == "local_file" && relativePath.startsWith("attachments/")) {
                QString fullPath = QCoreApplication::applicationDirPath() + "/" + relativePath;
                QFile::remove(fullPath);
            }
        }
        // [OPTIMIZATION] 开启了批量事务，优先通过数据库回滚清理记录
        DatabaseManager::instance().rollbackBatch();
        
        if (progress) delete progress;
        return 0;
    }

    if (progress) {
        progress->setValue(1000);
        delete progress;
    }

    // [OPTIMIZATION] 结束批量导入并一次性持久化
    DatabaseManager::instance().endBatch();

    return totalCount;
}

FileStorageHelper::ItemStats FileStorageHelper::calculateItemsStats(const QStringList& paths) {
    ItemStats stats;
    for (const QString& path : paths) {
        QFileInfo info(path);
        stats.totalCount++;
        if (info.isDir()) {
            QDir dir(path);
            QStringList subPaths;
            for (const auto& entry : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                subPaths << dir.absoluteFilePath(entry);
            }
            ItemStats subStats = calculateItemsStats(subPaths);
            stats.totalSize += subStats.totalSize;
            stats.totalCount += subStats.totalCount;
        } else {
            stats.totalSize += info.size();
        }
    }
    return stats;
}

int FileStorageHelper::importFolderRecursive(const QString& folderPath, int parentCategoryId, 
                                           QList<int>& createdNoteIds, QList<int>& createdCatIds,
                                           FramelessProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
    QFileInfo info(folderPath);
    
    // 直接采用文件夹原始名称
    QString catName = info.fileName();

    // 1. 创建分类
    int catId = DatabaseManager::instance().addCategory(catName, parentCategoryId);
    if (catId <= 0) return 0;
    
    createdCatIds.append(catId);

    int count = 1; // 包含分类自身
    QDir dir(folderPath);

    // 优先处理 notes.csv (还原文本笔记)
    if (dir.exists("notes.csv")) {
        count += parseCsvFile(dir.filePath("notes.csv"), catId, &createdNoteIds);
    }

    // 2. 导入文件 (排除 notes.csv)
    for (const QString& fileName : dir.entryList(QDir::Files)) {
        if (progress && progress->wasCanceled()) break;
        if (fileName.toLower() == "notes.csv") continue;
        if (storeFile(dir.filePath(fileName), catId, createdNoteIds, progress, processedSize, false)) {
            count++;
        }
    }

    // 3. 递归导入子文件夹
    for (const QString& subDirName : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (progress && progress->wasCanceled()) break;
        count += importFolderRecursive(dir.filePath(subDirName), catId, createdNoteIds, createdCatIds, progress, processedSize, false);
    }

    return count;
}

int FileStorageHelper::parseCsvFile(const QString& csvPath, int catId, QList<int>* createdNoteIds) {
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    
    QString data = QString::fromUtf8(file.readAll());
    file.close();

    int count = 0;
    QList<QStringList> rows;
    QStringList currentRow;
    QString currentField;
    bool inQuotes = false;
    for (int i = 0; i < data.length(); ++i) {
        QChar c = data[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < data.length() && data[i + 1] == '"') {
                    currentField += '"'; i++;
                } else inQuotes = false;
            } else currentField += c;
        } else {
            if (c == '"') inQuotes = true;
            else if (c == ',') { currentRow << currentField; currentField.clear(); }
            else if (c == '\n') { currentRow << currentField; rows << currentRow; currentRow.clear(); currentField.clear(); }
            else if (c == '\r') continue;
            else currentField += c;
        }
    }
    if (!currentRow.isEmpty() || !currentField.isEmpty()) { currentRow << currentField; rows << currentRow; }

    if (rows.size() > 1) {
        QStringList headers = rows[0];
        int idxTitle = -1, idxContent = -1, idxTags = -1;
        for(int i=0; i<headers.size(); ++i) {
            QString h = headers[i].trimmed().toLower();
            if(h == "title") idxTitle = i;
            else if(h == "content") idxContent = i;
            else if(h == "tags") idxTags = i;
        }

        for (int i = 1; i < rows.size(); ++i) {
            QStringList row = rows[i];
            QString title = (idxTitle != -1 && idxTitle < row.size()) ? row[idxTitle] : "导入笔记";
            QString content = (idxContent != -1 && idxContent < row.size()) ? row[idxContent] : "";
            QString tagsStr = (idxTags != -1 && idxTags < row.size()) ? row[idxTags] : "";
            if (title.isEmpty() && content.isEmpty()) continue;
            
            int noteId = DatabaseManager::instance().addNote(title, content, tagsStr.split(",", Qt::SkipEmptyParts), "", catId);
            if (noteId > 0) {
                if (createdNoteIds) createdNoteIds->append(noteId);
                count++;
            }
        }
    }
    return count;
}

bool FileStorageHelper::storeFile(const QString& path, int categoryId, 
                                QList<int>& createdNoteIds,
                                FramelessProgressDialog* progress, qint64* processedSize, bool fromClipboard) {
    QFileInfo info(path);
    QString storageDir = getStorageRoot();
    QString destPath = getUniqueFilePath(storageDir, info.fileName());
    
    // 执行物理拷贝
    bool ok = QFile::copy(path, destPath);
    
    if (ok) {
        if (processedSize) {
            *processedSize += info.size();
            if (progress) {
                 // 计算当前总进度的比例并映射到 0-1000 范围
                 qint64 total = progress->property("totalSize").toLongLong();
                 if (total > 0) {
                     int val = static_cast<int>((*processedSize * 1000) / total);
                     progress->setValue(val);
                 }
                 progress->setLabelText(QString("正在导入: %1").arg(info.fileName()));
            }
        }

        QFileInfo destInfo(destPath);
        QString relativePath = "attachments/" + destInfo.fileName();

        QString title = info.fileName();

        int noteId = DatabaseManager::instance().addNote(
            title,
            relativePath,
            {"导入文件"},
            "#2c3e50",
            categoryId,
            "local_file",
            QByteArray(),
            "FileStorage",
            info.absoluteFilePath()
        );

        if (noteId > 0) {
            createdNoteIds.append(noteId);
        } else {
            // 如果数据库记录插入失败，为了严谨，删除刚才拷贝的物理文件
            QFile::remove(destPath);
            ok = false;
        }
        
        QApplication::processEvents();
    }
    
    return ok;
}

QString FileStorageHelper::getStorageRoot() {
    QString path = QCoreApplication::applicationDirPath() + "/attachments";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

void FileStorageHelper::exportCategory(int catId, const QString& catName, QWidget* parent) {
    QString dir = QFileDialog::getExistingDirectory(parent, "选择导出目录", "");
    if (dir.isEmpty()) return;

    QString safeCatName = catName;
    safeCatName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString exportPath = dir + "/" + safeCatName;

    QList<QVariantMap> notes = DatabaseManager::instance().searchNotes("", "category", catId, -1, -1);
    if (notes.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 导出失败：当前分类下没有灵感</b>");
        return;
    }

    int processedCount = 0;
    FramelessProgressDialog* progress = nullptr;
    if (notes.size() > 50) {
        progress = new FramelessProgressDialog("导出进度", "正在导出灵感及附件...", 0, notes.size(), parent);
        progress->show();
    }

    exportNotesToDirectory(notes, exportPath, progress, &processedCount);

    if (progress) delete progress;
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 分类 [%1] 导出完成</b>").arg(catName));
}

void FileStorageHelper::exportByFilter(const QString& filterType, const QVariant& filterValue, const QString& exportName, QWidget* parent) {
    // 2026-03-22 [NEW] 按照用户要求：支持特殊分类（收藏、今日等）导出
    QString dir = QFileDialog::getExistingDirectory(parent, QString("选择导出目录 [%1]").arg(exportName), "");
    if (dir.isEmpty()) return;

    QString safeName = exportName;
    safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString exportPath = dir + "/" + safeName;

    QList<QVariantMap> notes = DatabaseManager::instance().searchNotes("", filterType, filterValue, -1, -1);
    if (notes.isEmpty()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e67e22;'>[!] 导出失败：当前视图下没有可导出的灵感</b>");
        return;
    }

    int processedCount = 0;
    FramelessProgressDialog* progress = nullptr;
    if (notes.size() > 50) {
        progress = new FramelessProgressDialog("导出进度", QString("正在导出 %1...").arg(exportName), 0, notes.size(), parent);
        progress->show();
    }

    exportNotesToDirectory(notes, exportPath, progress, &processedCount);

    if (progress) delete progress;
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] %1 导出完成</b>").arg(exportName));
}

void FileStorageHelper::exportFullStructure(QWidget* parent) {
    // 2026-03-22 [NEW] 按照用户要求：从“全部数据”导出时，按照完整分类结构导出
    QString dir = QFileDialog::getExistingDirectory(parent, "选择完整结构导出目录", "");
    if (dir.isEmpty()) return;

    QString exportPath = dir + "/RapidNotes_FullBackup_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmm");
    QDir().mkpath(exportPath);

    // 1. 获取所有分类笔记总数以支持进度条（可选，这里简化处理）
    
    // 2. 递归导出逻辑
    std::function<void(int, const QString&)> recursiveExport = [&](int catId, const QString& currentPath) {
        // 导出当前分类下的笔记（含附件）
        QList<QVariantMap> notes = DatabaseManager::instance().searchNotes("", "category", catId, -1, -1);
        if (!notes.isEmpty()) {
            exportNotesToDirectory(notes, currentPath);
        }

        // 导出子分类
        QList<QVariantMap> children = DatabaseManager::instance().getChildCategories(catId);
        for (const auto& child : children) {
            QString safeSubName = child.value("name").toString().replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
            recursiveExport(child.value("id").toInt(), currentPath + "/" + safeSubName);
        }
    };

    // 3. 处理所有顶级分类
    QList<QVariantMap> topCats = DatabaseManager::instance().getChildCategories(-1);
    for (const auto& cat : topCats) {
        QString safeCatName = cat.value("name").toString().replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
        recursiveExport(cat.value("id").toInt(), exportPath + "/" + safeCatName);
    }

    // 4. 额外处理“未分类”灵感（位于根目录下的“未分类灵感”文件夹）
    QList<QVariantMap> uncategorized = DatabaseManager::instance().searchNotes("", "uncategorized", -1, -1, -1);
    if (!uncategorized.isEmpty()) {
        exportNotesToDirectory(uncategorized, exportPath + "/未分类灵感");
    }

    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 完整结构导出完成</b>");
}

void FileStorageHelper::exportCategoryRecursive(int catId, const QString& catName, QWidget* parent) {
    // 1. 获取用户选择的导出目录
    QString dir = QFileDialog::getExistingDirectory(parent, "选择递归导出目录", "");
    if (dir.isEmpty()) return;

    QString safeCatName = catName;
    safeCatName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    QString exportPath = dir + "/" + safeCatName;

    // 2. 递归导出逻辑
    std::function<void(int, const QString&)> doExport = [&](int currentCatId, const QString& currentPath) {
        // A. 导出当前分类下的笔记及其附件
        QList<QVariantMap> notes = DatabaseManager::instance().searchNotes("", "category", currentCatId, -1, -1);
        if (!notes.isEmpty()) {
            exportNotesToDirectory(notes, currentPath);
        }

        // B. 递归处理所有子分类
        QList<QVariantMap> subCats = DatabaseManager::instance().getChildCategories(currentCatId);
        for (const auto& subCat : subCats) {
            QString safeSubName = subCat.value("name").toString().replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
            doExport(subCat.value("id").toInt(), currentPath + "/" + safeSubName);
        }
    };

    // 3. 开始执行
    doExport(catId, exportPath);
    
    ToolTipOverlay::instance()->showText(QCursor::pos(), QString("<b style='color: #2ecc71;'>[OK] 递归导出分类 [%1] 完成</b>").arg(catName));
}

QString FileStorageHelper::getUniqueFilePath(const QString& dirPath, const QString& fileName) {
    QDir dir(dirPath);
    QString baseName = QFileInfo(fileName).completeBaseName();
    QString suffix = QFileInfo(fileName).suffix();
    if (!suffix.isEmpty()) suffix = "." + suffix;

    QString finalName = fileName;
    int counter = 1;

    while (dir.exists(finalName)) {
        finalName = QString("%1_%2%3").arg(baseName).arg(counter).arg(suffix);
        counter++;
    }
    return dir.filePath(finalName);
}

void FileStorageHelper::exportToPackage(int catId, const QString& catName, QWidget* parent) {
    QString fileName = QFileDialog::getSaveFileName(parent, "导出为数据包", catName + ".rnp", "RapidNotes Package (*.rnp)");
    if (fileName.isEmpty()) return;

    auto& db = DatabaseManager::instance();
    QJsonObject rootObj;
    rootObj["version"] = "1.0";
    rootObj["export_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 递归获取分类及笔记数据
    std::function<QJsonObject(int)> serializeCategory = [&](int id) -> QJsonObject {
        QJsonObject catObj;
        // 获取分类基本属性
        QList<QVariantMap> allCats = db.getAllCategories();
        QVariantMap currentCat;
        for (const auto& c : allCats) {
            if (c.value("id").toInt() == id) { currentCat = c; break; }
        }

        catObj["name"] = currentCat.value("name").toString();
        catObj["color"] = currentCat.value("color").toString();
        catObj["preset_tags"] = db.getCategoryPresetTags(id);

        // 导出该分类下的所有笔记
        QJsonArray notesArray;
        QList<QVariantMap> notes = db.searchNotes("", "category", id, -1, -1);
        for (const auto& note : notes) {
            QJsonObject nObj;
            nObj["title"] = note.value("title").toString();
            nObj["content"] = note.value("content").toString();
            nObj["tags"] = note.value("tags").toString();
            nObj["color"] = note.value("color").toString();
            nObj["item_type"] = note.value("item_type").toString();
            nObj["rating"] = note.value("rating").toInt();
            nObj["is_pinned"] = note.value("is_pinned").toInt();
            nObj["is_favorite"] = note.value("is_favorite").toInt();
            nObj["remark"] = note.value("remark").toString();
            
            // 处理二进制数据
            QByteArray blob = note.value("data_blob").toByteArray();
            if (!blob.isEmpty()) {
                nObj["data_blob"] = QString(blob.toBase64());
            }
            notesArray.append(nObj);
        }
        catObj["notes"] = notesArray;

        // 递归导出子分类
        QJsonArray childrenArray;
        QList<QVariantMap> children = db.getChildCategories(id);
        for (const auto& child : children) {
            childrenArray.append(serializeCategory(child.value("id").toInt()));
        }
        catObj["children"] = childrenArray;

        return catObj;
    };

    rootObj["data"] = serializeCategory(catId);

    // [MODIFIED] 2026-03-xx 按照用户要求：对 .rnp 文件进行加密处理
    QString tempPath = QDir::tempPath() + "/rnp_export_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";
    QFile tempFile(tempPath);
    if (tempFile.open(QIODevice::WriteOnly)) {
        tempFile.write(QJsonDocument(rootObj).toJson(QJsonDocument::Compact));
        tempFile.close();

        // 使用系统复合密钥进行加密
        QString key = FileCryptoHelper::getCombinedKey();
        if (FileCryptoHelper::encryptFileWithShell(tempPath, fileName, key)) {
            QFile::remove(tempPath);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 专属加密安装包导出成功</b>", 2000);
        } else {
            QFile::remove(tempPath);
            ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[Error] 加密导出失败</b>", 2000);
        }
    } else {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[Error] 临时文件创建失败</b>", 2000);
    }
}

void FileStorageHelper::importFromPackage(QWidget* parent) {
    QString fileName = QFileDialog::getOpenFileName(parent, "选择专属安装包", "", "RapidNotes Package (*.rnp)");
    if (fileName.isEmpty()) return;

    // [MODIFIED] 2026-03-xx 按照用户要求：解密 .rnp 文件
    QString tempPath = QDir::tempPath() + "/rnp_import_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".json";
    QString key = FileCryptoHelper::getCombinedKey();

    if (!FileCryptoHelper::decryptFileWithShell(fileName, tempPath, key)) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[Error] 安装包解密失败 (密钥或格式错误)</b>", 2000);
        QFile::remove(tempPath);
        return;
    }

    QFile file(tempPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QFile::remove(tempPath);
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QFile::remove(tempPath); // 解析后立即清理

    if (doc.isNull() || !doc.isObject()) {
        ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #e74c3c;'>[Error] 非法的加密安装包内容</b>", 2000);
        return;
    }

    QJsonObject rootObj = doc.object();
    QJsonObject data = rootObj["data"].toObject();
    auto& db = DatabaseManager::instance();

    db.beginBatch();

    std::function<void(const QJsonObject&, int)> importCategory = [&](const QJsonObject& obj, int parentId) {
        QString name = obj["name"].toString();
        QString color = obj["color"].toString();
        QString presetTags = obj["preset_tags"].toString();

        // 创建分类
        int newCatId = db.addCategory(name, parentId, color);
        if (newCatId > 0) {
            // 恢复预设标签
            if (!presetTags.isEmpty()) db.setCategoryPresetTags(newCatId, presetTags);

            // 导入笔记
            QJsonArray notes = obj["notes"].toArray();
            for (const auto& v : notes) {
                QJsonObject n = v.toObject();
                QStringList tags = n["tags"].toString().split(",", Qt::SkipEmptyParts);
                QByteArray blob = QByteArray::fromBase64(n["data_blob"].toString().toUtf8());
                
                int noteId = db.addNote(
                    n["title"].toString(),
                    n["content"].toString(),
                    tags,
                    n["color"].toString(),
                    newCatId,
                    n["item_type"].toString(),
                    blob,
                    "", "", // sourceApp, sourceTitle
                    n["remark"].toString()
                );
                
                if (noteId > 0) {
                    // 恢复状态属性
                    db.updateNoteState(noteId, "rating", n["rating"].toInt());
                    db.updateNoteState(noteId, "is_pinned", n["is_pinned"].toInt());
                    db.updateNoteState(noteId, "is_favorite", n["is_favorite"].toInt());
                }
            }

            // 递归导入子分类
            QJsonArray children = obj["children"].toArray();
            for (const auto& cv : children) {
                importCategory(cv.toObject(), newCatId);
            }
        }
    };

    importCategory(data, -1); // 默认导入到顶级
    db.endBatch();

    ToolTipOverlay::instance()->showText(QCursor::pos(), "<b style='color: #2ecc71;'>[OK] 安装包数据还原完成</b>", 2000);
}
