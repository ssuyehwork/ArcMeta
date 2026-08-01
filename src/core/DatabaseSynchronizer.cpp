#include "DatabaseSynchronizer.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "../meta/MetadataManager.h"
#include "sqlite3.h"
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QtConcurrent>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

namespace ArcMeta {

struct ScanNode {
    std::wstring path;
    std::wstring name;
    uint64_t frn = 0;
    bool isDir = false;
    std::vector<ScanNode> children;
    std::vector<std::wstring> files;
};

static void scanPhysicalDirectory(const QString& currentPath, ScanNode& node) {
    QDir currentDir(currentPath);
    QFileInfoList list = currentDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

    for (const QFileInfo& fi : list) {
        std::wstring wPath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        if (fi.isDir()) {
            // 🚨 核心物理防线：如果该目录是以 .arc 结尾的资产包容器，绝对禁止作为子分类（node.children）添加！
            if (fi.fileName().endsWith(".arc", Qt::CaseInsensitive)) {
                // 直接扫描 .arc 内部的真实物理文件，将其作为文件塞入 node.files
                QDir arcDir(fi.absoluteFilePath());
                QFileInfoList arcFiles = arcDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
                for (const QFileInfo& afi : arcFiles) {
                    QString fn = afi.fileName();
                    if (fn.endsWith("_thumbnail.png", Qt::CaseInsensitive)) continue;
                    if (fn.compare("metadata.json", Qt::CaseInsensitive) == 0) continue;
                    node.files.push_back(QDir::toNativeSeparators(afi.absoluteFilePath()).toStdWString());
                }
                continue; // 彻底跳过将 .arc 自身创建为分类！
            }

            std::string fid;
            std::wstring frnStr;
            if (MetadataManager::fetchWinApiMetadataDirect(wPath, fid, &frnStr)) {
                try {
                    ScanNode childNode;
                    childNode.path = wPath;
                    childNode.name = fi.fileName().toStdWString();
                    childNode.frn = std::stoull(frnStr, nullptr, 16);
                    childNode.isDir = true;
                    scanPhysicalDirectory(fi.absoluteFilePath(), childNode);
                    node.children.push_back(std::move(childNode));
                } catch (...) {}
            }
        } else {
            node.files.push_back(wPath);
        }
    }
}

void DatabaseSynchronizer::syncPhysicalDirectoryCascade(const std::wstring& rootPath) {
    // ----------------------------------------------------
    // 【第一阶段】：纯 I/O 目录树收集，绝对不持任何 DB 写锁，杜绝假死
    // ----------------------------------------------------
    ScanNode rootNode;
    rootNode.path = rootPath;
    QFileInfo rootInfo(QString::fromStdWString(rootPath));
    rootNode.name = rootInfo.fileName().toStdWString();
    
    std::string rootFid;
    std::wstring rootFrnStr;
    if (!MetadataManager::fetchWinApiMetadataDirect(rootPath, rootFid, &rootFrnStr)) {
        return; 
    }
    try {
        rootNode.frn = std::stoull(rootFrnStr, nullptr, 16);
    } catch (...) { return; }
    rootNode.isDir = true;

    // 同步纯磁盘递归扫描，此时数据库不被上任何锁
    scanPhysicalDirectory(QString::fromStdWString(rootPath), rootNode);

    // ----------------------------------------------------
    // 【第二阶段】：超高速、高安全性纯内存与 CPU 对账，并开启极速写事务
    // ----------------------------------------------------
    sqlite3* db = DatabaseManager::instance().getGlobalDb();
    SqlTransaction trans(db);

    int rootCatId = CategoryRepo::findByFrn(rootNode.frn);
    if (rootCatId == 0) {
        std::wstring parentPath = rootInfo.absolutePath().toStdWString();
        std::string parentFid;
        std::wstring parentFrnStr;
        int parentCatId = 0;
        if (MetadataManager::fetchWinApiMetadataDirect(parentPath, parentFid, &parentFrnStr)) {
            try {
                uint64_t pFrn = std::stoull(parentFrnStr, nullptr, 16);
                parentCatId = CategoryRepo::findByFrn(pFrn);
            } catch (...) {}
        }

        Category cat;
        if (rootInfo.fileName().startsWith("ArcMeta.Library_", Qt::CaseInsensitive)) {
            cat.parentId = 0;
        } else {
            cat.parentId = parentCatId;
        }
        cat.name = rootNode.name;
        cat.physicalFrn = rootNode.frn;
        cat.physicalPath = rootNode.path;
        if (CategoryRepo::add(cat)) {
            rootCatId = cat.id;
            MetadataManager::instance().registerItem(rootPath, true);
        }
    }

    if (rootCatId <= 0) {
        trans.commit();
        return;
    }

    std::vector<std::wstring> collectedFilesToProcess;

    // 递归对账 lambda 函数
    std::function<void(const ScanNode&, int)> processNode;
    processNode = [&](const ScanNode& node, int parentCatId) {
        // 1. 处理子文件夹分类对账
        for (const auto& childNode : node.children) {
            // 【核心加固】：优先使用物理 FRN 指纹绝对唯一性检索，防止同名不同物理路径的映射重叠冲突
            int existingId = CategoryRepo::findByFrn(childNode.frn);
            if (existingId == 0) {
                // 如果指纹未命中，再尝试根据父分类ID和名字在数据库查找（处理可能的历史空 FRN 数据）
                existingId = CategoryRepo::findCategoryId(parentCatId, childNode.name);
                if (existingId > 0) {
                    // 若名字匹配了，检查其原有的 FRN。如果是空白或不符，安全修复并升级为 FRN 指纹标识
                    Category existingCat = CategoryRepo::getById(existingId);
                    if (existingCat.physicalFrn == 0 || existingCat.physicalFrn != childNode.frn) {
                        CategoryRepo::updatePhysicalMapping(existingId, childNode.frn, childNode.path);
                    }
                } else {
                    // 彻底未命中任何已知记录，新建分类
                    Category cat;
                    cat.parentId = parentCatId;
                    cat.name = childNode.name;
                    cat.physicalFrn = childNode.frn;
                    cat.physicalPath = childNode.path;
                    if (CategoryRepo::add(cat)) {
                        existingId = cat.id;
                        MetadataManager::instance().registerItem(childNode.path, true);
                    }
                }
            } else {
                // 指纹命中了，但可能物理路径由于用户外部移动发生过位移，执行安全升级修复路径关联
                Category existingCat = CategoryRepo::getById(existingId);
                if (existingCat.physicalPath != childNode.path || existingCat.parentId != parentCatId) {
                    existingCat.physicalPath = childNode.path;
                    existingCat.parentId = parentCatId;
                    CategoryRepo::update(existingCat);
                }
            }

            if (existingId > 0) {
                processNode(childNode, existingId);
            }
        }

        // 2. 收集此节点下的文件供批量多媒体提取与注册使用
        for (const auto& fPath : node.files) {
            collectedFilesToProcess.push_back(fPath);
            if (parentCatId > 0) {
                std::string fid;
                if (MetadataManager::fetchWinApiMetadataDirect(fPath, fid)) {
                    CategoryRepo::addItemToCategory(parentCatId, fid, fPath);
                }
            }
        }
    };

    processNode(rootNode, rootCatId);

    trans.commit();

    // ----------------------------------------------------
    // 【第三阶段】：异步投递多媒体高级特征提取流水线，解决断流 Bug
    // ----------------------------------------------------
    if (!collectedFilesToProcess.empty()) {
        QStringList qPathsToRegister;
        for (const auto& fp : collectedFilesToProcess) {
            qPathsToRegister.append(QString::fromStdWString(fp));
        }
        // 调用 registerItemsAsync，完美一键批处理在后台将文件塞入多媒体解析提取队列 (enqueueBatch)
        MetadataManager::instance().registerItemsAsync(qPathsToRegister, true);
        qDebug() << "[AutoImport] [Pipeline_Bridge] 已将" << qPathsToRegister.size() << "个新导入文件全部推入异步多媒体高级特征提取队列";
    }
}

} // namespace ArcMeta
