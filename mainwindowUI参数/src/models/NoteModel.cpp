#include "NoteModel.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QIcon>
#include "../ui/IconHelper.h"
#include "../ui/StringUtils.h"
#include "../core/DatabaseManager.h"
#include <QFileInfo>
#include <QBuffer>
#include <QPixmap>
#include <QByteArray>
#include <QUrl>
#include <QDir>
#include <QCoreApplication>

static QString getIconHtml(const QString& name, const QString& color) {
    QIcon icon = IconHelper::getIcon(name, color, 16);
    QPixmap pixmap = icon.pixmap(16, 16);
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return QString("<img src='data:image/png;base64,%1' width='16' height='16' style='vertical-align:middle;'>")
           .arg(QString(ba.toBase64()));
}

NoteModel::NoteModel(QObject* parent) : QAbstractListModel(parent) {
    updateCategoryMap();
}

int NoteModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return m_notes.count();
}

QVariant NoteModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_notes.count()) return QVariant();

    const QVariantMap& note = m_notes.at(index.row());
    switch (role) {
        case Qt::BackgroundRole:
            return QVariant(); // 强制不返回任何背景色，由 Delegate 控制
        case Qt::DecorationRole: {
            // 2026-03-11 按照用户要求，全局重定义图标颜色，确保色差 >= 60 度以实现快速识别
            QString type = note.value("item_type").toString();
            QString content = note.value("content").toString().trimmed();
            QString iconName = "text"; // Default
            QString iconColor = "#95A5A6"; // 文本：默认灰色

            // 优先依据 item_type 显示图标，确保即使内容不完整也能维持属性展示一致性
            if (type == "link") {
                iconName = "link";
                iconColor = "#17B345";
                return IconHelper::getIcon(iconName, iconColor, 32);
            } else if (type == "code") {
                iconName = "code";
                iconColor = "#00FF00";
                return IconHelper::getIcon(iconName, iconColor, 32);
            } else if (type == "color") {
                iconName = "palette";
                iconColor = content;
                return IconHelper::getIcon(iconName, iconColor, 32);
            }

            if (type == "image") {
                int id = note.value("id").toInt();
                if (m_thumbnailCache.contains(id)) return m_thumbnailCache[id];
                
                QImage img;
                img.loadFromData(note.value("data_blob").toByteArray());
                if (!img.isNull()) {
                    // [OPTIMIZATION] 缩略图缓存硬上限 (LRU 近似实现)
                    if (m_thumbnailCache.size() > 100) m_thumbnailCache.clear();
                    
                    QIcon thumb(QPixmap::fromImage(img.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                    m_thumbnailCache[id] = thumb;
                    return thumb;
                }
                iconName = "image";
                iconColor = "#FF00FF"; // 图片：洋红色 (Hue 300)
            } else if (type == "file" || type == "files" || type == "folder" || type == "folders") {
                if (type == "folder" || (!content.contains(";") && QFileInfo(content.trimmed().remove('\"').remove('\'')).isDir())) {
                    iconName = "folder";
                    iconColor = "#FF8C00"; // 单文件夹：橙色 (Hue 33)
                } else if (type == "folders") {
                    iconName = "folders_multiple";
                    iconColor = "#483D8B"; // 多文件夹：深紫蓝色 (Hue 248) - 与红色文件形成巨大反差
                } else if (content.contains(";")) {
                    iconName = "files_multiple";
                    iconColor = "#FF0000"; // 多文件：纯红色 (Hue 0)
                } else {
                    iconName = "file";
                    iconColor = "#FFFF00"; // 单文件：纯黄色 (Hue 60)
                }
            } else if (type == "ocr_text") {
                iconName = "screenshot_ocr";
                iconColor = "#00FFFF"; // OCR：纯青色 (Hue 180)
            } else if (type == "captured_message") {
                iconName = "message";
                iconColor = "#00FFFF"; // 采集消息：纯青色 (Hue 180)
            } else if (type == "local_file" || type == "local_batch") {
                iconName = (type == "local_file") ? "file_import" : "batch_import";
                iconColor = "#FFFF00"; // 本地导入文件：黄色
            } else if (type == "local_folder") {
                iconName = "folder_import";
                iconColor = "#FF8C00"; // 本地导入文件夹：橙色
            } else if (type == "color" || type == "palette") {
                iconName = "palette";
                iconColor = content; // 保持原有颜色
            } else if (type == "pixel_ruler") {
                iconName = "pixel_ruler";
                iconColor = "#FF5722"; // 像素尺：深橙
            } else {
                QString stripped = content.trimmed();
                QString cleanPath = stripped;
                if ((cleanPath.startsWith("\"") && cleanPath.endsWith("\"")) || 
                    (cleanPath.startsWith("'") && cleanPath.endsWith("'"))) {
                    cleanPath = cleanPath.mid(1, cleanPath.length() - 2);
                }

                QString plain = StringUtils::htmlToPlainText(content).trimmed();
                if (stripped.startsWith("http://") || stripped.startsWith("https://") || stripped.startsWith("www.")) {
                    iconName = "link";
                    iconColor = "#17B345"; // 链接：绿色 (2026-03-xx 用户修改)
                } else if (static const QRegularExpression hexColorRegex("^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$"); hexColorRegex.match(plain).hasMatch()) {
                    iconName = "palette";
                    iconColor = plain;
                } else if (plain.startsWith("import ") || plain.startsWith("class ") || 
                           plain.startsWith("def ") || plain.startsWith("function") || 
                           plain.startsWith("var ") || plain.startsWith("const ") ||
                           // 2026-03-xx 按照用户要求重构：不再仅靠单字符判断，改用结构化特征识别
                           (plain.startsWith("<") && [](){
                               static const QRegularExpression htmlTagRegex("^<(html|div|p|span|table|h[1-6]|script|!DOCTYPE|body|head)", QRegularExpression::CaseInsensitiveOption);
                               return htmlTagRegex;
                           }().match(plain).hasMatch()) ||
                           (plain.startsWith("{") && plain.contains("\":") && plain.contains("}"))) {
                    iconName = "code";
                    iconColor = "#00FF00"; // 代码：鲜绿色 (Hue 120)
                } else if (cleanPath.length() < 260 && (
                           (cleanPath.length() > 2 && cleanPath[1] == ':') || 
                           cleanPath.startsWith("\\\\") || cleanPath.startsWith("/") || 
                           cleanPath.startsWith("./") || cleanPath.startsWith("../"))) {
                    QFileInfo info(cleanPath);
                    if (info.exists()) {
                        if (info.isDir()) {
                            iconName = "folder";
                            iconColor = "#FF8C00"; // 自动识别文件夹：橙色
                        } else {
                            iconName = "file";
                            iconColor = "#FFFF00"; // 自动识别文件：黄色
                        }
                    }
                }
            }
            return IconHelper::getIcon(iconName, iconColor, 32);
        }
        case Qt::ToolTipRole: {
            int id = note.value("id").toInt();
            // [NOTE] 不再使用 tooltipCache，因为备注可能随时更新
            // 如需性能优化，可在备注更新时清除缓存

            QString title = note.value("title").toString();
            QString content = note.value("content").toString();
            QString remark = note.value("remark").toString().trimmed();
            int catId = note.value("category_id").toInt();
            QString tags = note.value("tags").toString();
            bool pinned = note.value("is_pinned").toBool();
            // 2026-03-xx 按照用户要求：废弃笔记级锁定逻辑
            bool favorite = note.value("is_favorite").toBool();
            int rating = note.value("rating").toInt();
            QString sourceApp = note.value("source_app").toString();

            QString catName = m_categoryMap.value(catId, "未分类");
            if (tags.isEmpty()) tags = "无";

            QString statusStr;
            if (pinned) statusStr += getIconHtml("pin_vertical", "#FF551C") + " 置顶 ";
            statusStr += getIconHtml(favorite ? "bookmark_filled" : "bookmark", favorite ? "#F2B705" : "#aaaaaa") + (favorite ? " 收藏 " : " 未收藏 ");

            if (statusStr.isEmpty()) statusStr = "无";

            QString ratingStr;
            for(int i=0; i<rating; ++i) ratingStr += getIconHtml("star_filled", "#f39c12") + " ";
            if (ratingStr.isEmpty()) ratingStr = "无";

            QString preview;
            if (note.value("item_type").toString() == "image") {
                QByteArray ba = note.value("data_blob").toByteArray();
                preview = QString("<img src='data:image/png;base64,%1' width='300'>").arg(QString(ba.toBase64()));
            } else {
                // 2026-03-15 按照用户意图：如果内容与标题重复，则不显示预览区，保持干练
                QString plainText = StringUtils::htmlToPlainText(content).trimmed();
                if (plainText != title.trimmed()) {
                    preview = plainText.left(400).toHtmlEscaped().replace("\n", "<br>").trimmed();
                    if (plainText.length() > 400) preview += "...";
                }
            }

            // 标题行（顶部突出显示）
            QString titleHtml;
            if (!title.isEmpty()) {
                titleHtml = QString("<div style='font-size: 13px; font-weight: bold; color: #fff; "
                                    "border-bottom: 1px solid #444; padding-bottom: 5px; margin-bottom: 5px;'>%1</div>")
                                .arg(title.toHtmlEscaped());
            }

            // 备注行（仅在有内容时显示）
            QString remarkRow;
            if (!remark.isEmpty()) {
                remarkRow = QString("<tr><td width='22'>%1</td><td><b>备注:</b> "
                                    "<span style='color:#b3e5fc;'>%2</span></td></tr>")
                                .arg(getIconHtml("edit", "#4fc3f7"),
                                     remark.left(120).toHtmlEscaped().replace("\n", "<br>")
                                     + (remark.length() > 120 ? "..." : ""));
            }

            QString previewHtml;
            if (!preview.isEmpty()) {
                previewHtml = QString("<hr style='border: 0; border-top: 1px solid #555; margin: 5px 0;'>"
                                      "<div style='color: #ccc; font-size: 12px; line-height: 1.4;'>%1</div>").arg(preview);
            }

            // 2026-03-15 按照用户意图：从 ToolTip 中移除“来源”字段
            QString html = QString("<html><body style='color: #ddd;'>"
                           "%1"
                           "<table border='0' cellpadding='2' cellspacing='0'>"
                           "<tr><td width='22'>%2</td><td><b>分类:</b> %3</td></tr>"
                           "<tr><td width='22'>%4</td><td><b>标签:</b> %5</td></tr>"
                           "<tr><td width='22'>%6</td><td><b>评级:</b> %7</td></tr>"
                           "<tr><td width='22'>%8</td><td><b>状态:</b> %9</td></tr>"
                           "%10"
                           "</table>"
                           "%11"
                           "</body></html>")
                .arg(titleHtml,
                     getIconHtml("branch", "#4a90e2"), catName,
                     getIconHtml("tag", "#FFAB91"), tags,
                     getIconHtml("star", "#f39c12"), ratingStr,
                     getIconHtml("pin_tilted", "#aaa"), statusStr,
                     remarkRow, previewHtml);
            
            // [OPTIMIZATION] ToolTip 缓存硬上限
            if (m_tooltipCache.size() > 100) m_tooltipCache.clear();
            m_tooltipCache[id] = html;
            return html;
        }
        case Qt::DisplayRole: {
            // [MODIFIED] 2026-03-11 底层清算：DisplayRole 严禁直接返回标题，防止 Qt 默认复制逻辑回退抓取标题。
            // 同时为了保证 UI 列表项不为空白，非文本项将显示内容路径或类型占位符。
            QString type = note.value("item_type").toString();
            QString content = note.value("content").toString();
            if (type == "text" || type.isEmpty() || type == "ocr_text" || type == "captured_message" || 
                type == "file" || type == "folder" || type == "files" || type == "folders") {
                QString plain = StringUtils::htmlToPlainText(content);
                QString display = plain.replace('\n', ' ').replace('\r', ' ').trimmed().left(150);
                if (!display.isEmpty()) return display;
            }
            if (type == "image") return QString("[图片]");
            return QString();
        }
        case TitleRole:
            return note.value("title");
        case ContentRole:
            return note.value("content");
        case IdRole:
            return note.value("id");
        case TagsRole:
            return note.value("tags");
        case TimeRole:
            return note.value("updated_at");
        case PinnedRole:
            return note.value("is_pinned");
        case LockedRole:
            // 2026-03-xx 按照用户要求：废弃笔记级锁定逻辑，固定返回 false
            return false;
        case FavoriteRole:
            return note.value("is_favorite");
        case TypeRole:
            return note.value("item_type");
        case RatingRole:
            return note.value("rating");
        case CategoryIdRole:
            return note.value("category_id");
        case CategoryNameRole:
            return m_categoryMap.value(note.value("category_id").toInt(), "未分类");
        case ColorRole:
            return note.value("color");
        case SourceAppRole:
            return note.value("source_app");
        case SourceTitleRole:
            return note.value("source_title");
        case BlobRole:
            return note.value("data_blob");
        case RemarkRole:
            return note.value("remark");
        case PlainContentRole: {
            // [PERF] 极致性能优化：优先使用预处理缓存，彻底消除 Delegate 渲染时的 HTML 解析开销。
            int id = note.value("id").toInt();
            if (m_plainContentCache.contains(id)) return m_plainContentCache[id];
            
            // [OPTIMIZATION] 纯文本缓存硬上限
            if (m_plainContentCache.size() > 500) m_plainContentCache.clear();

            QString content = note.value("content").toString();
            QString plain = StringUtils::htmlToPlainText(content).simplified();
            m_plainContentCache[id] = plain;
            return plain;
        }
        default:
            return QVariant();
    }
}

Qt::ItemFlags NoteModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::ItemIsEnabled;
    return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList NoteModel::mimeTypes() const {
    // 【核心修复】优先级调整：text/plain 必须放在第一位，确保浏览器等外部应用优先识别
    return {"text/plain", "text/html", "text/uri-list", "application/x-note-ids"};
}

QMimeData* NoteModel::mimeData(const QModelIndexList& indexes) const {
    // [MODIFIED] 2026-03-11 按照用户要求，重构底层导出逻辑：内容优先策略，排除标题。
    QMimeData* mimeData = new QMimeData();
    QStringList ids;
    QStringList plainTexts;
    QStringList htmlTexts;
    QList<QUrl> urls;
    QImage firstImage;

    for (const QModelIndex& index : indexes) {
        if (index.isValid()) {
            ids << QString::number(data(index, IdRole).toInt());
            
            QString content = data(index, ContentRole).toString();
            QString type = data(index, TypeRole).toString();
            QByteArray blob = data(index, BlobRole).toByteArray();
            
            if (type == "image") {
                // 支持图片导出
                if (firstImage.isNull()) {
                    firstImage.loadFromData(blob);
                }
                plainTexts << "[图片数据]";
            } else if (type == "file" || type == "folder" || type == "files" || type == "folders" || type == "local_file" || type == "local_folder" || type == "local_batch") {
                // 支持文件路径导出
                QStringList rawPaths;
                if (type == "local_batch") {
                    QString fullPath = content;
                    if (content.startsWith("attachments/")) fullPath = QCoreApplication::applicationDirPath() + "/" + content;
                    QDir dir(fullPath);
                    for (const QString& f : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) rawPaths << dir.absoluteFilePath(f);
                } else {
                    rawPaths = content.split(';', Qt::SkipEmptyParts);
                }

                for (const QString& p : rawPaths) {
                    QString fullPath = p.trimmed().remove('\"');
                    if (p.startsWith("attachments/")) fullPath = QCoreApplication::applicationDirPath() + "/" + p;
                    if (QFileInfo::exists(fullPath)) {
                        urls << QUrl::fromLocalFile(fullPath);
                    }
                }
                plainTexts << content;
            } else {
                // 文本相关类型
                if (StringUtils::isHtml(content)) {
                    plainTexts << StringUtils::htmlToPlainText(content);
                    htmlTexts << content;
                } else {
                    plainTexts << content;
                    htmlTexts << content.toHtmlEscaped().replace("\n", "<br>");
                }
            }
        }
    }
    
    mimeData->setData("application/x-note-ids", ids.join(",").toUtf8());
    if (!firstImage.isNull()) {
        mimeData->setImageData(firstImage);
    }
    
    if (!plainTexts.isEmpty()) {
        // 1. 设置纯文本格式 (使用 \r\n 换行)
        QString combinedPlain = plainTexts.join("\n---\n").replace("\n", "\r\n");
        mimeData->setText(combinedPlain);
        
        // 2. 仅在确实包含 HTML 内容时提供 HTML 分支，防止纯文本拖拽时出现 HTML 源码泄漏
        bool hasActualHtml = false;
        for (const QModelIndex& index : indexes) {
            if (StringUtils::isHtml(data(index, ContentRole).toString())) {
                hasActualHtml = true;
                break;
            }
        }

        if (hasActualHtml) {
            if (indexes.size() == 1) {
                mimeData->setHtml(data(indexes.first(), ContentRole).toString());
            } else {
                QString combinedHtml = htmlTexts.join("<br><hr><br>");
                mimeData->setHtml(QString(
                    "<html>"
                    "<head><meta charset='utf-8'></head>"
                    "<body>%1</body>"
                    "</html>"
                ).arg(combinedHtml));
            }
        }
    }
    
    if (!urls.isEmpty()) {
        mimeData->setUrls(urls);
    }
    
    return mimeData;
}

void NoteModel::setNotes(const QList<QVariantMap>& notes) {
    updateCategoryMap();
    m_thumbnailCache.clear();
    m_tooltipCache.clear();
    m_plainContentCache.clear(); // 列表重置时清理缓存，确保数据一致性
    beginResetModel();
    m_notes = notes;
    endResetModel();
}

void NoteModel::updateCategoryMap() {
    auto categories = DatabaseManager::instance().getAllCategories();
    m_categoryMap.clear();
    for (const auto& cat : categories) {
        m_categoryMap[cat["id"].toInt()] = cat["name"].toString();
    }
}

// 【新增】函数的具体实现
void NoteModel::prependNote(const QVariantMap& note) {
    // 通知视图：我要在第0行插入1条数据
    beginInsertRows(QModelIndex(), 0, 0);
    m_notes.prepend(note);
    endInsertRows();
}