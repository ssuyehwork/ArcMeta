#include "AssetImporter.h"
#include "ShellHelper.h"
#include "../ui/Logger.h"
#include "../ui/BatchProgressDialog.h"
#include "../ui/ToolTipOverlay.h"
#include "../meta/MetadataManager.h"
#include "../meta/CategoryRepo.h"
#include "../meta/DatabaseManager.h"
#include "../ui/WindowsShellThumbnailProvider.h"
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>
#include <QMetaObject>
#include <QCoreApplication>
#include "FramelessDialog.h"
#include <QDateTime>
#include <QApplication>
#include <QRadioButton>
#include <QButtonGroup>
#include <QPainter>
#include <QPainterPath>
#include <QImageReader>
#include <QLabel>
#include <QFrame>

namespace ArcMeta {

static QString formatFileSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1B").arg(bytes);
    double kb = bytes / 1024.0;
    if (kb < 1024) return QString("%1KB").arg(QString::number(kb, 'f', 0));
    double mb = kb / 1024.0;
    return QString("%1MB").arg(QString::number(mb, 'f', 1));
}

class ImagePreviewLabel : public QLabel {
public:
    ImagePreviewLabel(const QString& filePath, const QString& overlayText, const QColor& overlayBgColor, QWidget* parent = nullptr)
        : QLabel(parent), m_overlayText(overlayText), m_overlayBgColor(overlayBgColor) {
        setFixedSize(280, 360);
        setStyleSheet("border-radius: 8px; border: 1px solid #333333; background-color: #111111;");

        // Load shell thumbnail in a safe size
        m_image = WindowsShellThumbnailProvider::getShellThumbnail(filePath, 256);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QLabel::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // 1. Draw the scaled image with aspect ratio keeping and round clipping
        if (!m_image.isNull()) {
            QPixmap pix = QPixmap::fromImage(m_image).scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            // Crop to center
            QPixmap cropped = pix.copy((pix.width() - width()) / 2, (pix.height() - height()) / 2, width(), height());

            QPainterPath path;
            path.addRoundedRect(rect(), 8, 8);
            painter.setClipPath(path);
            painter.drawPixmap(0, 0, cropped);
        }

        // 2. Draw the overlay badge in the center
        if (!m_overlayText.isEmpty()) {
            QFont font = painter.font();
            font.setPixelSize(12);
            font.setBold(true);
            painter.setFont(font);

            QFontMetrics fm(font);
            int textWidth = fm.horizontalAdvance(m_overlayText);
            int textHeight = fm.height();

            int padX = 16;
            int padY = 6;
            int rectW = textWidth + padX * 2;
            int rectH = textHeight + padY * 2;

            QRect bgRect((width() - rectW) / 2, (height() - rectH) / 2, rectW, rectH);

            painter.setClipping(false); // Disable clipping for drawing the badge over the image
            painter.setBrush(m_overlayBgColor);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(bgRect, 12, 12);

            painter.setPen(Qt::white);
            painter.drawText(bgRect, Qt::AlignCenter, m_overlayText);
        }
    }

private:
    QImage m_image;
    QString m_overlayText;
    QColor m_overlayBgColor;
};

class DuplicatePromptDialog : public FramelessDialog {
public:
    DuplicatePromptDialog(const QString& existingPath, const QString& newPath, const QString& categoryName, QWidget* parent = nullptr)
        : FramelessDialog("重复添加提示", parent), m_decision(0) {

        // Hide maximize/minimize/pin buttons
        setVisibleButtons(FramelessDialog::Close);

        // Set fixed dialog size
        setFixedSize(720, 560);

        QWidget* content = getContentArea();
        QVBoxLayout* layout = new QVBoxLayout(content);
        layout->setContentsMargins(30, 20, 30, 20);
        layout->setSpacing(20);

        // Columns layout for side-by-side comparison
        QHBoxLayout* colLayout = new QHBoxLayout();
        colLayout->setSpacing(40);

        // --- Left: Existing File ---
        QVBoxLayout* leftCol = new QVBoxLayout();
        leftCol->setSpacing(8);
        leftCol->setAlignment(Qt::AlignHCenter);

        ImagePreviewLabel* leftPreview = new ImagePreviewLabel(existingPath, "已存在", QColor(20, 20, 20, 220), this);
        leftCol->addWidget(leftPreview, 0, Qt::AlignHCenter);

        QFileInfo leftInfo(existingPath);
        QLabel* leftNameLabel = new QLabel(leftInfo.fileName());
        leftNameLabel->setAlignment(Qt::AlignCenter);
        leftNameLabel->setWordWrap(true);
        leftNameLabel->setStyleSheet("color: #FFFFFF; font-size: 13px; font-weight: normal;");
        leftCol->addWidget(leftNameLabel);

        // Get left image dimensions
        int lw = 0, lh = 0;
        QImageReader leftReader(existingPath);
        if (leftReader.canRead()) {
            QSize sz = leftReader.size();
            lw = sz.width();
            lh = sz.height();
        }
        QString leftSizeStr = formatFileSize(leftInfo.size());
        QString leftInfoText = (lw > 0 && lh > 0) ? QString("%1 × %2 / %3").arg(lw).arg(lh).arg(leftSizeStr) : leftSizeStr;
        QLabel* leftInfoLabel = new QLabel(leftInfoText);
        leftInfoLabel->setAlignment(Qt::AlignCenter);
        leftInfoLabel->setStyleSheet("color: #888888; font-size: 11px;");
        leftCol->addWidget(leftInfoLabel);

        QLabel* leftFolderBadge = new QLabel(categoryName);
        leftFolderBadge->setAlignment(Qt::AlignCenter);
        leftFolderBadge->setStyleSheet("border: 1px solid #444444; border-radius: 4px; background-color: #252525; color: #CCCCCC; font-size: 11px; padding: 2px 8px;");
        leftCol->addWidget(leftFolderBadge, 0, Qt::AlignHCenter);

        // --- Right: New File ---
        QVBoxLayout* rightCol = new QVBoxLayout();
        rightCol->setSpacing(8);
        rightCol->setAlignment(Qt::AlignHCenter);

        ImagePreviewLabel* rightPreview = new ImagePreviewLabel(newPath, "新的文件", QColor(30, 80, 150, 220), this);
        rightCol->addWidget(rightPreview, 0, Qt::AlignHCenter);

        QFileInfo rightInfo(newPath);
        QLabel* rightNameLabel = new QLabel(rightInfo.fileName());
        rightNameLabel->setAlignment(Qt::AlignCenter);
        rightNameLabel->setWordWrap(true);
        rightNameLabel->setStyleSheet("color: #FFFFFF; font-size: 13px; font-weight: normal;");
        rightCol->addWidget(rightNameLabel);

        // Get right image dimensions
        int rw = 0, rh = 0;
        QImageReader rightReader(newPath);
        if (rightReader.canRead()) {
            QSize sz = rightReader.size();
            rw = sz.width();
            rh = sz.height();
        }
        QString rightSizeStr = formatFileSize(rightInfo.size());
        QString rightInfoText = (rw > 0 && rh > 0) ? QString("%1 × %2 / %3").arg(rw).arg(rh).arg(rightSizeStr) : rightSizeStr;
        QLabel* rightInfoLabel = new QLabel(rightInfoText);
        rightInfoLabel->setAlignment(Qt::AlignCenter);
        rightInfoLabel->setStyleSheet("color: #888888; font-size: 11px;");
        rightCol->addWidget(rightInfoLabel);

        // Dummy folder label to align layout heights
        QLabel* rightDummy = new QLabel();
        rightDummy->setStyleSheet("font-size: 11px; padding: 2px 8px;");
        rightCol->addWidget(rightDummy, 0, Qt::AlignHCenter);

        colLayout->addLayout(leftCol);
        colLayout->addLayout(rightCol);
        layout->addLayout(colLayout);

        // Divider
        QFrame* line = new QFrame();
        line->setFixedHeight(1);
        line->setStyleSheet("background-color: #333333; border: none;");
        layout->addWidget(line);

        // Bottom controls layout
        QHBoxLayout* bottomLayout = new QHBoxLayout();

        m_radioGroup = new QButtonGroup(this);

        m_radioUseExisting = new QRadioButton("使用已存在文件导入", this);
        m_radioUseExisting->setChecked(true);
        m_radioUseExisting->setStyleSheet(
            "QRadioButton { color: #CCCCCC; font-size: 13px; spacing: 8px; }"
            "QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px; border: 2px solid #555555; background-color: #1E1E1E; }"
            "QRadioButton::indicator:checked { border: 2px solid #0078D4; background-color: #0078D4; }"
            "QRadioButton::indicator:hover { border-color: #777777; }"
        );
        m_radioGroup->addButton(m_radioUseExisting, 0);
        bottomLayout->addWidget(m_radioUseExisting);

        m_radioKeepBoth = new QRadioButton("保留两者", this);
        m_radioKeepBoth->setStyleSheet(
            "QRadioButton { color: #CCCCCC; font-size: 13px; spacing: 8px; }"
            "QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px; border: 2px solid #555555; background-color: #1E1E1E; }"
            "QRadioButton::indicator:checked { border: 2px solid #0078D4; background-color: #0078D4; }"
            "QRadioButton::indicator:hover { border-color: #777777; }"
        );
        m_radioGroup->addButton(m_radioKeepBoth, 1);
        bottomLayout->addWidget(m_radioKeepBoth);

        bottomLayout->addStretch();

        QPushButton* importBtn = new QPushButton("导入文件", this);
        importBtn->setFixedSize(100, 32);
        importBtn->setCursor(Qt::PointingHandCursor);
        importBtn->setStyleSheet(
            "QPushButton { background-color: #0078D4; color: white; border: none; border-radius: 4px; font-weight: bold; font-size: 13px; } "
            "QPushButton:hover { background-color: #1085E0; } "
            "QPushButton:pressed { background-color: #006CBE; }"
        );
        connect(importBtn, &QPushButton::clicked, this, [this]() {
            m_decision = m_radioGroup->checkedId();
            accept();
        });
        bottomLayout->addWidget(importBtn);

        layout->addLayout(bottomLayout);
    }

    int getDecision() const { return m_decision; }

private:
    QButtonGroup* m_radioGroup;
    QRadioButton* m_radioUseExisting;
    QRadioButton* m_radioKeepBoth;
    int m_decision;
};

static QString findDuplicateFile(qint64 fileSize, const QString& srcPath, std::string& outFid) {
    QString foundPath;
    MetadataManager::instance().forEachCachedItem([&](const std::wstring& path, const RuntimeMeta& meta) {
        if (!foundPath.isEmpty()) return;
        if (!meta.isFolder && !meta.isTrash && meta.fileSize == fileSize) {
            QString qPath = QString::fromStdWString(path);
            if (QDir::toNativeSeparators(qPath).toLower() != QDir::toNativeSeparators(srcPath).toLower()) {
                foundPath = qPath;
                outFid = meta.fileId128;
            }
        }
    });
    return foundPath;
}

void AssetImporter::importAssets(const QStringList& paths,
                                 int targetCatId,
                                 QWidget* parent,
                                 std::function<void()> onComplete) {
    importAssets(paths, targetCatId, "", false, parent, onComplete);
}

void AssetImporter::importAssets(const QStringList& paths,
                                 int targetCatId,
                                 const QString& targetPhysicalPath,
                                 bool isMove,
                                 QWidget* parent,
                                 std::function<void()> onComplete) {
    if (paths.isEmpty()) return;

    QString title = !targetPhysicalPath.isEmpty() ? "正在迁移项目至托管库..." : "正在导入资产包...";
    BatchProgressDialog* progress = new BatchProgressDialog(title, parent);
    progress->show();

    struct ImportContext {
        std::atomic<bool> isCancelled{false};
        QFuture<void> future;
    };
    auto context = std::make_shared<ImportContext>();
    QPointer<BatchProgressDialog> weakProgress(progress);

    QObject::connect(progress, &BatchProgressDialog::rejected, [weakProgress, context, parent, targetPhysicalPath]() {
        if (!weakProgress) return;
        QString titleStr = !targetPhysicalPath.isEmpty() ? "中断迁移" : "中断导入";
        QString contentStr = !targetPhysicalPath.isEmpty() ? "迁移尚未完成。确定要停止当前迁移任务吗？" : "导入尚未完成。确定要停止当前导入吗？";
        if (!FramelessMessageBox::question(parent, titleStr, contentStr)) {
            weakProgress->show();
            return;
        }
        context->isCancelled = true;
        if (context->future.isRunning()) context->future.waitForFinished();
        weakProgress->deleteLater();
    });

    context->future = QtConcurrent::run([paths, targetCatId, targetPhysicalPath, isMove, weakProgress, context, onComplete]() {
        int total = paths.size();
        int handled = 0;
        int successCount = 0;

        bool isPhysicalImport = !targetPhysicalPath.isEmpty();

        for (const QString& src : paths) {
            if (context->isCancelled) break;

            handled++;
            if (weakProgress) {
                QMetaObject::invokeMethod(weakProgress.data(), "updateProgress", Qt::QueuedConnection,
                                         Q_ARG(int, handled), Q_ARG(int, total), Q_ARG(QString, QFileInfo(src).fileName()));
            }

            if (isPhysicalImport) {
                // 🚨 核心逻辑（从原 ImportHelper 移植）：如果目标位置是 ArcMeta.Library_[盘符] 托管库，为其创建 .arc 资产包！
                QString destPath;
                if (targetPhysicalPath.contains("ArcMeta.Library_", Qt::CaseInsensitive)) {
                    // 统一规范：无论是导入还是剪切迁入，都使用统一 Base36 ID 生成！
                    QString assetId = ShellHelper::generateBase36Id();
                    QString arcContainer = QDir(targetPhysicalPath).filePath(assetId + ".arc");
                    if (!QDir().mkpath(arcContainer)) {
                        qWarning() << "[AssetImporter] 无法建立 .arc 资产包容器:" << arcContainer << " 源项目:" << src;
                        continue;
                    }
                    destPath = QDir(arcContainer).filePath(QFileInfo(src).fileName());
                } else {
                    destPath = QDir(targetPhysicalPath).filePath(QFileInfo(src).fileName());
                }

                // 精准大小去重检测
                QFileInfo srcInfo(src);
                std::string existingFid;
                QString existingPath = findDuplicateFile(srcInfo.size(), src, existingFid);
                if (!existingPath.isEmpty() && !existingFid.empty()) {
                    int userDecision = 0;
                    QString categoryName = "";
                    std::vector<int> categoryIds = CategoryRepo::getItemCategoryIds(existingFid);
                    if (!categoryIds.empty()) {
                        Category cat = CategoryRepo::getById(categoryIds[0]);
                        if (cat.id > 0) {
                            categoryName = QString::fromStdWString(cat.name);
                        }
                    }
                    if (categoryName.isEmpty()) {
                        categoryName = QFileInfo(existingPath).dir().dirName();
                    }
                    if (categoryName.isEmpty()) {
                        categoryName = "未命名文件夹";
                    }

                    QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
                        QWidget* parent = nullptr;
                        for (QWidget* w : QApplication::topLevelWidgets()) {
                            if (w->inherits("QMainWindow") || w->objectName() == "MainWindow") {
                                parent = w;
                                break;
                            }
                        }
                        DuplicatePromptDialog dlg(existingPath, src, categoryName, parent);
                        if (dlg.exec() == QDialog::Accepted) {
                            userDecision = dlg.getDecision();
                        } else {
                            userDecision = -1;
                        }
                    }, Qt::BlockingQueuedConnection);

                    if (userDecision == -1) {
                        continue; // Skip this file
                    } else if (userDecision == 0) {
                        // 使用已存在文件导入：不进行物理移动，如果 targetCatId > 0，绑定它即可
                        if (targetCatId > 0) {
                            CategoryRepo::addItemToCategory(targetCatId, existingFid, existingPath.toStdWString());
                        }
                        successCount++;
                        continue;
                    }
                }

                // 同一硬盘下无论是粘贴、拖拽导入都必须采用剪切、移动方式
                bool sameDrive = (QFileInfo(src).absoluteFilePath().left(1).toUpper() == QFileInfo(destPath).absoluteFilePath().left(1).toUpper());
                bool actualMove = isMove || sameDrive;

                // 执行物理移动/复制
                bool moved = ShellHelper::copyOrMoveItems({src}, QFileInfo(destPath).absolutePath(), actualMove);
                if (moved) {
                    MetadataManager::instance().syncAfterMove(
                        QDir::toNativeSeparators(src).toStdWString(),
                        QDir::toNativeSeparators(destPath).toStdWString());
                    successCount++;
                } else {
                    qWarning() << "[AssetImporter] copyOrMoveItems 失败！ 源文件:" << src << " 目标文件夹:" << QFileInfo(destPath).absolutePath();
                }
            } else {
                // 1. 获取目标盘符托管库路径 [盘符]:/ArcMeta.Library_[盘符]/
                QString drive = QFileInfo(src).absolutePath().left(3);
                if (drive.isEmpty()) drive = "D:/";
                QString managedRoot = drive + "ArcMeta.Library_" + drive.at(0).toUpper();

                if (!QDir().mkpath(managedRoot)) {
                    qWarning() << "[AssetImporter] 无法建立托管库根目录:" << managedRoot << " 导入源:" << src;
                    continue;
                }

                QFileInfo srcInfo(src);
                bool ok = false;
                if (srcInfo.isFile()) {
                    ok = importSingleFile(src, targetCatId, managedRoot);
                    if (!ok) {
                        qWarning() << "[AssetImporter] importSingleFile 导入失败！源文件:" << src;
                    }
                } else if (srcInfo.isDir()) {
                    ok = importDirectoryRecursive(src, targetCatId, managedRoot);
                    if (!ok) {
                        qWarning() << "[AssetImporter] importDirectoryRecursive 导入失败！源文件夹:" << src;
                    }
                }
                if (ok) {
                    successCount++;
                }
            }
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [weakProgress, context, successCount, isPhysicalImport, onComplete]() {
            if (context->isCancelled) return;
            if (weakProgress) {
                weakProgress->accept();
                weakProgress->deleteLater();
            }

            QString tipMsg = isPhysicalImport ? QString("已成功迁移 %1 个条目") : QString("已成功导入 %1 个受控资产单元");
            ToolTipOverlay::instance()->showText(QCursor::pos(),
                tipMsg.arg(successCount), 2000, QColor("#2ecc71"));

            if (onComplete) {
                onComplete();
            }
        });
    });
}

bool AssetImporter::importSingleFile(const QString& srcPath,
                                     int targetCatId,
                                     const QString& managedRoot) {
    QFileInfo srcInfo(srcPath);
    if (!srcInfo.exists() || !srcInfo.isFile()) return false;

    // 精准大小去重检测
    std::string existingFid;
    QString existingPath = findDuplicateFile(srcInfo.size(), srcPath, existingFid);
    if (!existingPath.isEmpty() && !existingFid.empty()) {
        int userDecision = 0;
        QString categoryName = "";
        std::vector<int> categoryIds = CategoryRepo::getItemCategoryIds(existingFid);
        if (!categoryIds.empty()) {
            Category cat = CategoryRepo::getById(categoryIds[0]);
            if (cat.id > 0) {
                categoryName = QString::fromStdWString(cat.name);
            }
        }
        if (categoryName.isEmpty()) {
            categoryName = QFileInfo(existingPath).dir().dirName();
        }
        if (categoryName.isEmpty()) {
            categoryName = "未命名文件夹";
        }

        QMetaObject::invokeMethod(QCoreApplication::instance(), [&]() {
            QWidget* parent = nullptr;
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (w->inherits("QMainWindow") || w->objectName() == "MainWindow") {
                    parent = w;
                    break;
                }
            }
            DuplicatePromptDialog dlg(existingPath, srcPath, categoryName, parent);
            if (dlg.exec() == QDialog::Accepted) {
                userDecision = dlg.getDecision();
            } else {
                userDecision = -1;
            }
        }, Qt::BlockingQueuedConnection);

        if (userDecision == -1) {
            return false;
        } else if (userDecision == 0) {
            // 使用已存在文件导入：不复制物理文件，如果 targetCatId > 0，绑定它即可
            if (targetCatId > 0) {
                CategoryRepo::addItemToCategory(targetCatId, existingFid, existingPath.toStdWString());
            }
            return true;
        }
    }

    // 1. 产生 13 位唯一 Base36 ID
    QString fileId = ShellHelper::generateBase36Id();

    // 2. 建立 [ID].arc 文件夹容器
    QString containerDir = managedRoot + "/" + fileId + ".arc";
    if (!QDir().mkpath(containerDir)) return false;

    // 3. 将文件复制/移动进容器 (支持跨盘物理搬运)
    QString fileName = srcInfo.fileName();
    QString destPath = containerDir + "/" + fileName;

    // 同一硬盘下无论是粘贴、拖拽导入都必须采用剪切、移动方式
    bool sameDrive = (srcInfo.absoluteFilePath().left(1).toUpper() == QString(containerDir).left(1).toUpper());
    bool copied = false;
    if (sameDrive) {
        if (QFile::rename(srcPath, destPath)) {
            copied = true;
        } else if (QFile::copy(srcPath, destPath)) {
            copied = true;
            QFile::remove(srcPath);
        }
    } else {
        if (QFile::copy(srcPath, destPath)) {
            copied = true;
        }
    }

    if (!copied) {
        QDir(containerDir).removeRecursively();
        return false;
    }

    // 4. 提取 256x256 高清预渲染缩略图 [baseName]_thumbnail.png
    QImage thumb = WindowsShellThumbnailProvider::getShellThumbnail(destPath, 256);
    if (!thumb.isNull()) {
        QString baseName = QFileInfo(fileName).completeBaseName();
        thumb.save(containerDir + "/" + baseName + "_thumbnail.png", "PNG");
    }

    // 5. 写入数据库：将整个 .arc 资产包文件夹作为唯一的受控资产单位进行激活和登记！
    // 完美契合“1个 .arc 包 = 1个条目”的核心准则，杜绝包内原始文件重复计账导致的 FID 断层假死现象。
    std::wstring wContainerPath = QDir::toNativeSeparators(containerDir).toStdWString();
    MetadataManager::instance().ensureActivated(wContainerPath);

    // 更新 added_at 为当前毫秒时间戳
    long long nowMsecs = QDateTime::currentMSecsSinceEpoch();
    MetadataManager::instance().setAddedAt(wContainerPath, nowMsecs, false);

    // 🚨 显式补充 SQLite 数据库持久化落盘，确保新创建的 .arc 资产包被正式写入 SQLite 的 metadata 表，使得刷新后可以精准查出并刷新卡片
    MetadataManager::instance().persistAsync(wContainerPath, false, true);

    // 从容器路径中反查其实际的物理 File ID，用以执行 100% 精准的逻辑分类绑定
    std::string actualContainerFid = MetadataManager::instance().getFileIdSync(wContainerPath);

    // 6. 分类归纳
    // 如果 targetCatId > 0，绑定它
    if (targetCatId > 0 && !actualContainerFid.empty()) {
        CategoryRepo::addItemToCategory(targetCatId, actualContainerFid, wContainerPath);
    }

    return true;
}

bool AssetImporter::importDirectoryRecursive(const QString& srcDir,
                                             int parentCatId,
                                             const QString& managedRoot) {
    QFileInfo dirInfo(srcDir);
    if (!dirInfo.exists() || !dirInfo.isDir()) return false;

    // 🚨 核心防线：.arc 容器已经是最终打包好的资产单元，绝不能被当作普通子目录再次创建分类/递归打包
    if (dirInfo.fileName().endsWith(".arc", Qt::CaseInsensitive)) {
        return false; // 视为已导入资产，跳过，不重复打包、不创建同名分类
    }

    // 1. 在 categories 树中递归新建逻辑分类
    Category cat;
    cat.parentId = parentCatId;
    cat.name = dirInfo.fileName().toStdWString();
    cat.color = CategoryRepo::getDefaultColor();
    if (!CategoryRepo::add(cat)) return false;

    // 2. 递归导入文件夹里的所有实体文件和子目录
    QDir dir(srcDir);
    QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& entry : entries) {
        if (entry.isFile()) {
            importSingleFile(entry.absoluteFilePath(), cat.id, managedRoot);
        } else if (entry.isDir()) {
            importDirectoryRecursive(entry.absoluteFilePath(), cat.id, managedRoot);
        }
    }

    return true;
}

} // namespace ArcMeta
