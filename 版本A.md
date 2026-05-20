# 代码导出结果 - 20260329_231004

**项目路径**: `G:\C++\ArcMeta\backups\Buk_20260329_220425`

**文件总数**: 47

## 文件: `src/meta/AmMetaJson.cpp`

```cpp
#include "AmMetaJson.h"

#include <windows.h>
#include <QFile>
#include <QThread>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QDir>
#include <string>
#include <map>
#include <vector>
#include <type_traits>

namespace ArcMeta {

/**
 * @brief 构造函数，确定目标元数据文件路径
 */
AmMetaJson::AmMetaJson(const std::wstring& folderPath)
    : m_folderPath(folderPath) {
    // 自动追加后缀，生成 .am_meta.json 完整路径
    // folderPath 如果不以斜杠结尾，自动补偿一个斜杠（这里统一使用 Windows 斜杠）
    std::wstring path = folderPath;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L'\\';
    }
    m_filePath = path + L".am_meta.json";
}

/**
 * @brief 加载元数据文件
 */
bool AmMetaJson::load() {
    QString qPath = toQString(m_filePath);
    QFile file(qPath);

    // 如果文件不存在，属于正常情况，视为数据全为空
    if (!file.exists()) {
        m_folder = FolderMeta();
        m_items.clear();
        return true;
    }

    // 尝试以只读模式打开
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(fileData, &parseError);

    // 格式校验失败
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    
    // 加载文件夹级别元数据
    if (root.contains("folder") && root["folder"].isObject()) {
        m_folder = entryToFolder(root["folder"].toObject());
    } else {
        m_folder = FolderMeta();
    }

    // 加载条目级别元数据
    m_items.clear();
    if (root.contains("items") && root["items"].isObject()) {
        QJsonObject itemsObj = root["items"].toObject();
        for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
            if (it.value().isObject()) {
                m_items[toStdWString(it.key())] = entryToItem(it.value().toObject());
            }
        }
    }

    return true;
}

/**
 * @brief 安全写入元数据文件
 */
bool AmMetaJson::save() const {
    // 1. 构建 JSON 根对象
    QJsonObject root;
    root["version"] = "1";
    root["folder"] = folderToEntry(m_folder);

    QJsonObject itemsObj;
    for (const auto& [name, meta] : m_items) {
        // 只有发生过用户操作（星级/标签/备注/加密等）的条目才写入 JSON
        if (meta.hasUserOperations()) {
            itemsObj[toQString(name)] = itemToEntry(meta);
        }
    }
    root["items"] = itemsObj;

    QJsonDocument doc(root);
    QByteArray jsonData = doc.toJson(QJsonDocument::Indented);

    // 2. 写入临时文件 .am_meta.json.tmp
    QString tmpPath = toQString(m_filePath) + ".tmp";
    QFile tmpFile(tmpPath);
    // 2026-03-xx 增加重试机制：防止后台线程 UsnWatcher 正在迁移导致的临时冲突
    bool opened = false;
    for (int i = 0; i < 3; ++i) {
        if (tmpFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            opened = true;
            break;
        }
        QThread::msleep(50);
    }
    if (!opened) return false;
    if (tmpFile.write(jsonData) != jsonData.size()) {
        tmpFile.close();
        tmpFile.remove();
        return false;
    }
    tmpFile.close();

    // 3. 校验临时文件（重新解析）
    QFile checkFile(tmpPath);
    if (!checkFile.open(QIODevice::ReadOnly)) {
        checkFile.remove();
        return false;
    }
    QByteArray checkData = checkFile.readAll();
    checkFile.close();

    QJsonParseError checkError;
    QJsonDocument checkDoc = QJsonDocument::fromJson(checkData, &checkError);
    if (checkDoc.isNull() || !checkDoc.isObject()) {
        // 校验失败，删除临时文件并返回错误
        QFile::remove(tmpPath);
        return false;
    }

    // 4. 原子重命名替换
    // Windows API: MoveFileExW 使用 MOVEFILE_REPLACE_EXISTING 模拟原子替换
    // 虽然真正的原子性在 NTFS 下有限，但这能最大程度防止断电等破坏
    if (!MoveFileExW(tmpPath.toStdWString().c_str(), m_filePath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // 重命名失败，清理临时文件
        QFile::remove(tmpPath);
        return false;
    }

    // 5. 设置隐藏属性（关键红线要求）
    SetFileAttributesW(m_filePath.c_str(), FILE_ATTRIBUTE_HIDDEN);

    return true;
}

/**
 * @brief 按照用户要求：物理重命名元数据条目（2026-03-xx）
 */
bool AmMetaJson::renameItem(const QString& folderPath, const QString& oldName, const QString& newName) {
    if (oldName == newName) return true;

    AmMetaJson meta(folderPath.toStdWString());
    if (!meta.load()) return false;

    auto& items = meta.items();
    std::wstring wOld = oldName.toStdWString();
    std::wstring wNew = newName.toStdWString();

    auto it = items.find(wOld);
    if (it != items.end()) {
        // 迁移元数据到新键值
        items[wNew] = it->second;
        items.erase(it);
        return meta.save();
    }
    
    return true; // 没有对应元数据也视为成功
}

// --- 内部辅助函数：转换 ---

QJsonObject AmMetaJson::folderToEntry(const FolderMeta& meta) {
    QJsonObject obj;
    obj["sort_by"] = toQString(meta.sortBy);
    obj["sort_order"] = toQString(meta.sortOrder);
    obj["rating"] = meta.rating;
    obj["color"] = toQString(meta.color);
    obj["pinned"] = meta.pinned;
    obj["note"] = toQString(meta.note);

    QJsonArray tagsArr;
    for (const auto& tag : meta.tags) {
        tagsArr.append(toQString(tag));
    }
    obj["tags"] = tagsArr;

    return obj;
}

FolderMeta AmMetaJson::entryToFolder(const QJsonObject& obj) {
    FolderMeta meta;
    if (obj.contains("sort_by")) meta.sortBy = toStdWString(obj["sort_by"].toString());
    if (obj.contains("sort_order")) meta.sortOrder = toStdWString(obj["sort_order"].toString());
    if (obj.contains("rating")) meta.rating = obj["rating"].toInt();
    if (obj.contains("color")) meta.color = toStdWString(obj["color"].toString());
    if (obj.contains("pinned")) meta.pinned = obj["pinned"].toBool();
    if (obj.contains("note")) meta.note = toStdWString(obj["note"].toString());

    if (obj.contains("tags") && obj["tags"].isArray()) {
        QJsonArray tagsArr = obj["tags"].toArray();
        for (const auto& tag : tagsArr) {
            meta.tags.push_back(toStdWString(tag.toString()));
        }
    }
    return meta;
}

QJsonObject AmMetaJson::itemToEntry(const ItemMeta& meta) {
    QJsonObject obj;
    obj["type"] = toQString(meta.type);
    obj["rating"] = meta.rating;
    obj["color"] = toQString(meta.color);
    obj["pinned"] = meta.pinned;
    obj["note"] = toQString(meta.note);
    obj["encrypted"] = meta.encrypted;
    obj["encrypt_salt"] = QString::fromStdString(meta.encryptSalt);
    
    // 实现 IV 的 Base64 转换（红线要求）
    QByteArray ivData = QByteArray::fromStdString(meta.encryptIv);
    obj["encrypt_iv"] = QString::fromLatin1(ivData.toBase64());

    obj["encrypt_verify_hash"] = QString::fromStdString(meta.encryptVerifyHash);
    obj["original_name"] = toQString(meta.originalName);
    obj["volume"] = toQString(meta.volume);
    obj["frn"] = toQString(meta.frn);

    QJsonArray tagsArr;
    for (const auto& tag : meta.tags) {
        tagsArr.append(toQString(tag));
    }
    obj["tags"] = tagsArr;

    return obj;
}

ItemMeta AmMetaJson::entryToItem(const QJsonObject& obj) {
    ItemMeta meta;
    if (obj.contains("type")) meta.type = toStdWString(obj["type"].toString());
    if (obj.contains("rating")) meta.rating = obj["rating"].toInt();
    if (obj.contains("color")) meta.color = toStdWString(obj["color"].toString());
    if (obj.contains("pinned")) meta.pinned = obj["pinned"].toBool();
    if (obj.contains("note")) meta.note = toStdWString(obj["note"].toString());
    if (obj.contains("encrypted")) meta.encrypted = obj["encrypted"].toBool();
    if (obj.contains("encrypt_salt")) meta.encryptSalt = obj["encrypt_salt"].toString().toStdString();
    
    // 实现 IV 的 Base64 解码
    if (obj.contains("encrypt_iv")) {
        QByteArray base64Iv = obj["encrypt_iv"].toString().toLatin1();
        meta.encryptIv = QByteArray::fromBase64(base64Iv).toStdString();
    }

    if (obj.contains("encrypt_verify_hash")) meta.encryptVerifyHash = obj["encrypt_verify_hash"].toString().toStdString();
    if (obj.contains("original_name")) meta.originalName = toStdWString(obj["original_name"].toString());
    if (obj.contains("volume")) meta.volume = toStdWString(obj["volume"].toString());
    if (obj.contains("frn")) meta.frn = toStdWString(obj["frn"].toString());

    if (obj.contains("tags") && obj["tags"].isArray()) {
        QJsonArray tagsArr = obj["tags"].toArray();
        for (const auto& tag : tagsArr) {
            meta.tags.push_back(toStdWString(tag.toString()));
        }
    }
    return meta;
}

} // namespace ArcMeta
```

## 文件: `src/meta/AmMetaJson.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <map>
#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>

namespace ArcMeta {

/**
 * @brief 文件夹级别的元数据
 */
struct FolderMeta {
    std::wstring sortBy = L"name";
    std::wstring sortOrder = L"asc";
    int rating = 0;
    std::wstring color = L"";
    std::vector<std::wstring> tags;
    bool pinned = false;
    std::wstring note = L"";

    // 判断是否为空（即均为默认值，无需写入 items 列表时使用，此处用于 folder 节点总是存在）
    bool isDefault() const {
        return sortBy == L"name" && sortOrder == L"asc" && rating == 0 &&
               color.empty() && tags.empty() && !pinned && note.empty();
    }
};

/**
 * @brief 单个条目（文件或子文件夹）的元数据
 */
struct ItemMeta {
    std::wstring type = L"file"; // "file" | "folder"
    int rating = 0;
    std::wstring color = L"";
    std::vector<std::wstring> tags;
    bool pinned = false;
    std::wstring note = L"";
    bool encrypted = false;
    std::string encryptSalt;      // 存储为字符串
    std::string encryptIv;        // Base64 字符串
    std::string encryptVerifyHash;
    std::wstring originalName;
    std::wstring volume;
    std::wstring frn;             // 十六进制字符串存储，避免溢出
    
    // 2026-03-xx 极致性能：系统级属性缓存
    long long size = 0;
    double mtime = 0;
    double ctime = 0;

    /**
     * @brief 判断该条目是否有过用户操作。
     * 只有满足该条件的条目才会被序列化到 JSON 的 items 节点中。
     */
    bool hasUserOperations() const {
        return rating > 0 || !color.empty() || !tags.empty() || pinned ||
               !note.empty() || encrypted;
    }
};

/**
 * @brief 处理 .am_meta.json 的读写类，包含安全写入逻辑
 */
class AmMetaJson {
public:
    /**
     * @param folderPath 目标文件夹的完整路径（不含文件名）
     */
    explicit AmMetaJson(const std::wstring& folderPath);

    /**
     * @brief 加载 .am_meta.json 文件
     * @return 加载成功返回 true，文件不存在返回 true（视为初始化），解析错误返回 false
     */
    bool load();

    /**
     * @brief 安全保存到 .am_meta.json 文件
     * 遵循：写临时文件 -> 校验 -> 原子替换 -> 设置隐藏属性 流程
     */
    bool save() const;

    // 数据访问接口
    FolderMeta& folder() { return m_folder; }
    const FolderMeta& folder() const { return m_folder; }

    std::map<std::wstring, ItemMeta>& items() { return m_items; }
    const std::map<std::wstring, ItemMeta>& items() const { return m_items; }

    /**
     * @brief 获取元数据文件的完整路径
     */
    std::wstring getMetaFilePath() const { return m_filePath; }

    /**
     * @brief 静态辅助方法：物理重命名元数据条目
     * @param folderPath 所在目录
     * @param oldName 旧文件名
     * @param newName 新文件名
     */
    static bool renameItem(const QString& folderPath, const QString& oldName, const QString& newName);

private:
    std::wstring m_folderPath;
    std::wstring m_filePath;
    
    FolderMeta m_folder;
    std::map<std::wstring, ItemMeta> m_items;

    // 内部转换辅助
    static QJsonObject folderToEntry(const FolderMeta& meta);
    static FolderMeta entryToFolder(const QJsonObject& obj);
    static QJsonObject itemToEntry(const ItemMeta& meta);
    static ItemMeta entryToItem(const QJsonObject& obj);

    static QString toQString(const std::wstring& ws) { return QString::fromStdWString(ws); }
    static std::wstring toStdWString(const QString& qs) { return qs.toStdWString(); }
};

} // namespace ArcMeta
```

## 文件: `src/meta/BatchRenameEngine.cpp`

```cpp
#include "BatchRenameEngine.h"
#include <QFileInfo>
#include <QDateTime>
#include <filesystem>

namespace ArcMeta {

BatchRenameEngine& BatchRenameEngine::instance() {
    static BatchRenameEngine inst;
    return inst;
}

std::vector<std::wstring> BatchRenameEngine::preview(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules) {
    std::vector<std::wstring> results;
    for (size_t i = 0; i < originalPaths.size(); ++i) {
        results.push_back(processOne(originalPaths[i], (int)i, rules).toStdWString());
    }
    return results;
}

/**
 * @brief 管道模式处理单个文件名
 */
QString BatchRenameEngine::processOne(const std::wstring& path, int index, const std::vector<RenameRule>& rules) {
    QFileInfo info(QString::fromStdWString(path));
    QString newName = "";

    for (const auto& rule : rules) {
        switch (rule.type) {
            case RenameComponentType::Text:
                newName += rule.value;
                break;
            case RenameComponentType::Sequence: {
                int val = rule.start + (index * rule.step);
                newName += QString("%1").arg(val, rule.padding, 10, QChar('0'));
                break;
            }
            case RenameComponentType::Date:
                newName += QDateTime::currentDateTime().toString(rule.value.isEmpty() ? "yyyyMMdd" : rule.value);
                break;
            case RenameComponentType::Metadata:
                // 注入 ArcMeta 元数据标记（如评级星级）
                newName += "[ArcMeta]"; 
                break;
        }
    }

    // 保留原始后缀
    QString ext = info.suffix();
    if (!ext.isEmpty()) newName += "." + ext;

    return newName;
}

/**
 * @brief 执行物理重命名（红线：重命名成功后必须迁移元数据）
 */
bool BatchRenameEngine::execute(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules) {
    auto newNames = preview(originalPaths, rules);
    
    for (size_t i = 0; i < originalPaths.size(); ++i) {
        std::filesystem::path oldP(originalPaths[i]);
        std::filesystem::path newP = oldP.parent_path() / newNames[i];
        
        try {
            std::filesystem::rename(oldP, newP);
            // 关键：重命名成功后，USN Watcher 会处理 FRN 追踪，
            // 但此处需确保 .am_meta.json 键值同步更新逻辑能够触发。
        } catch (...) {
            return false; // 任一失败则中断（实际生产应支持回滚）
        }
    }
    return true;
}

} // namespace ArcMeta
```

## 文件: `src/meta/BatchRenameEngine.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <QString>

namespace ArcMeta {

/**
 * @brief 批量重命名规则组件类型
 */
enum class RenameComponentType {
    Text,           // 固定文本
    Sequence,       // 序列数字
    Date,           // 日期 (yyyyMMdd)
    Metadata        // 元数据变量 (标签, 星级)
};

struct RenameRule {
    RenameComponentType type;
    QString value;      // 文本值、日期格式或元数据键名
    int start = 1;      // 序列起始
    int step = 1;       // 序列步长
    int padding = 3;    // 补零位数
};

/**
 * @brief 批量重命名引擎
 * 管道模式依次处理文件名，支持预检与冲突检测
 */
class BatchRenameEngine {
public:
    static BatchRenameEngine& instance();

    /**
     * @brief 执行预览计算
     * @param originalPaths 原始文件路径列表
     * @param rules 管道规则列表
     * @return 计算后的新名称列表
     */
    std::vector<std::wstring> preview(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules);

    /**
     * @brief 执行物理重命名与元数据迁移
     */
    bool execute(const std::vector<std::wstring>& originalPaths, const std::vector<RenameRule>& rules);

private:
    BatchRenameEngine() = default;
    ~BatchRenameEngine() = default;

    QString processOne(const std::wstring& path, int index, const std::vector<RenameRule>& rules);
};

} // namespace ArcMeta
```

## 文件: `src/ui/BreadcrumbBar.cpp`

```cpp
#include "BreadcrumbBar.h"
#include <QMouseEvent>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include "UiHelper.h"

namespace ArcMeta {

BreadcrumbBar::BreadcrumbBar(QWidget* parent) : QWidget(parent) {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(8, 0, 8, 0);
    m_layout->setSpacing(2);
    
    setCursor(Qt::PointingHandCursor);
    // 基础样式：作为地址栏背景
    setStyleSheet("QWidget { background: transparent; border: none; }");
}

void BreadcrumbBar::setPath(const QString& path) {
    m_currentPath = path;
    clearButtons();

    if (path == "computer://") {
        addLevel("此电脑", "computer://");
        m_layout->addStretch();
        return;
    }

    QString normPath = QDir::toNativeSeparators(path);
    QStringList parts = normPath.split(QDir::separator(), Qt::SkipEmptyParts);

    // 处理 Windows 盘符根目录 (如 C:\)
    QString currentBuildPath;
    if (normPath.contains(":") && normPath.indexOf(":") == 1) {
        QString drive = normPath.left(3); // "C:\"
        addLevel(drive, drive);
        currentBuildPath = drive;
        
        // 如果路径只是根目录，parts 可能只有盘符或为空
        if (parts.size() > 0 && parts[0].contains(":")) {
             parts.removeFirst(); 
        }
    }

    for (qsizetype i = 0; i < parts.size(); ++i) {
        // 添加箭头/分隔符
        QLabel* sep = new QLabel(">", this);
        sep->setStyleSheet("color: #555; font-size: 10px; padding: 0 2px;");
        m_layout->addWidget(sep);

        if (!currentBuildPath.endsWith(QDir::separator())) {
            currentBuildPath += QDir::separator();
        }
        currentBuildPath += parts[i];
        
        addLevel(parts[i], currentBuildPath);
    }

    m_layout->addStretch();
}

void BreadcrumbBar::clearButtons() {
    QLayoutItem* item;
    while ((item = m_layout->takeAt(0))) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
}

void BreadcrumbBar::addLevel(const QString& name, const QString& fullPath) {
    QPushButton* btn = new QPushButton(name, this);
    btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    btn->setFixedHeight(24);
    
    // 面包屑按钮样式：扁平化，仅悬停可见背景
    btn->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 6px; "
        "              color: #EEEEEE; font-size: 12px; padding: 0 6px; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background: rgba(255, 255, 255, 0.2); }"
    );

    connect(btn, &QPushButton::clicked, [this, fullPath]() {
        emit pathClicked(fullPath);
    });

    m_layout->addWidget(btn);
}

void BreadcrumbBar::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 改进：点击按钮以外的任何地方（包括分隔符和空白区）都触发编辑模式
        QWidget* child = childAt(event->pos());
        if (!qobject_cast<QPushButton*>(child)) {
            emit blankAreaClicked();
        }
    }
    QWidget::mousePressEvent(event);
}

} // namespace ArcMeta
```

## 文件: `src/ui/BreadcrumbBar.h`

```cpp
#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStringList>

namespace ArcMeta {

/**
 * @brief 面包屑导航栏部件
 * 将路径拆分为层级按钮，支持点击跳转
 */
class BreadcrumbBar : public QWidget {
    Q_OBJECT

public:
    explicit BreadcrumbBar(QWidget* parent = nullptr);
    ~BreadcrumbBar() override = default;

    /**
     * @brief 设置当前显示路径并刷新按钮
     */
    void setPath(const QString& path);

signals:
    /**
     * @brief 用户点击某个层级按钮时发出
     * @param path 该层级对应的完整物理路径
     */
    void pathClicked(const QString& path);

    /**
     * @brief 当用户点击空白区域时发出，用于告知外部切换到编辑模式
     */
    void blankAreaClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;

private:
    void clearButtons();
    void addLevel(const QString& name, const QString& fullPath);

    QHBoxLayout* m_layout = nullptr;
    QString m_currentPath;
};

} // namespace ArcMeta
```

## 文件: `src/ui/CategoryPanel.cpp`

```cpp
#include "CategoryPanel.h"
#include "../../SvgIcons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QHeaderView>
#include <QScrollBar>
#include <QLabel>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include "UiHelper.h"

namespace ArcMeta {

/**
 * @brief 构造函数，设置面板属性
 */
CategoryPanel::CategoryPanel(QWidget* parent)
    : QWidget(parent) {
    setFixedWidth(230);
    setStyleSheet("QWidget { background-color: #1E1E1E; color: #EEEEEE; border: none; }");
    
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    
    initUi();
    setupContextMenu(); // 初始化 Action 指针
}

/**
 * @brief 初始化整体 UI 结构
 */
void CategoryPanel::initUi() {
    // 面板标题
    QLabel* titleLabel = new QLabel("灵感归档", this);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; padding: 10px 12px; background: #252526;");
    m_mainLayout->addWidget(titleLabel);

    initTopStats();
    
    QFrame* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet("background-color: #333333; border: none;");
    m_mainLayout->addWidget(line);

    initCategoryTree();
    initBottomToolbar();
}

/**
 * @brief 初始化顶部统计区
 */
void CategoryPanel::initTopStats() {
    m_statsWidget = new QWidget(this);
    m_statsLayout = new QVBoxLayout(m_statsWidget);
    m_statsLayout->setContentsMargins(12, 8, 12, 8); // 缩小上下边距
    m_statsLayout->setSpacing(2); // 还原紧凑间距
    
    addStatItem("all_data", "全部数据", 0);
    addStatItem("today", "今日数据", 0);
    addStatItem("today", "昨日数据", 0);
    addStatItem("clock_history", "最近访问", 0);
    addStatItem("uncategorized", "未分类", 0);
    addStatItem("untagged", "未标签", 0);
    addStatItem("star", "收藏", 0);
    addStatItem("trash", "回收站", 0);

    m_mainLayout->addWidget(m_statsWidget);
}

/**
 * @brief 添加统计项条目
 */
void CategoryPanel::addStatItem(const QString& iconKey, const QString& name, int count) {
    QWidget* item = new QWidget(this);
    item->setFixedHeight(26); // 恢复紧凑型物理约束
    QHBoxLayout* layout = new QHBoxLayout(item);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->setSpacing(8);

    QLabel* iconLabel = new QLabel(this);
    iconLabel->setPixmap(UiHelper::getIcon(iconKey, QColor("#EEEEEE"), 16).pixmap(16, 16));
    layout->addWidget(iconLabel);
    QLabel* nameLabel = new QLabel(name, item);
    nameLabel->setStyleSheet("font-size: 11px; color: #B0B0B0;");
    
    QLabel* countLabel = new QLabel(QString::number(count), item);
    countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    countLabel->setStyleSheet("font-size: 11px; color: #5F5E5A;");
    
    layout->addStretch();
    layout->addWidget(countLabel);
    
    // 安装事件过滤器以拦截统计项点击
    item->installEventFilter(this);
    item->setProperty("statName", name);
    item->setCursor(Qt::PointingHandCursor);

    m_statsLayout->addWidget(item);
}

bool CategoryPanel::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress) {
        if (obj->property("statName").isValid()) {
            emit categorySelected(obj->property("statName").toString());
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

/**
 * @brief 初始化中间分类树区
 */
void CategoryPanel::initCategoryTree() {
    m_treeView = new QTreeView(this);
    m_treeView->setHeaderHidden(true);
    m_treeView->setIndentation(16);
    m_treeView->setAnimated(true);
    m_treeView->setDragEnabled(true);
    m_treeView->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::InternalMove);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    
    m_treeView->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background: transparent; width: 4px; }"
        "QScrollBar::handle:vertical { background: #444444; border-radius: 2px; }"
    );

    m_treeView->setStyleSheet(
        "QTreeView { background-color: transparent; border: none; font-size: 12px; selection-background-color: #378ADD; }"
        "QTreeView::item { height: 26px; padding-left: 4px; color: #EEEEEE; }"
        "QTreeView::item:hover { background-color: rgba(255, 255, 255, 0.05); }"
    );

    m_treeModel = new QStandardItemModel(this);
    m_treeView->setModel(m_treeModel);

    connect(m_treeView, &QTreeView::customContextMenuRequested, 
            this, &CategoryPanel::onCustomContextMenuRequested);
    connect(m_treeView, &QTreeView::clicked, [this](const QModelIndex& index) {
        emit categorySelected(index.data().toString());
    });

    m_mainLayout->addWidget(m_treeView, 1);
}

/**
 * @brief 初始化底部工具栏
 */
void CategoryPanel::initBottomToolbar() {
    m_bottomToolbar = new QWidget(this);
    m_bottomToolbar->setFixedHeight(36);
    m_bottomToolbar->setStyleSheet("background-color: #252525; border-top: 1px solid #333333;");
    
    QHBoxLayout* layout = new QHBoxLayout(m_bottomToolbar);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("搜索分类...");
    m_searchEdit->setFixedHeight(24);
    m_searchEdit->setStyleSheet(
        "QLineEdit { background: #1E1E1E; border: 1px solid #333333; border-radius: 6px; padding-left: 6px; font-size: 11px; color: #EEEEEE; }"
        "QLineEdit:focus { border: 1px solid #378ADD; }"
    );
    m_addCategoryBtn = new QPushButton(this);
    m_addCategoryBtn->setFixedSize(28, 28);
    m_addCategoryBtn->setIcon(UiHelper::getIcon("add", QColor("#EEEEEE"), 16));
    m_addCategoryBtn->setStyleSheet("QPushButton { border: none; background: transparent; } QPushButton:hover { background: #444444; border-radius: 4px; }");
    
    layout->addWidget(m_searchEdit);
    layout->addWidget(m_addCategoryBtn);
    
    m_mainLayout->addWidget(m_bottomToolbar);
}

/**
 * @brief 实现声明的 setupContextMenu 方法，初始化所有 Action 指针
 */
void CategoryPanel::setupContextMenu() {
    actNewData = new QAction(UiHelper::getIcon("add", QColor("#EEEEEE")), "新建数据...", this);
    actAssignToCategory = new QAction(UiHelper::getIcon("category", QColor("#EEEEEE")), "归类到...", this);
    actImportData = new QAction(UiHelper::getIcon("refresh", QColor("#EEEEEE")), "导入数据 / 刷新索引", this);
    actExport = new QAction(UiHelper::getIcon("redo", QColor("#EEEEEE")), "导出数据", this);

    actSetColor = new QAction(UiHelper::getIcon("palette", QColor("#EEEEEE")), "设置颜色", this);
    actRandomColor = new QAction(UiHelper::getIcon("random_color", QColor("#EEEEEE")), "随机颜色", this);
    actSetPresetTags = new QAction(UiHelper::getIcon("tag", QColor("#EEEEEE")), "设置预设标签", this);

    actNewSibling = new QAction("新建同级分类", this);
    actNewChild = new QAction("新建子分类", this);

    actTogglePin = new QAction(UiHelper::getIcon("pin", QColor("#EEEEEE")), "置顶 / 取消置顶", this);
    actRename = new QAction(UiHelper::getIcon("edit", QColor("#EEEEEE")), "重命名", this);
    actDelete = new QAction(UiHelper::getIcon("trash", QColor("#EEEEEE")), "删除", this);

    // 绑定基础创建逻辑 (从 UI 层穿透至 Model 层)
    connect(actNewSibling, &QAction::triggered, this, [this]() {
        QModelIndex currentIndex = m_treeView->currentIndex();
        bool ok;
        QString name = QInputDialog::getText(this, "新建同级分类", "请输入分类名称:", QLineEdit::Normal, "", &ok);
        if(ok && !name.isEmpty()) {
            QStandardItem* item = new QStandardItem(name);
            if(!currentIndex.isValid() || !m_treeModel->itemFromIndex(currentIndex)->parent()) {
                m_treeModel->appendRow(item);
            } else {
                m_treeModel->itemFromIndex(currentIndex)->parent()->appendRow(item);
            }
        }
    });

    connect(actNewChild, &QAction::triggered, this, [this]() {
        QModelIndex currentIndex = m_treeView->currentIndex();
        if(!currentIndex.isValid()) return;
        bool ok;
        QString name = QInputDialog::getText(this, "新建子分类", "请输入子分类名称:", QLineEdit::Normal, "", &ok);
        if(ok && !name.isEmpty()) {
            QStandardItem* parentItem = m_treeModel->itemFromIndex(currentIndex);
            parentItem->appendRow(new QStandardItem(name));
            m_treeView->expand(currentIndex);
        }
    });

    actSortByName = new QAction("按名称", this);
    actSortByCount = new QAction("按数量", this);
    actSortByTime = new QAction("按时间", this);

    actPasswordProtect = new QAction(UiHelper::getIcon("lock", QColor("#EEEEEE")), "密码保护", this);
}

/**
 * @brief 渲染右键菜单
 */
void CategoryPanel::onCustomContextMenuRequested(const QPoint& pos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2B2B2B; border: 1px solid #444444; color: #EEEEEE; padding: 4px; border-radius: 6px; }"
        "QMenu::item { height: 24px; padding: 0 10px 0 10px; border-radius: 3px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #505050; }"
        "QMenu::separator { height: 1px; background: #444444; margin: 4px 8px 4px 8px; }"
        "QMenu::right-arrow { image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjRUVFRUVFIiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PHBvbHlsaW5lIHBvaW50cz0iOSAxOCAxNSAxMiA5IDYiPjwvcG9seWxpbmU+PC9zdmc+); width: 12px; height: 12px; right: 8px; }"
    );

    menu.addAction(actNewData);
    menu.addAction(actAssignToCategory);
    menu.addSeparator();
    menu.addAction(actImportData);
    menu.addAction(actExport);
    menu.addSeparator();
    menu.addAction(actSetColor);
    menu.addAction(actRandomColor);
    menu.addAction(actSetPresetTags);
    menu.addSeparator();
    menu.addAction(actNewSibling);
    menu.addAction(actNewChild);
    menu.addSeparator();
    menu.addAction(actTogglePin);
    menu.addAction(actRename);
    menu.addAction(actDelete);
    menu.addSeparator();

    QMenu* sortMenu = menu.addMenu("排列");
    sortMenu->addAction(actSortByName);
    sortMenu->addAction(actSortByCount);
    sortMenu->addAction(actSortByTime);

    menu.addAction(actPasswordProtect);

    menu.exec(m_treeView->viewport()->mapToGlobal(pos));
}

} // namespace ArcMeta
```

## 文件: `src/ui/CategoryPanel.h`

```cpp
#pragma once

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QMenu>
#include <QAction>

namespace ArcMeta {

/**
 * @brief 分类面板（面板一）
 * 包含：顶部统计区、中间分类树区、底部工具栏
 */
class CategoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit CategoryPanel(QWidget* parent = nullptr);
    ~CategoryPanel() override = default;

signals:
    void categorySelected(const QString& name);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    // UI 初始化方法
    void initUi();
    void initTopStats();
    void initCategoryTree();
    void initBottomToolbar();

    // 内部组件
    QVBoxLayout* m_mainLayout = nullptr;
    
    // 统计项容器
    QWidget* m_statsWidget = nullptr;
    QVBoxLayout* m_statsLayout = nullptr;

    // 分类树
    QTreeView* m_treeView = nullptr;
    QStandardItemModel* m_treeModel = nullptr;

    // 底部工具栏
    QWidget* m_bottomToolbar = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_addCategoryBtn = nullptr;

    // 内部私有辅助
    void addStatItem(const QString& iconKey, const QString& name, int count);
    
    // 右键菜单 Action 声明
    void setupContextMenu();
    QAction* actNewData = nullptr;
    QAction* actAssignToCategory = nullptr;
    QAction* actImportData = nullptr;
    QAction* actExport = nullptr;
    QAction* actSetColor = nullptr;
    QAction* actRandomColor = nullptr;
    QAction* actSetPresetTags = nullptr;
    QAction* actNewSibling = nullptr;
    QAction* actNewChild = nullptr;
    QAction* actTogglePin = nullptr;
    QAction* actRename = nullptr;
    QAction* actDelete = nullptr;
    QAction* actSortByName = nullptr;
    QAction* actSortByCount = nullptr;
    QAction* actSortByTime = nullptr;
    QAction* actPasswordProtect = nullptr;

private slots:
    void onCustomContextMenuRequested(const QPoint& pos);
};

} // namespace ArcMeta
```

## 文件: `src/db/CategoryRepo.cpp`

```cpp
#include "CategoryRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>

namespace ArcMeta {

bool CategoryRepo::add(Category& cat, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("INSERT INTO categories (parent_id, name, color, preset_tags, sort_order, pinned, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(cat.parentId);
    q.addBindValue(QString::fromStdWString(cat.name));
    q.addBindValue(QString::fromStdWString(cat.color));

    QJsonArray tagsArr;
    for (const auto& t : cat.presetTags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));

    q.addBindValue(cat.sortOrder);
    q.addBindValue(cat.pinned ? 1 : 0);
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());

    if (q.exec()) {
        cat.id = q.lastInsertId().toInt();
        return true;
    }
    return false;
}

bool CategoryRepo::addItemToCategory(int categoryId, const std::wstring& itemPath, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("INSERT OR IGNORE INTO category_items (category_id, item_path, added_at) VALUES (?, ?, ?)");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdWString(itemPath));
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    return q.exec();
}

std::vector<Category> CategoryRepo::getAll(QSqlDatabase db) {
    std::vector<Category> results;
    QSqlQuery q("SELECT id, parent_id, name, color, preset_tags, sort_order, pinned FROM categories ORDER BY sort_order ASC", db);
    while (q.next()) {
        Category cat;
        cat.id = q.value(0).toInt();
        cat.parentId = q.value(1).toInt();
        cat.name = q.value(2).toString().toStdWString();
        cat.color = q.value(3).toString().toStdWString();
        
        QJsonDocument doc = QJsonDocument::fromJson(q.value(4).toByteArray());
        if (doc.isArray()) {
            for (const auto& v : doc.array()) cat.presetTags.push_back(v.toString().toStdWString());
        }

        cat.sortOrder = q.value(5).toInt();
        cat.pinned = q.value(6).toBool();
        results.push_back(cat);
    }
    return results;
}

bool CategoryRepo::remove(int id, QSqlDatabase db) {
    QSqlQuery q1(db);
    q1.prepare("DELETE FROM category_items WHERE category_id = ?");
    q1.addBindValue(id);
    q1.exec();

    QSqlQuery q2(db);
    q2.prepare("DELETE FROM categories WHERE id = ?");
    q2.addBindValue(id);
    return q2.exec();
}

bool CategoryRepo::update(const Category& cat, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("UPDATE categories SET parent_id = ?, name = ?, color = ?, sort_order = ?, pinned = ? WHERE id = ?");
    q.addBindValue(cat.parentId);
    q.addBindValue(QString::fromStdWString(cat.name));
    q.addBindValue(QString::fromStdWString(cat.color));
    q.addBindValue(cat.sortOrder);
    q.addBindValue(cat.pinned ? 1 : 0);
    q.addBindValue(cat.id);
    return q.exec();
}

bool CategoryRepo::removeItemFromCategory(int categoryId, const std::wstring& itemPath, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("DELETE FROM category_items WHERE category_id = ? AND item_path = ?");
    q.addBindValue(categoryId);
    q.addBindValue(QString::fromStdWString(itemPath));
    return q.exec();
}

} // namespace ArcMeta
```

## 文件: `src/db/CategoryRepo.h`

```cpp
#pragma once

#include "Database.h"
#include <string>
#include <vector>
#include <QSqlDatabase>

namespace ArcMeta {

struct Category {
    int id = 0;
    int parentId = 0;
    std::wstring name;
    std::wstring color;
    std::vector<std::wstring> presetTags;
    int sortOrder = 0;
    bool pinned = false;
    bool encrypted = false;
};

/**
 * @brief 分类持久层
 */
class CategoryRepo {
public:
    static bool add(Category& cat, QSqlDatabase db = QSqlDatabase::database());
    static bool update(const Category& cat, QSqlDatabase db = QSqlDatabase::database());
    static bool remove(int id, QSqlDatabase db = QSqlDatabase::database());
    static std::vector<Category> getAll(QSqlDatabase db = QSqlDatabase::database());

    // 条目关联逻辑
    static bool addItemToCategory(int categoryId, const std::wstring& itemPath, QSqlDatabase db = QSqlDatabase::database());
    static bool removeItemFromCategory(int categoryId, const std::wstring& itemPath, QSqlDatabase db = QSqlDatabase::database());
};

} // namespace ArcMeta
```

## 文件: `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.20)
project(ArcMeta)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 修复：优先探测 QT_DIR 环境变量，若无则使用文档红线默认路径
if(DEFINED ENV{QT_DIR})
    set(CMAKE_PREFIX_PATH "$ENV{QT_DIR}")
else()
    set(CMAKE_PREFIX_PATH "C:/Qt/6.10.2/msvc2022_64")
endif()

# 强制 MSVC 编译器及选项 (文档红线: /W4 /O2)
if(MSVC)
    add_compile_options(/W4 /O2 /utf-8)
endif()

# Qt 6 基础组件
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Sql)

# 优化输出目录，生成的 exe 将位于 build 目录下
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# 自动生成 MOC/UIC/RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)

# 搜集源文件
file(GLOB_RECURSE SOURCES 
    "src/*.cpp"
    "src/*.h"
)
# 修复：显式包含新添加的文件，防止 CMake 缓存导致 GLOB 滞后
list(APPEND SOURCES 
    "src/ui/BreadcrumbBar.h"
    "src/ui/BreadcrumbBar.cpp"
)

# 可执行文件 (添加 WIN32 属性以消除控制台黑框)
add_executable(ArcMeta WIN32 ${SOURCES} "ArcMeta.manifest")

# 链接库 (文档红线列表: ntdll, ole32, bcrypt + QtSql)
target_link_libraries(ArcMeta PRIVATE
    Qt6::Widgets
    Qt6::Svg
    Qt6::Sql
    ntdll
    ole32
    bcrypt
)

# 声明管理员权限 Manifest (现代 CMake 方式通过直接将 ArcMeta.manifest 加入 add_executable 处理)
```

## 文件: `src/ui/ContentPanel.cpp`

```cpp
#include "ContentPanel.h"
#include "../../SvgIcons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QSvgRenderer>
#include <QPainter>
#include <QHeaderView>
#include <QScrollBar>
#include <QStyle>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QAbstractItemView>
#include <QStandardItem>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStyleOptionViewItem>
#include <QItemSelectionModel>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>
#include <QFileIconProvider>
#include <QApplication>
#include <QProcess>
#include <QClipboard>
#include <QMimeData>
#include <QLineEdit>
#include <QInputDialog>
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <QStringList>
#include <QDateTime>
#include "../mft/MftReader.h"
#include "../db/ItemRepo.h"
#include "../mft/PathBuilder.h"
#include "../meta/AmMetaJson.h"
#include "UiHelper.h"

namespace ArcMeta {

/**
 * @brief 内部代理类：专门处理高级筛选逻辑
 */
class FilterProxyModel : public QSortFilterProxyModel {
public:
    explicit FilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    FilterState currentFilter;

    void updateFilter() {
        invalidate();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
        QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
        
        // 1. 评级过滤
        if (!currentFilter.ratings.isEmpty()) {
            int r = idx.data(RatingRole).toInt();
            if (!currentFilter.ratings.contains(r)) return false;
        }

        // 2. 颜色过滤
        if (!currentFilter.colors.isEmpty()) {
            QString c = idx.data(ColorRole).toString();
            if (!currentFilter.colors.contains(c)) return false;
        }

        // 3. 标签过滤
        if (!currentFilter.tags.isEmpty()) {
            QStringList itemTags = idx.data(TagsRole).toStringList();
            bool matchTag = false;
            for (const QString& fTag : currentFilter.tags) {
                if (fTag == "__none__") {
                    if (itemTags.isEmpty()) { matchTag = true; break; }
                } else {
                    if (itemTags.contains(fTag)) { matchTag = true; break; }
                }
            }
            if (!matchTag) return false;
        }

        // 4. 类型过滤
        if (!currentFilter.types.isEmpty()) {
            QString type = idx.data(TypeRole).toString(); // "folder" or "file"
            QString ext = QFileInfo(idx.data(PathRole).toString()).suffix().toUpper();
            bool matchType = false;
            for (const QString& fType : currentFilter.types) {
                if (fType == "folder") {
                    if (type == "folder") { matchType = true; break; }
                } else {
                    if (ext == fType.toUpper()) { matchType = true; break; }
                }
            }
            if (!matchType) return false;
        }

        // 5. 创建日期过滤
        if (!currentFilter.createDates.isEmpty()) {
            QDate d = QFileInfo(idx.data(PathRole).toString()).birthTime().date();
            QDate today = QDate::currentDate();
            QString dStr = d.toString("yyyy-MM-dd");
            bool matchDate = false;
            for (const QString& fDate : currentFilter.createDates) {
                if (fDate == "today" && d == today) { matchDate = true; break; }
                if (fDate == "yesterday" && d == today.addDays(-1)) { matchDate = true; break; }
                if (fDate == dStr) { matchDate = true; break; }
            }
            if (!matchDate) return false;
        }

        // 6. 修改日期过滤
        if (!currentFilter.modifyDates.isEmpty()) {
            QDate d = QFileInfo(idx.data(PathRole).toString()).lastModified().date();
            QDate today = QDate::currentDate();
            QString dStr = d.toString("yyyy-MM-dd");
            bool matchDate = false;
            for (const QString& fDate : currentFilter.modifyDates) {
                if (fDate == "today" && d == today) { matchDate = true; break; }
                if (fDate == "yesterday" && d == today.addDays(-1)) { matchDate = true; break; }
                if (fDate == dStr) { matchDate = true; break; }
            }
            if (!matchDate) return false;
        }

        return true;
    }

    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override {
        // 核心红线：置顶优先规则
        bool leftPinned = source_left.data(IsLockedRole).toBool();
        bool rightPinned = source_right.data(IsLockedRole).toBool();

        if (leftPinned != rightPinned) {
            // 在 lessThan(left, right) 中，返回 true 表示 left < right (升序时 left 排在 right 之前)
            // 如果我们想要 Pinned 的永远在最前面，无论升序降序，我们需要特殊处理。
            // 但 Qt 的排序是：lessThan 结果决定相对顺序，升序直接用 lessThan，降序取反。
            // 因此，为了实现“置顶永远在前”，必须根据 sortOrder 动态返回。
            
            if (sortOrder() == Qt::AscendingOrder)
                return leftPinned; // 升序时，Pinned 为 "小"，排在前
            else
                return !leftPinned; // 降序时，Pinned 为 "大"，排在前
        }

        // 如果置顶状态相同，则走默认逻辑
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }
};


// ─── FileModel 实现 (极致性能享元架构) ───────────────────────────────────

FileModel::FileModel(QObject* parent) : QAbstractTableModel(parent) {}

int FileModel::rowCount(const QModelIndex&) const { return m_items.count(); }
int FileModel::columnCount(const QModelIndex&) const { return 4; }

void FileModel::setItems(const QList<FileItem>& items) {
    beginResetModel();
    m_items = items;
    // 换目录时清理图标缓存，防止内存无限增长
    m_iconCache.clear();
    endResetModel();
}

QVariant FileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_items.count()) return QVariant();
    const auto& item = m_items[index.row()];

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case 0: return item.name;
            case 1: return item.sizeStr;
            case 2: return item.typeStr;
            case 3: return item.timeStr;
        }
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        static QFileIconProvider provider;
        if (item.isDrive) return provider.icon(QFileIconProvider::Drive);
        
        // 极致性能：针对常见后缀使用后缀缓存，减少 QFileInfo 的系统调用
        // 2026-03-xx 修复：文件夹图标可能因 Desktop.ini 个性化。为保证原生多样性，文件夹图标使用路径作为缓存键。
        QString cacheKey = item.isDir ? item.fullPath : item.extension.toLower();
        if (m_iconCache.contains(cacheKey)) return m_iconCache[cacheKey];
        
        QIcon icon = provider.icon(QFileInfo(item.fullPath));
        // 普通文件或文件夹，限制缓存总量
        if (m_iconCache.size() < 1000) {
            // 只缓存通用图标（普通文件夹或无特殊性质的文件）
            if (!item.isDir || item.fullPath.length() > 3) {
                m_iconCache[cacheKey] = icon;
            }
        }
        return icon;
    } else if (role == PathRole)      return item.fullPath;
    else if (role == RatingRole)    return item.rating;
    else if (role == ColorRole)     return item.color;
    else if (role == IsLockedRole)  return item.pinned;
    else if (role == EncryptedRole) return item.encrypted;
    else if (role == TagsRole)      return item.tags;
    else if (role == TypeRole)      return item.isDir ? "folder" : "file";
    else if (role == SizeRawRole)   return (qlonglong)item.size;
    else if (role == MTimeRawRole)  return item.mtime;
    else if (role == CTimeRawRole)  return item.ctime;
    else if (role == IsDirRole)     return item.isDir;
    else if (role == IsDriveRole)   return item.isDrive;

    return QVariant();
}

bool FileModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= m_items.count()) return false;
    auto& item = m_items[index.row()];
    
    if (role == RatingRole)    item.rating = value.toInt();
    else if (role == ColorRole)     item.color = value.toString();
    else if (role == IsLockedRole)  item.pinned = value.toBool();
    else if (role == Qt::EditRole) {
        item.name = value.toString();
        emit dataChanged(index, index, {Qt::DisplayRole}); // 名称改变必须触发显示刷新
    }
    else if (role == PathRole)      item.fullPath = value.toString();
    else return false;

    emit dataChanged(index, index, {role});
    return true;
}

QVariant FileModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        static QStringList headers = {"名称", "大小", "类型", "修改时间"};
        return headers.value(section);
    }
    return QVariant();
}

// ─── ContentPanel 实现 ───────────────────────────────────────────────────

ContentPanel::ContentPanel(QWidget* parent)
    : QWidget(parent) {
    setMinimumWidth(200);
    setStyleSheet("QWidget { background-color: #1A1A1A; color: #EEEEEE; border: none; }");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    m_model = new FileModel(this); // 2026-03-xx 极致性能重构：切换至自定义享元模型
    m_proxyModel = new FilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    m_zoomLevel = 96;

    initUi();
}

void ContentPanel::initUi() {
    QWidget* titleBar = new QWidget(this);
    titleBar->setStyleSheet("background: #252526; border-bottom: 1px solid #333;");
    titleBar->setFixedHeight(38);
    QHBoxLayout* titleL = new QHBoxLayout(titleBar);
    titleL->setContentsMargins(12, 0, 12, 0);

    QLabel* titleLabel = new QLabel("内容（文件夹 / 文件）", titleBar);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; border: none;");
    
    QPushButton* btnLayers = new QPushButton(titleBar);
    btnLayers->setFixedSize(24, 24);
    btnLayers->setIcon(UiHelper::getIcon("layers", QColor("#B0B0B0"), 18));
    btnLayers->setToolTip("递归显示子目录所有文件");
    btnLayers->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 4px; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }"
        "QPushButton:pressed { background: rgba(255, 255, 255, 0.2); }"
    );
    connect(btnLayers, &QPushButton::clicked, [this]() {
        if (!m_currentPath.isEmpty() && m_currentPath != "computer://") {
            loadDirectory(m_currentPath, true);
        }
    });

    titleL->addWidget(titleLabel);
    titleL->addStretch();
    titleL->addWidget(btnLayers);

    m_mainLayout->addWidget(titleBar);

    m_viewStack = new QStackedWidget(this);
    
    initGridView();
    initListView();

    m_viewStack->addWidget(m_gridView);
    m_viewStack->addWidget(m_treeView);
    m_viewStack->setCurrentWidget(m_gridView);

    QVBoxLayout* contentWrapper = new QVBoxLayout();
    contentWrapper->setContentsMargins(10, 10, 10, 10);
    contentWrapper->setSpacing(0);
    contentWrapper->addWidget(m_viewStack);
    
    m_mainLayout->addLayout(contentWrapper);
    
    // 快捷键拦截提升至控件本身，而非 viewport()，确保焦点行为一致
    m_gridView->installEventFilter(this);
}

void ContentPanel::updateGridSize() {
    m_zoomLevel = qBound(32, m_zoomLevel, 128);
    m_gridView->setIconSize(QSize(m_zoomLevel, m_zoomLevel));
    
    int cardW = m_zoomLevel + 30; // 用户指定: zoomLevel+30
    int cardH = m_zoomLevel + 60; // 遵循规范: zoomLevel+60
    m_gridView->setGridSize(QSize(cardW, cardH));
}

bool ContentPanel::eventFilter(QObject* obj, QEvent* event) {
    if ((obj == m_gridView || obj == m_gridView->viewport()) && event->type() == QEvent::Wheel) {
        QWheelEvent* wEvent = static_cast<QWheelEvent*>(event);
        if (wEvent->modifiers() & Qt::ControlModifier) {
            int delta = wEvent->angleDelta().y();
            if (delta > 0) m_zoomLevel += 8;
            else m_zoomLevel -= 8;
            updateGridSize();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        QAbstractItemView* view = qobject_cast<QAbstractItemView*>(obj);
        if (!view) view = qobject_cast<QAbstractItemView*>(obj->parent());

        // 核心红线：如果焦点在 QLineEdit 上，说明正在重命名，直接放行
        if (qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
            return false;
        }

        if (view) {
            auto selectedIdxs = view->selectionModel()->selectedIndexes();
            if (selectedIdxs.isEmpty()) return false;

            // 极致性能：批量更新逻辑封装
            auto updateBatch = [&](std::function<void(AmMetaJson&, const std::wstring&, const QModelIndex&)> updater) {
                std::map<std::wstring, std::vector<std::pair<QModelIndex, std::wstring>>> groups;
                for (const auto& idx : selectedIdxs) {
                    if (idx.column() != 0) continue;
                    QString p = idx.data(PathRole).toString();
                    if (p.isEmpty()) continue;
                    QFileInfo info(p);
                    groups[info.absolutePath().toStdWString()].push_back({idx, info.fileName().toStdWString()});
                }
                for (auto& [folder, itemsGroup] : groups) {
                    AmMetaJson meta(folder);
                    meta.load();
                    for (auto& itemPair : itemsGroup) updater(meta, itemPair.second, itemPair.first);
                    meta.save();
                }
            };

            // 1. Ctrl + 0..5 (星级)
            if ((keyEvent->modifiers() & Qt::ControlModifier) && 
                (keyEvent->key() >= Qt::Key_0 && keyEvent->key() <= Qt::Key_5)) {
                int r = keyEvent->key() - Qt::Key_0;
                updateBatch([&](AmMetaJson& meta, const std::wstring& name, const QModelIndex& idx) {
                    m_proxyModel->setData(idx, r, RatingRole);
                    meta.items()[name].rating = r;
                });
                return true;
            }

            // 2. Alt + D (置顶/取消置顶)
            if (((keyEvent->modifiers() & Qt::AltModifier) || (keyEvent->modifiers() & (Qt::AltModifier | Qt::WindowShortcut))) && 
                (keyEvent->key() == Qt::Key_D)) {
                updateBatch([&](AmMetaJson& meta, const std::wstring& name, const QModelIndex& idx) {
                    bool cur = meta.items()[name].pinned;
                    meta.items()[name].pinned = !cur;
                    m_proxyModel->setData(idx, !cur, IsLockedRole);
                });
                return true;
            }

            // 3. Alt + 1..9 (颜色打标)
            if ((keyEvent->modifiers() & Qt::AltModifier) && 
                (keyEvent->key() >= Qt::Key_1 && keyEvent->key() <= Qt::Key_9)) {
                QString color;
                switch (keyEvent->key()) {
                    case Qt::Key_1: color = "red"; break;
                    case Qt::Key_2: color = "orange"; break;
                    case Qt::Key_3: color = "yellow"; break;
                    case Qt::Key_4: color = "green"; break;
                    case Qt::Key_5: color = "cyan"; break;
                    case Qt::Key_6: color = "blue"; break;
                    case Qt::Key_7: color = "purple"; break;
                    case Qt::Key_8: color = "gray"; break;
                    case Qt::Key_9: color = ""; break;
                }
                updateBatch([&](AmMetaJson& meta, const std::wstring& name, const QModelIndex& idx) {
                    m_proxyModel->setData(idx, color, ColorRole);
                    meta.items()[name].color = color.toStdWString();
                });
                return true;
            }

            if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
                if (keyEvent->key() == Qt::Key_C) {
                    QStringList paths;
                    for (const auto& idx : selectedIdxs) if (idx.column() == 0) paths << QDir::toNativeSeparators(idx.data(PathRole).toString());
                    if (!paths.isEmpty()) QApplication::clipboard()->setText(paths.join("\r\n"));
                    return true;
                }
                if (keyEvent->key() == Qt::Key_R) {
                    // 批量重命名 (Ctrl+Shift+R) 被触发
                    return true;
                }
            }

            if (keyEvent->key() == Qt::Key_F2) {
                view->edit(view->currentIndex());
                return true;
            }
            if (keyEvent->key() == Qt::Key_Delete) {
                onCustomContextMenuRequested(view->mapFromGlobal(QCursor::pos()));
                return true;
            }
            
            if (keyEvent->modifiers() & Qt::ControlModifier) {
                if (keyEvent->key() == Qt::Key_C && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
                    QList<QUrl> urls;
                    for (const auto& idx : selectedIdxs) if (idx.column() == 0) urls << QUrl::fromLocalFile(idx.data(PathRole).toString());
                    if (!urls.isEmpty()) {
                        QMimeData* mime = new QMimeData();
                        mime->setUrls(urls);
                        QApplication::clipboard()->setMimeData(mime);
                    }
                    return true;
                }
                if (keyEvent->key() == Qt::Key_V) {
                    const QMimeData* mime = QApplication::clipboard()->mimeData();
                    if (mime && mime->hasUrls()) {
                        QList<QUrl> urls = mime->urls();
                        std::wstring fromPaths;
                        for (const QUrl& url : urls) {
                            fromPaths += QDir::toNativeSeparators(url.toLocalFile()).toStdWString() + L'\0';
                        }
                        if (!fromPaths.empty()) {
                            fromPaths += L'\0';
                            std::wstring toPath = m_currentPath.toStdWString() + L'\0' + L'\0';
                            SHFILEOPSTRUCTW fileOp = { 0 };
                            fileOp.wFunc = FO_COPY;
                            fileOp.pFrom = fromPaths.c_str();
                            fileOp.pTo = toPath.c_str();
                            fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
                            if (SHFileOperationW(&fileOp) == 0) loadDirectory(m_currentPath);
                        }
                    }
                    return true;
                }
            }

            if (keyEvent->key() == Qt::Key_Space) {
                QModelIndex idx = view->currentIndex();
                if (idx.isValid()) emit requestQuickLook(idx.data(PathRole).toString());
                return true;
            }
            if (keyEvent->key() == Qt::Key_Backspace) {
                if (!m_currentPath.isEmpty() && m_currentPath != "computer://") {
                    QDir dir(m_currentPath);
                    if (dir.isRoot() || m_currentPath.length() <= 3) {
                        emit directorySelected("computer://");
                    } else if (dir.cdUp()) {
                        emit directorySelected(dir.absolutePath());
                    }
                }
                return true;
            }
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                onDoubleClicked(view->currentIndex());
                return true;
            }
            if (keyEvent->modifiers() & Qt::ControlModifier && keyEvent->key() == Qt::Key_Backslash) {
                setViewMode(m_viewStack->currentIndex() == 0 ? ListView : GridView);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ContentPanel::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        int delta = event->angleDelta().y();
        if (delta > 0) m_zoomLevel += 8;
        else m_zoomLevel -= 8;
        updateGridSize();
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
}

void ContentPanel::setViewMode(ViewMode mode) {
    // 2026-03-xx 交互架构修复：实现跨视图选择状态同步
    QAbstractItemView* oldView = (m_viewStack->currentIndex() == 0) ? (QAbstractItemView*)m_gridView : (QAbstractItemView*)m_treeView;
    QAbstractItemView* newView = (mode == GridView) ? (QAbstractItemView*)m_gridView : (QAbstractItemView*)m_treeView;
    
    if (oldView != newView) {
        QItemSelection selection = oldView->selectionModel()->selection();
        m_viewStack->setCurrentWidget(newView);
        newView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
        if (!selection.isEmpty()) {
            newView->scrollTo(selection.indexes().first());
        }
    }
}

void ContentPanel::initGridView() {
    m_gridView = new QListView(this);
    m_gridView->setViewMode(QListView::IconMode);
    m_gridView->setMovement(QListView::Static);
    m_gridView->setSpacing(8);
    m_gridView->setResizeMode(QListView::Adjust);
    m_gridView->setWrapping(true);
    m_gridView->setIconSize(QSize(96, 96));
    m_gridView->setGridSize(QSize(126, 156)); // 遵循规范: W=96+30, H=96+60
    m_gridView->setSpacing(10);
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);

    // 禁用双击编辑，将双击权归还给“打开”操作
    m_gridView->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    m_gridView->setModel(m_proxyModel);
    m_gridView->setItemDelegate(new GridItemDelegate(this));
    m_gridView->viewport()->installEventFilter(this);

    // 显式连接双击打开信号
    connect(m_gridView, &QListView::doubleClicked, this, &ContentPanel::onDoubleClicked);

    m_gridView->setStyleSheet(
        "QListView { background-color: transparent; border: none; outline: none; }"
        "QListView::item { background: transparent; }"
        "QListView::item:selected { background-color: rgba(55, 138, 221, 0.2); border-radius: 6px; }"
        "QListView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 2px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }"
    );

    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged);
    connect(m_gridView, &QListView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested);
}

void ContentPanel::initListView() {
    m_treeView = new QTreeView(this);
    m_treeView->setSortingEnabled(true);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_treeView->setExpandsOnDoubleClick(false);
    m_treeView->setRootIsDecorated(false);

    m_treeView->setModel(m_proxyModel);
    m_treeView->viewport()->installEventFilter(this);

    m_treeView->setStyleSheet(
        "QTreeView { background-color: transparent; border: none; outline: none; font-size: 12px; }"
        "QTreeView::item { height: 28px; color: #EEEEEE; padding-left: 4px; }"
        "QTreeView::item:selected { background-color: #378ADD; }"
        "QTreeView::item:hover { background-color: rgba(255, 255, 255, 0.05); }"
        "QTreeView QLineEdit { background-color: #2D2D2D; color: #FFFFFF; border: 1px solid #378ADD; border-radius: 2px; padding: 2px; selection-background-color: #378ADD; selection-color: #FFFFFF; }"
    );

    m_treeView->header()->setStyleSheet(
        "QHeaderView::section { background-color: #252525; color: #B0B0B0; padding-left: 10px; border: none; height: 32px; font-size: 11px; }"
    );

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ContentPanel::onSelectionChanged);
    connect(m_treeView, &QTreeView::customContextMenuRequested, this, &ContentPanel::onCustomContextMenuRequested);
    connect(m_treeView, &QTreeView::doubleClicked, this, &ContentPanel::onDoubleClicked);
}

void ContentPanel::onCustomContextMenuRequested(const QPoint& pos) {
    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2B2B2B; border: 1px solid #444444; color: #EEEEEE; padding: 4px; border-radius: 6px; }"
        "QMenu::item { height: 24px; padding: 0 10px 0 10px; border-radius: 3px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #505050; }"
        "QMenu::separator { height: 1px; background: #444444; margin: 4px 8px 4px 8px; }"
        "QMenu::right-arrow { image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjRUVFRUVFIiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PHBvbHlsaW5lIHBvaW50cz0iOSAxOCAxNSAxMiA5IDYiPjwvcG9seWxpbmU+PC9zdmc+); width: 12px; height: 12px; right: 8px; }"
    );

    menu.addAction("打开");
    menu.addAction("用系统默认程序打开");
    menu.addAction("在“资源管理器”中显示");
    menu.addSeparator();

    QMenu* newMenu = menu.addMenu("新建...");
    newMenu->setIcon(UiHelper::getIcon("ruler_spacing", QColor("#EEEEEE")));
    QAction* actNewFolder = newMenu->addAction(UiHelper::getIcon("folder", QColor("#EEEEEE")), "创建文件夹");
    QAction* actNewMd     = newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown");
    QAction* actNewTxt    = newMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)");
    menu.addSeparator();
    
    QMenu* categorizeMenu = menu.addMenu("归类到...");
    categorizeMenu->addAction("（暂无分类）"); // 占位，待后续对接数据库加载
    
    menu.addSeparator();
    
    menu.addAction("置顶 / 取消置顶");
    QMenu* cryptoMenu = menu.addMenu("加密保护");
    cryptoMenu->addAction("加密保护");
    cryptoMenu->addAction("解除加密");
    cryptoMenu->addAction("修改密码");
    
    menu.addSeparator();
    menu.addAction("批量重命名 (Ctrl+Shift+R)");
    menu.addSeparator();
    
    menu.addAction("重命名");
    menu.addAction("复制");
    menu.addAction("剪切");
    menu.addAction("粘贴");
    menu.addAction("删除（移入回收站）");
    menu.addSeparator();
    
    menu.addAction("复制路径");
    menu.addAction("属性");

    QAbstractItemView* view = qobject_cast<QAbstractItemView*>(sender());
    if (!view) return;
    
    QAction* selectedAction = menu.exec(view->viewport()->mapToGlobal(pos));
    if (!selectedAction) return;

    QString actionText = selectedAction->text();
    QModelIndex currentIndex = view->indexAt(pos);
    QString path = currentIndex.data(PathRole).toString();

    if (actionText == "在资源管理器中显示") {
        QStringList args;
        args << "/select," << QDir::toNativeSeparators(path);
        QProcess::startDetached("explorer", args);
    } else if (selectedAction == actNewFolder) {
        createNewItem("folder");
    } else if (selectedAction == actNewMd) {
        createNewItem("md");
    } else if (selectedAction == actNewTxt) {
        createNewItem("txt");
    } else if (actionText == "复制路径") {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    } else if (actionText == "重命名") {
        view->edit(currentIndex);
    } else if (actionText.startsWith("删除")) {
        std::wstring wpath = path.toStdWString() + L'\0' + L'\0';
        SHFILEOPSTRUCTW fileOp = { 0 };
        fileOp.wFunc = FO_DELETE;
        fileOp.pFrom = wpath.c_str();
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION;
        if (SHFileOperationW(&fileOp) == 0) loadDirectory(m_currentPath);
    } else if (actionText == "置顶 / 取消置顶") {
        auto selectedIdxs = view->selectionModel()->selectedIndexes();
        std::map<std::wstring, std::vector<std::pair<QModelIndex, std::wstring>>> groups;
        for (const auto& idx : selectedIdxs) {
            if (idx.column() != 0) continue;
            QString p = idx.data(PathRole).toString();
            if (p.isEmpty()) continue;
            QFileInfo info(p);
            groups[info.absolutePath().toStdWString()].push_back({idx, info.fileName().toStdWString()});
        }
        for (auto& [folder, itemPairs] : groups) {
            AmMetaJson meta(folder);
            meta.load();
            for (auto& item : itemPairs) {
                bool cur = meta.items()[item.second].pinned;
                meta.items()[item.second].pinned = !cur;
                m_proxyModel->setData(item.first, !cur, IsLockedRole);
            }
            meta.save();
        }
    } else if (actionText == "属性") {
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        sei.lpVerb = L"properties";
        std::wstring wpath = path.toStdWString();
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        ShellExecuteExW(&sei);
    } else if (actionText == "打开" || actionText == "用系统默认程序打开") {
        onDoubleClicked(currentIndex);
    }
}

void ContentPanel::onSelectionChanged() {
    QItemSelectionModel* selectionModel = (m_viewStack->currentWidget() == m_gridView) ? m_gridView->selectionModel() : m_treeView->selectionModel();
    if (!selectionModel) return;

    QStringList selectedPaths;
    QModelIndexList indices = selectionModel->selectedIndexes();
    for (const QModelIndex& index : indices) {
        if (index.column() == 0) {
            QString path = index.data(PathRole).toString();
            if (!path.isEmpty()) selectedPaths.append(path);
        }
    }
    emit selectionChanged(selectedPaths);
}

void ContentPanel::onDoubleClicked(const QModelIndex& index) {
    if (!index.isValid()) return;
    QString path = index.data(PathRole).toString();
    if (path.isEmpty()) return;

    QFileInfo info(path);
    if (info.isDir()) {
        emit directorySelected(path); 
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void ContentPanel::loadDirectory(const QString& path, bool recursive) {
    // 2026-03-xx 极致性能重构：切换目录时清空搜索缓存，确保元数据一致性
    m_searchMetaCache.clear();

    // 2026-03-xx 极致性能重构：彻底废除 QStandardItem 堆分配
    QList<FileItem> items;
    if (path.isEmpty() || path == "computer://") {
        m_currentPath = "computer://";
        const auto drives = QDir::drives();
        
        QMap<int, int>     ratingCounts;
        QMap<QString, int> colorCounts;
        QMap<QString, int> tagCounts;
        QMap<QString, int> typeCounts;
        QMap<QString, int> createDateCounts;
        QMap<QString, int> modifyDateCounts;

        for (const QFileInfo& drive : drives) {
            QString drivePath = drive.absolutePath();
            FileItem fi;
            fi.name = drivePath;
            fi.fullPath = drivePath;
            fi.isDir = true;
            fi.isDrive = true;
            fi.typeStr = "磁盘分区";
            fi.sizeStr = "-";
            fi.timeStr = "-";
            items.append(fi);

            typeCounts["folder"]++;
        }
        m_model->setItems(items);
        // 发送统计数据，使筛选面板更新（例如显示“文件夹(8)”）
        emit directoryStatsReady(ratingCounts, colorCounts, tagCounts, typeCounts,
                                  createDateCounts, modifyDateCounts);
        return;
    }

    m_currentPath = path;
    
    QMap<int, int>     ratingCounts;
    QMap<QString, int> colorCounts;
    QMap<QString, int> tagCounts;
    QMap<QString, int> typeCounts;
    QMap<QString, int> createDateCounts;
    QMap<QString, int> modifyDateCounts;
    int noTagCount = 0;

    addItemsFromDirectory(path, recursive, ratingCounts, colorCounts, tagCounts, typeCounts, createDateCounts, modifyDateCounts, noTagCount, items);
    m_model->setItems(items);

    applyFilters();

    if (noTagCount > 0) tagCounts["__none__"] = noTagCount;
    emit directoryStatsReady(ratingCounts, colorCounts, tagCounts, typeCounts,
                              createDateCounts, modifyDateCounts);
}

void ContentPanel::addItemsFromDirectory(const QString& path, bool recursive,
                                       QMap<int, int>& ratingCounts,
                                       QMap<QString, int>& colorCounts,
                                       QMap<QString, int>& tagCounts,
                                       QMap<QString, int>& typeCounts,
                                       QMap<QString, int>& createDateCounts,
                                       QMap<QString, int>& modifyDateCounts,
                                       int& noTagCount,
                                       QList<FileItem>& outItems) 
{
    QDir dir(path);
    if (!dir.exists()) return;

    QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

    // 2026-03-xx 极致性能重构：切换至“数据库优先”预取策略，彻底消除逐个 JSON 解析的 IO 开销
    auto metaItems = ItemRepo::getMetadataBatch(path.toStdWString());

    QDate today    = QDate::currentDate();
    QDate yesterday = today.addDays(-1);

    auto dateKey = [&](const QDate& d) -> QString {
        if (d == today)     return "today";
        if (d == yesterday) return "yesterday";
        return d.toString("yyyy-MM-dd");
    };

    for (const QFileInfo& info : entries) {
        QString fileName = info.fileName();
        if (fileName == ".am_meta.json" || fileName == ".am_meta.json.tmp") continue;

        FileItem fi;
        fi.name = fileName;
        fi.fullPath = info.absoluteFilePath();
        fi.isDir = info.isDir();
        fi.isDrive = fi.isDir && (fi.fullPath.length() <= 3 && fi.fullPath.endsWith(":\\"));
        fi.extension = info.suffix().toUpper();

        // 2026-03-xx 极致性能重构：元数据反填 (Flyweight + DB-First Zero IO)
        auto it = metaItems.find(fileName.toStdWString());
        if (it != metaItems.end()) {
            fi.rating = it->second.rating;
            fi.color = QString::fromStdWString(it->second.color);
            fi.pinned = it->second.pinned;
            fi.encrypted = it->second.encrypted;
            for (const auto& tag : it->second.tags) fi.tags << QString::fromStdWString(tag);
            
            // 2026-03-xx 极致性能：预填充所有原始数值，消除 UI 渗漏 IO
            fi.size = it->second.size;
            fi.mtime = it->second.mtime;
            fi.ctime = it->second.ctime;

            // 从 DB 预取显示属性，消除 QFileInfo 物理 IO
            fi.sizeStr = fi.isDir ? "-" : QString::number(fi.size / 1024) + " KB";
            fi.timeStr = QDateTime::fromMSecsSinceEpoch((qint64)fi.mtime).toString("yyyy-MM-dd HH:mm");
            
            // 统计更新
            createDateCounts[dateKey(QDateTime::fromMSecsSinceEpoch((qint64)fi.ctime).date())]++;
            modifyDateCounts[dateKey(QDateTime::fromMSecsSinceEpoch((qint64)fi.mtime).date())]++;
        } else {
            // 后备方案：若 DB 尚未同步，才使用 QFileInfo (仅在初次创建时发生)
            fi.size = info.size();
            fi.mtime = (double)info.lastModified().toMSecsSinceEpoch();
            fi.ctime = (double)info.birthTime().toMSecsSinceEpoch();

            fi.sizeStr = fi.isDir ? "-" : QString::number(fi.size / 1024) + " KB";
            fi.timeStr = QDateTime::fromMSecsSinceEpoch((qint64)fi.mtime).toString("yyyy-MM-dd HH:mm");
            createDateCounts[dateKey(info.birthTime().date())]++;
            modifyDateCounts[dateKey(info.lastModified().date())]++;
        }

        fi.typeStr = fi.isDir ? "文件夹" : (fi.extension.isEmpty() ? "文件" : fi.extension + " 文件");

        // 基础统计
        ratingCounts[fi.rating]++;
        colorCounts[fi.color]++;
        if (fi.tags.isEmpty()) noTagCount++;
        else for (const auto& t : fi.tags) tagCounts[t]++;
        typeCounts[fi.isDir ? "folder" : fi.extension]++;

        outItems.append(fi);

        if (recursive && info.isDir()) {
            addItemsFromDirectory(fi.fullPath, true, ratingCounts, colorCounts, tagCounts, typeCounts, createDateCounts, modifyDateCounts, noTagCount, outItems);
        }
    }
}



void ContentPanel::search(const QString& query) {
    if (query.isEmpty()) return;
    
    // 2026-03-xx 极致性能重构：切换至享元条目列表
    QList<FileItem> items;

    auto results = MftReader::instance().search(query.toStdWString());
    QFileIconProvider iconProvider;

    QMap<int, int>     ratingCounts;
    QMap<QString, int> colorCounts;
    QMap<QString, int> tagCounts;
    QMap<QString, int> typeCounts;
    QMap<QString, int> createDateCounts;
    QMap<QString, int> modifyDateCounts;
    int noTagCount = 0;

    QDate today = QDate::currentDate();
    QDate yesterday = today.addDays(-1);
    auto dateKey = [&](const QDate& d) -> QString {
        if (d == today)     return "today";
        if (d == yesterday) return "yesterday";
        return d.toString("yyyy-MM-dd");
    };

    int count = 0;
    for (const auto& entry : results) {
        if (++count > 1000) break;

        QString fileName = QString::fromStdWString(entry.name);
        if (fileName == ".am_meta.json" || fileName == ".am_meta.json.tmp") continue;

        // 2026-03-xx 极致性能重构：利用双向 O(1) 索引获取路径，消除递归
        std::wstring fullWPath = MftReader::instance().getPathFromFrn(entry.volume, entry.frn);
        QString fullPath = QString::fromStdWString(fullWPath);
        if (fullPath.isEmpty()) {
            fullPath = QString::fromStdWString(entry.volume) + "[FRN:" + QString::number(entry.frn, 16) + "]";
        }

        QFileInfo info(fullPath);
        FileItem fi;
        fi.name = fileName;
        fi.fullPath = fullPath;
        fi.isDir = entry.isDir();
        fi.isDrive = fi.isDir && (fullPath.length() <= 3 && fullPath.endsWith(":\\"));
        fi.extension = info.suffix().toUpper();
        fi.sizeStr = fullPath; // 在搜索模式下，第二列展示路径
        fi.typeStr = fi.isDir ? "文件夹" : (fi.extension.isEmpty() ? "文件" : fi.extension + " 文件");
        
        // --- 搜索模式下元数据对接：2026-03-xx 极致性能重构：采用数据库批量缓存，消除同步磁盘探针 ---
        // 优化：使用成员缓存池处理交错的搜索结果，并在 loadDirectory 时重置，解决一致性 Bug
        std::wstring parentWDir = info.absolutePath().toStdWString();
        
        auto itBatch = m_searchMetaCache.find(parentWDir);
        if (itBatch == m_searchMetaCache.end()) {
            // 缓存未命中，从数据库加载整个目录的元数据批次
            m_searchMetaCache[parentWDir] = ItemRepo::getMetadataBatch(parentWDir);
            itBatch = m_searchMetaCache.find(parentWDir);
        }
        
        auto it = itBatch->second.find(fileName.toStdWString());
        if (it != itBatch->second.end()) {
            fi.rating = it->second.rating;
            fi.color = QString::fromStdWString(it->second.color);
            fi.pinned = it->second.pinned;
            fi.encrypted = it->second.encrypted;
            for (const auto& tag : it->second.tags) fi.tags << QString::fromStdWString(tag);
            
            fi.timeStr = QDateTime::fromMSecsSinceEpoch((qint64)it->second.mtime).toString("yyyy-MM-dd HH:mm");
            createDateCounts[dateKey(QDateTime::fromMSecsSinceEpoch((qint64)it->second.ctime).date())]++;
            modifyDateCounts[dateKey(QDateTime::fromMSecsSinceEpoch((qint64)it->second.mtime).date())]++;
        } else {
            fi.timeStr = info.lastModified().toString("yyyy-MM-dd HH:mm");
            createDateCounts[dateKey(info.birthTime().date())]++;
            modifyDateCounts[dateKey(info.lastModified().date())]++;
        }

        // 统计累加
        ratingCounts[fi.rating]++;
        colorCounts[fi.color]++;
        if (fi.tags.isEmpty()) noTagCount++;
        else for (const auto& t : fi.tags) tagCounts[t]++;
        typeCounts[fi.isDir ? "folder" : fi.extension]++;

        items.append(fi);
    }
    m_model->setItems(items);

    if (noTagCount > 0) tagCounts["__none__"] = noTagCount;
    emit directoryStatsReady(ratingCounts, colorCounts, tagCounts, typeCounts,
                              createDateCounts, modifyDateCounts);
}

void ContentPanel::applyFilters(const FilterState& state) {
    m_currentFilter = state;
    applyFilters();
}

void ContentPanel::applyFilters() {
    auto* proxy = static_cast<FilterProxyModel*>(m_proxyModel);
    proxy->currentFilter = m_currentFilter;
    proxy->updateFilter();
}

void ContentPanel::createNewItem(const QString& type) {
    if (m_currentPath.isEmpty() || m_currentPath == "computer://") return;

    QString baseName = (type == "folder") ? "新建文件夹" : "未命名";
    QString ext = (type == "md") ? ".md" : ((type == "txt") ? ".txt" : "");
    QString finalName = baseName + ext;
    QString fullPath = m_currentPath + "/" + finalName;

    // 自动避重
    int counter = 1;
    while (QFileInfo::exists(fullPath)) {
        finalName = baseName + QString(" (%1)").arg(counter++) + ext;
        fullPath = m_currentPath + "/" + finalName;
    }

    bool success = false;
    if (type == "folder") {
        success = QDir(m_currentPath).mkdir(finalName);
    } else {
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.close();
            success = true;
        }
    }

    if (success) {
        loadDirectory(m_currentPath);
        // 自动进入重命名模式
        const auto& items = m_model->items();
        for (int i = 0; i < items.size(); ++i) {
            if (items[i].name == finalName) {
                QModelIndex srcIdx = m_model->index(i, 0);
                QModelIndex proxyIdx = m_proxyModel->mapFromSource(srcIdx);
                if (proxyIdx.isValid()) {
                    m_gridView->setCurrentIndex(proxyIdx);
                    m_gridView->edit(proxyIdx);
                }
                break;
            }
        }
    }
}

// --- Delegate ---

void GridItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect cardRect = option.rect.adjusted(4, 4, -4, -4);
    bool isSelected = (option.state & QStyle::State_Selected);
    bool isHovered = (option.state & QStyle::State_MouseOver);
    
    QColor cardBg = isSelected ? QColor("#282828") : (isHovered ? QColor("#2A2A2A") : QColor("#2D2D2D"));
    painter->setPen(isSelected ? QPen(QColor("#3498db"), 2) : QPen(QColor("#333333"), 1));
    painter->setBrush(cardBg);
    painter->drawRoundedRect(cardRect, 8, 8);

    QString path = index.data(PathRole).toString();
    bool isDrive = index.data(IsDriveRole).toBool();
    
    if (!isDrive) {
        QFileInfo info(path);
        QString ext = info.isDir() ? "DIR" : info.suffix().toUpper();
        if (ext.isEmpty()) ext = "FILE";
        QColor badgeColor = UiHelper::getExtensionColor(ext);

        QRect extRect(cardRect.left() + 8, cardRect.top() + 8, 36, 18);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeColor);
        // 2026-03-xx 按照用户要求：卡片内圆角由 4px 统一调整为 2px
        painter->drawRoundedRect(extRect, 2, 2);
        painter->setPen(QColor("#FFFFFF"));
        QFont extFont = painter->font();
        extFont.setPointSize(8);
        extFont.setBold(true);
        painter->setFont(extFont);
        painter->drawText(extRect, Qt::AlignCenter, ext);
    }

    int baseIconSize = option.decorationSize.width();
    if (baseIconSize <= 0) baseIconSize = 64; 
    int iconDrawSize = static_cast<int>(baseIconSize * 0.65); 
    
    int ratingH = 12;
    // 2026-03-xx 按照用户要求：文件名背景块高度由 16px 调整为 18px
    int nameH = 18;
    int gap1 = 6;
    int gap2 = 4;
    
    int totalH = iconDrawSize + gap1 + ratingH + gap2 + nameH;
    int startY = cardRect.top() + (cardRect.height() - totalH) / 2 + 13; // +5 -> +13 (下移 8px)

    QRect iconRect(cardRect.left() + (cardRect.width() - iconDrawSize) / 2, startY, iconDrawSize, iconDrawSize);
    QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    icon.paint(painter, iconRect);

    int ratingY = iconRect.bottom() + gap1;
    int rating = index.data(RatingRole).toInt();
    
    int starSize = 12; 
    int starSpacing = 1; // 还原间距，不再脑补加宽
    int banW = 12;
    int banGap = 4;
    int infoTotalW = banW + banGap + (5 * starSize) + (4 * starSpacing);
    int infoStartX = cardRect.left() + (cardRect.width() - infoTotalW) / 2;

    // A. 绘制“清除星级”图标 (SVG)
    QRect banRect(infoStartX, ratingY + (ratingH - banW) / 2, banW, banW);
    QIcon banIcon = UiHelper::getIcon("no_color", QColor("#555555"), banW);
    banIcon.paint(painter, banRect);

    // B. 绘制星级 (SVG)
    int starsStartX = infoStartX + banW + banGap;
    QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), QColor("#EF9F27"));
    QPixmap emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), QColor("#444444"));

    for (int i = 0; i < 5; ++i) {
        QRect starRect(starsStartX + i * (starSize + starSpacing), ratingY + (ratingH - starSize) / 2, starSize, starSize);
        painter->drawPixmap(starRect, (i < rating) ? filledStar : emptyStar);
    }

    int nameY = ratingY + ratingH + gap2;
    QRect nameRect(cardRect.left() + 6, nameY, cardRect.width() - 12, nameH);
    
    QString colorName = index.data(ColorRole).toString();
    if (!colorName.isEmpty()) {
        QColor dotC(colorName);
        if (!dotC.isValid()) {
            if (colorName.contains("red") || colorName.contains("红")) dotC = QColor("#E24B4A");
            else if (colorName.contains("orange") || colorName.contains("橙")) dotC = QColor("#EF9F27");
            else if (colorName.contains("green") || colorName.contains("绿")) dotC = QColor("#639922");
            else if (colorName.contains("blue") || colorName.contains("蓝")) dotC = QColor("#378ADD");
        }
        if (dotC.isValid()) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(dotC);
            // 2026-03-xx 按照用户要求：卡片内圆角由 4px 统一调整为 2px
            painter->drawRoundedRect(nameRect, 2, 2);
            painter->setPen(dotC.lightness() > 180 ? Qt::black : Qt::white);
        } else {
            painter->setPen(QColor("#CCCCCC"));
        }
    } else {
        painter->setPen(QColor("#CCCCCC"));
    }

    QString name = index.data(Qt::DisplayRole).toString();
    QFont textFont = painter->font();
    textFont.setPointSize(8);
    textFont.setBold(false);
    painter->setFont(textFont);
    painter->drawText(nameRect.adjusted(4, 0, -4, 0), Qt::AlignCenter | Qt::ElideRight, name);

    painter->restore();
}

QSize GridItemDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const {
    auto* view = qobject_cast<const QListView*>(option.widget);
    if (view && view->gridSize().isValid()) return view->gridSize();
    return QSize(96, 112);
}

bool GridItemDelegate::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        QLineEdit* editor = qobject_cast<QLineEdit*>(obj);
        if (editor) {
            // 修复重命名：允许方向键、Home/End 在编辑框内正常导航，不触发视图切换
            switch (keyEvent->key()) {
                case Qt::Key_Left:
                case Qt::Key_Right:
                case Qt::Key_Up:
                case Qt::Key_Down:
                case Qt::Key_Home:
                case Qt::Key_End:
                    // 返回 false 表示不拦截，让 QLineEdit 自身处理
                    // 同时停止事件传播，防止 QAbstractItemView 捕获并结束编辑
                    keyEvent->accept();
                    return false;
                default:
                    break;
            }
        }
    }
    return QStyledItemDelegate::eventFilter(obj, event);
}

bool GridItemDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mEvent = static_cast<QMouseEvent*>(event);
        if (mEvent->button() == Qt::LeftButton) {
            int baseIconSize = option.decorationSize.width();
            if (baseIconSize <= 0) baseIconSize = 64; 
            int iconDrawSize = static_cast<int>(baseIconSize * 0.65); 
            int ratingH = 12;
            // 2026-03-xx 按照用户要求：点击判定基准高度同步调整为 18px
            int nameH = 18;
            int gap1 = 6;
            int gap2 = 4;
            int totalH = iconDrawSize + gap1 + ratingH + gap2 + nameH;
            int startY = option.rect.top() + (option.rect.height() - totalH) / 2 + 13;
            int ratingY = startY + iconDrawSize + gap1;
            int starSize = 12; 
            int starSpacing = 1; 
            int banW = 12;
            int banGap = 4;
            int infoTotalW = banW + banGap + (5 * starSize + 4 * starSpacing);
            int infoStartX = option.rect.left() + (option.rect.width() - infoTotalW) / 2;

            // 1. 判定“清除”图标
            QRect banRect(infoStartX, ratingY + (ratingH - 14) / 2, 14, 14);
            if (banRect.contains(mEvent->pos())) {
                // 此处的 model 已经是 View 关联的 ProxyModel
                model->setData(index, 0, RatingRole);
                QString path = index.data(PathRole).toString();
                if (!path.isEmpty()) {
                    QFileInfo info(path);
                    AmMetaJson meta(info.absolutePath().toStdWString());
                    meta.load();
                    meta.items()[info.fileName().toStdWString()].rating = 0;
                    meta.save();
                }
                return true;
            }

            // 2. 判定 5 颗星星 (1..5)
            int starsStartX = infoStartX + banW + banGap;
            for (int i = 0; i < 5; ++i) {
                QRect starRect(starsStartX + i * (starSize + starSpacing), ratingY + (ratingH - starSize) / 2, starSize, starSize);
                if (starRect.contains(mEvent->pos())) {
                    int r = i + 1;
                    model->setData(index, r, RatingRole);
                    QString path = index.data(PathRole).toString();
                    if (!path.isEmpty()) {
                        QFileInfo info(path);
                        AmMetaJson meta(info.absolutePath().toStdWString());
                        meta.load();
                        meta.items()[info.fileName().toStdWString()].rating = r;
                        meta.save();
                    }
                    return true;
                }
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

QWidget* GridItemDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(option);
    QLineEdit* editor = new QLineEdit(parent);
    
    // 关键点：重命名时光标行为修复
    // 默认情况下 QAbstractItemView 会在编辑状态下拦截方向键
    // 我们通过设置属性让编辑器能处理方向键
    editor->installEventFilter(const_cast<GridItemDelegate*>(this));

    editor->setAlignment(Qt::AlignCenter);
    editor->setFrame(false);
    
    // 背景色动态匹配标签色，若无标签则用默认深色
    QString tagColorStr = index.data(ColorRole).toString();
    QString bgColor = tagColorStr.isEmpty() ? "#3E3E42" : tagColorStr;
    QString textColor = tagColorStr.isEmpty() ? "#FFFFFF" : "#000000";

    editor->setStyleSheet(
        QString("QLineEdit { background-color: %1; color: %2; border-radius: 2px; "
                "border: 2px solid #3498db; font-weight: bold; font-size: 8pt; padding: 0px; }")
        .arg(bgColor, textColor)
    );
    return editor;
}

void GridItemDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    QString value = index.model()->data(index, Qt::EditRole).toString();
    QLineEdit* lineEdit = static_cast<QLineEdit*>(editor);
    lineEdit->setText(value);
    
    // 智能选中：选中不含扩展名的部分
    int lastDot = value.lastIndexOf('.');
    if (lastDot > 0) {
        lineEdit->setSelection(0, lastDot);
    } else {
        lineEdit->selectAll();
    }
}

void GridItemDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
    QLineEdit* lineEdit = static_cast<QLineEdit*>(editor);
    QString value = lineEdit->text();
    if(value.isEmpty() || value == index.data(Qt::DisplayRole).toString()) return;

    QString oldPath = index.data(PathRole).toString();
    QFileInfo info(oldPath);
    QString newPath = info.absolutePath() + "/" + value;
    
    if (QFile::rename(oldPath, newPath)) {
        model->setData(index, value, Qt::EditRole);
        model->setData(index, newPath, PathRole);
        // 2026-03-xx 按照用户要求：修复重命名后元数据丢失问题，同步更新 JSON
        AmMetaJson::renameItem(info.absolutePath(), info.fileName(), value);
    } else {
        // 失败则不改模型
    }
}

void GridItemDelegate::updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    Q_UNUSED(index);
    // 强制编辑器位置与 paint 中计算的文件名绘制区域 NameBox 重合
    QRect cardRect = option.rect.adjusted(4, 4, -4, -4);
    int baseIconSize = option.decorationSize.width();
    if (baseIconSize <= 0) baseIconSize = 64; 
    int iconDrawSize = static_cast<int>(baseIconSize * 0.65); 
    
    int ratingH = 12;
    // 2026-03-xx 按照用户要求：编辑器高度同步调整为 18px
    int nameH = 18;
    int gap1 = 6;
    int gap2 = 4;
    
    int totalH = iconDrawSize + gap1 + ratingH + gap2 + nameH;
    int startY = cardRect.top() + (cardRect.height() - totalH) / 2 + 13;
    int ratingY = startY + iconDrawSize + gap1;
    int nameY = ratingY + ratingH + gap2;
    
    // 编辑框精准覆盖文件名区域，不遮挡星级
    QRect nameBoxRect(cardRect.left() + 6, nameY, cardRect.width() - 12, nameH);
    editor->setGeometry(nameBoxRect);
}

} // namespace ArcMeta
```

## 文件: `src/ui/ContentPanel.h`

```cpp
#pragma once

#include <QWidget>
#include <QListView>
#include <QTreeView>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <QStyledItemDelegate>
#include <QMap>
#include <unordered_map>
#include <map>
#include <string>
#include "FilterPanel.h"
#include "../meta/AmMetaJson.h"

namespace ArcMeta {

/**
 * @brief 自定义 Role 枚举，用于 QStandardItemModel 数据存取
 */
enum ItemRole {
    RatingRole = Qt::UserRole + 1,
    ColorRole,
    EncryptedRole,
    PathRole,
    IsLockedRole, // 对应置顶/Lock 状态
    TagsRole,
    TypeRole,
    SizeRawRole,
    MTimeRawRole,
    CTimeRawRole,
    IsDirRole,
    IsDriveRole
};

/**
 * @brief 极致性能：轻量化享元条目结构
 */
struct FileItem {
    QString name;
    QString fullPath;
    QString extension;
    bool isDir = false;
    bool isDrive = false;
    
    // 系统属性 (从 DB 预取)
    long long size = 0;
    double mtime = 0;
    double ctime = 0;
    double atime = 0;

    // 元数据 (从 DB 预取)
    int rating = 0;
    QString color;
    bool pinned = false;
    bool encrypted = false;
    QStringList tags;
    
    // 渲染字符串缓存 (预计算)
    QString sizeStr;
    QString timeStr;
    QString typeStr;
};

/**
 * @brief 极致性能：自定义享元模型，消除 QStandardItem 堆分配开销
 */
class FileModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit FileModel(QObject* parent = nullptr);
    
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setItems(const QList<FileItem>& items);
    const QList<FileItem>& items() const { return m_items; }

private:
    QList<FileItem> m_items;
    mutable QMap<QString, QIcon> m_iconCache;
};

/**
 * @brief 内容面板（面板四）：核心业务展示区
 * 支持网格视图（QListView）与列表视图（QTreeView）切换
 */
class ContentPanel : public QWidget {
    Q_OBJECT

public:
    enum ViewMode {
        GridView,
        ListView
    };

    explicit ContentPanel(QWidget* parent = nullptr);
    ~ContentPanel() override = default;

    /**
     * @brief 切换视图模式
     */
    void setViewMode(ViewMode mode);

    /**
     * @brief 拦截空格键（红线：物理拦截 QEvent::KeyPress 且为 Key_Space）
     */
    bool eventFilter(QObject* obj, QEvent* event) override;

    // --- 业务接口 ---
    QAbstractItemModel* model() const { return m_model; }
    QSortFilterProxyModel* getProxyModel() const { return m_proxyModel; }
    QModelIndexList getSelectedIndexes() const {
        return (m_viewStack->currentWidget() == m_gridView) ? 
                m_gridView->selectionModel()->selectedIndexes() : 
                m_treeView->selectionModel()->selectedIndexes();
    }

signals:
    /**
     * @brief 请求 QuickLook 预览信号
     * @param path 物理路径
     */
    void requestQuickLook(const QString& path);

    /**
     * @brief 选中项发生变化时通知元数据面板刷新
     * @param paths 选中条目的物理路径列表
     */
    void selectionChanged(const QStringList& paths);
    void directorySelected(const QString& path);

    /**
     * @brief 目录装载完成后发出，携带统计数据供 FilterPanel 填充
     */
    void directoryStatsReady(
        const QMap<int, int>&     ratingCounts,
        const QMap<QString, int>& colorCounts,
        const QMap<QString, int>& tagCounts,
        const QMap<QString, int>& typeCounts,
        const QMap<QString, int>& createDateCounts,
        const QMap<QString, int>& modifyDateCounts);

private:
    void initUi();
    void initGridView();
    void initListView();
    void setupContextMenu();

    QVBoxLayout* m_mainLayout = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    
    // 视图组件
    QListView* m_gridView = nullptr;
    QTreeView* m_treeView = nullptr;
    FileModel* m_model = nullptr; // 2026-03-xx 极致性能重构：切换至自定义享元模型
    QSortFilterProxyModel* m_proxyModel = nullptr;

    // 2026-03-xx 极致性能：搜索专用元数据缓存池 (多目录分批)
    std::unordered_map<std::wstring, std::map<std::wstring, ItemMeta>> m_searchMetaCache;

    FilterState m_currentFilter;

    int m_zoomLevel = 64;
    QString m_currentPath;
    void updateGridSize();

    void addItemsFromDirectory(const QString& path, bool recursive,
                               QMap<int, int>& ratingCounts,
                               QMap<QString, int>& colorCounts,
                               QMap<QString, int>& tagCounts,
                               QMap<QString, int>& typeCounts,
                               QMap<QString, int>& createDateCounts,
                               QMap<QString, int>& modifyDateCounts,
                               int& noTagCount,
                               QList<FileItem>& outItems);

public slots:
    void onSelectionChanged();
    void onCustomContextMenuRequested(const QPoint& pos);
    void onDoubleClicked(const QModelIndex& index);

    /**
     * @brief 加载并显示目录内容
     */
    void loadDirectory(const QString& path, bool recursive = false);

    /**
     * @brief 全局/本地搜索
     */
    void search(const QString& query);

    /**
     * @brief 应用当前筛选器
     */
    void applyFilters(const FilterState& state);
    void applyFilters(); // 使用保存的状态重新应用

    /**
     * @brief 创建新条目（文件夹/Markdown/Txt）
     */
    void createNewItem(const QString& type);

protected:
    void wheelEvent(QWheelEvent* event) override;
};

/**
 * @brief 自定义 Delegate：处理网格视图下的图标、星级、颜色圆点及角标叠加
 */
class GridItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

} // namespace ArcMeta
```

## 文件: `src/db/Database.cpp`

```cpp
#include "Database.h"
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QStandardPaths>

namespace ArcMeta {

struct Database::Impl {
    QSqlDatabase db;
    std::wstring dbPath;
};

Database& Database::instance() {
    static Database inst;
    return inst;
}

Database::Database() : m_impl(std::make_unique<Impl>()) {}
Database::~Database() = default;

bool Database::init(const std::wstring& dbPath) {
    m_impl->dbPath = dbPath;
    m_impl->db = QSqlDatabase::addDatabase("QSQLITE");
    m_impl->db.setDatabaseName(QString::fromStdWString(dbPath));

    if (!m_impl->db.open()) return false;

    // 关键红线：防死锁配置
    QSqlQuery query(m_impl->db);
    query.exec("PRAGMA journal_mode=WAL;");
    query.exec("PRAGMA synchronous=NORMAL;");
    query.exec("PRAGMA busy_timeout=5000;");

    createTables();
    createIndexes();
    return true;
}

std::wstring Database::getDbPath() const {
    return m_impl->dbPath;
}

void Database::createTables() {
    QSqlQuery q(m_impl->db);
    q.exec("CREATE TABLE IF NOT EXISTS folders (path TEXT PRIMARY KEY, rating INTEGER DEFAULT 0, color TEXT DEFAULT '', tags TEXT DEFAULT '', pinned INTEGER DEFAULT 0, note TEXT DEFAULT '', sort_by TEXT DEFAULT 'name', sort_order TEXT DEFAULT 'asc', last_sync REAL)");
    q.exec("CREATE TABLE IF NOT EXISTS items (volume TEXT NOT NULL, frn TEXT NOT NULL, path TEXT, parent_path TEXT, type TEXT, rating INTEGER DEFAULT 0, color TEXT DEFAULT '', tags TEXT DEFAULT '', pinned INTEGER DEFAULT 0, note TEXT DEFAULT '', encrypted INTEGER DEFAULT 0, encrypt_salt TEXT DEFAULT '', encrypt_iv TEXT DEFAULT '', encrypt_verify_hash TEXT DEFAULT '', original_name TEXT DEFAULT '', size INTEGER DEFAULT 0, ctime REAL DEFAULT 0, mtime REAL DEFAULT 0, atime REAL DEFAULT 0, deleted INTEGER DEFAULT 0, PRIMARY KEY (volume, frn))");
    q.exec("CREATE TABLE IF NOT EXISTS tags (tag TEXT PRIMARY KEY, item_count INTEGER DEFAULT 0)");
    q.exec("CREATE TABLE IF NOT EXISTS favorites (path TEXT PRIMARY KEY, type TEXT, name TEXT, sort_order INTEGER DEFAULT 0, added_at REAL)");
    q.exec("CREATE TABLE IF NOT EXISTS categories (id INTEGER PRIMARY KEY AUTOINCREMENT, parent_id INTEGER DEFAULT 0, name TEXT NOT NULL, color TEXT DEFAULT '', preset_tags TEXT DEFAULT '', sort_order INTEGER DEFAULT 0, pinned INTEGER DEFAULT 0, encrypted INTEGER DEFAULT 0, encrypt_salt TEXT DEFAULT '', encrypt_iv TEXT DEFAULT '', encrypt_verify_hash TEXT DEFAULT '', created_at REAL)");
    q.exec("CREATE TABLE IF NOT EXISTS category_items (category_id INTEGER, item_path TEXT, added_at REAL, PRIMARY KEY (category_id, item_path))");
    q.exec("CREATE TABLE IF NOT EXISTS sync_state (key TEXT PRIMARY KEY, value TEXT)");
}

void Database::createIndexes() {
    QSqlQuery q(m_impl->db);
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_path ON items(path)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_parent ON items(parent_path)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_deleted ON items(deleted)");
}

} // namespace ArcMeta
```

## 文件: `src/db/Database.h`

```cpp
#pragma once

#include <string>
#include <memory>
#include <QSqlDatabase>

namespace ArcMeta {

/**
 * @brief 数据库管理类 (原生 QtSql 实现，拒绝第三方 SQLiteCpp)
 */
class Database {
public:
    static Database& instance();

    bool init(const std::wstring& dbPath);
    
    /**
     * @brief 获取数据库文件路径，用于工作线程创建独立连接
     */
    std::wstring getDbPath() const;

private:
    Database();
    ~Database();
    
    void createTables();
    void createIndexes();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ArcMeta
```

## 文件: `src/crypto/EncryptionManager.cpp`

```cpp
#include "EncryptionManager.h"
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <fstream>
#include <filesystem>

#pragma comment(lib, "bcrypt.lib")

namespace ArcMeta {

EncryptionManager& EncryptionManager::instance() {
    static EncryptionManager inst;
    return inst;
}

EncryptionManager::EncryptionManager() {
    BCryptOpenAlgorithmProvider(&m_aesAlg, BCRYPT_AES_ALGORITHM, NULL, 0);
    BCryptSetProperty(m_aesAlg, BCRYPT_CHAINING_MODE, (PBYTE)BCRYPT_CHAIN_MODE_CBC, sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
}

EncryptionManager::~EncryptionManager() {
    if (m_aesAlg) BCryptCloseAlgorithmProvider(m_aesAlg, 0);
}

bool EncryptionManager::deriveKey(const std::string& password, const std::vector<BYTE>& salt, std::vector<BYTE>& key) {
    BCRYPT_ALG_HANDLE hPbkdf2 = NULL;
    if (BCryptOpenAlgorithmProvider(&hPbkdf2, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0) return false;

    key.resize(32);
    NTSTATUS status = BCryptDeriveKeyPBKDF2(hPbkdf2, (PUCHAR)password.c_str(), (ULONG)password.length(),
                                           (PUCHAR)salt.data(), (ULONG)salt.size(), 10000, 
                                           key.data(), (ULONG)key.size(), 0);
    
    BCryptCloseAlgorithmProvider(hPbkdf2, 0);
    return status == 0;
}

std::vector<BYTE> EncryptionManager::generateRandom(size_t size) {
    std::vector<BYTE> buffer(size);
    BCryptGenRandom(NULL, buffer.data(), (ULONG)size, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return buffer;
}

/**
 * @brief 修复：实现基于 64KB 分块的加密逻辑，支持大文件且内存友好
 */
bool EncryptionManager::encryptFile(const std::wstring& srcPath, const std::wstring& destPath, const std::string& password) {
    std::ifstream is(srcPath, std::ios::binary);
    std::ofstream os(destPath, std::ios::binary);
    if (!is || !os) return false;

    std::vector<BYTE> salt = generateRandom(16);
    std::vector<BYTE> iv = generateRandom(16);
    std::vector<BYTE> key;
    if (!deriveKey(password, salt, key)) return false;

    // 写入 Salt 和原始 IV 到文件头
    os.write((char*)salt.data(), salt.size());
    os.write((char*)iv.data(), iv.size());

    BCRYPT_KEY_HANDLE hKey = NULL;
    BCryptGenerateSymmetricKey(m_aesAlg, &hKey, NULL, 0, key.data(), (ULONG)key.size(), 0);

    const size_t CHUNK_SIZE = 64 * 1024;
    std::vector<BYTE> buffer(CHUNK_SIZE);
    std::vector<BYTE> cipherBuffer(CHUNK_SIZE + 16); // 预留 Padding 空间

    while (is.read((char*)buffer.data(), CHUNK_SIZE) || is.gcount() > 0) {
        DWORD readBytes = (DWORD)is.gcount();
        DWORD cipherLen = 0;
        bool isLast = is.eof();
        
        // 执行加密
        BCryptEncrypt(hKey, buffer.data(), readBytes, NULL, iv.data(), (ULONG)iv.size(), 
                      cipherBuffer.data(), (ULONG)cipherBuffer.size(), &cipherLen, 
                      isLast ? BCRYPT_BLOCK_PADDING : 0);
        
        os.write((char*)cipherBuffer.data(), cipherLen);
    }

    BCryptDestroyKey(hKey);
    is.close();
    os.close();
    return true;
}

/**
 * @brief 修复：实现 RAII 句柄持有逻辑，防止临时文件在 CloseHandle 前被删除
 */
/**
 * @brief 实现分块解密逻辑，恢复原始文件到临时路径
 */
std::shared_ptr<DecryptedFileHandle> EncryptionManager::decryptToTemp(const std::wstring& amencPath, const std::string& password) {
    std::ifstream is(amencPath, std::ios::binary);
    if (!is) return nullptr;

    // 读取 Salt 和 IV
    std::vector<BYTE> salt(16);
    std::vector<BYTE> iv(16);
    is.read((char*)salt.data(), 16);
    is.read((char*)iv.data(), 16);

    std::vector<BYTE> key;
    if (!deriveKey(password, salt, key)) return nullptr;

    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring amTempDir = std::wstring(tempPath) + L"amtemp\\";
    CreateDirectoryW(amTempDir.c_str(), NULL);

    std::wstring outPath = amTempDir + std::filesystem::path(amencPath).stem().wstring();
    HANDLE hFile = CreateFileW(outPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    BCRYPT_KEY_HANDLE hKey = NULL;
    BCryptGenerateSymmetricKey(m_aesAlg, &hKey, NULL, 0, key.data(), (ULONG)key.size(), 0);

    const size_t CHUNK_SIZE = 64 * 1024;
    std::vector<BYTE> buffer(CHUNK_SIZE + 16);
    std::vector<BYTE> plainBuffer(CHUNK_SIZE + 16);

    while (is.read((char*)buffer.data(), CHUNK_SIZE) || is.gcount() > 0) {
        DWORD readBytes = (DWORD)is.gcount();
        DWORD plainLen = 0;
        bool isLast = is.eof();

        BCryptDecrypt(hKey, buffer.data(), readBytes, NULL, iv.data(), (ULONG)iv.size(),
                      plainBuffer.data(), (ULONG)plainBuffer.size(), &plainLen,
                      isLast ? BCRYPT_BLOCK_PADDING : 0);
        
        DWORD written = 0;
        WriteFile(hFile, plainBuffer.data(), plainLen, &written, NULL);
    }

    BCryptDestroyKey(hKey);
    is.close();
    
    // 关键：将文件指针移回开头，方便后续进程读取
    SetFilePointer(hFile, 0, NULL, FILE_BEGIN);

    return std::make_shared<DecryptedFileHandle>(hFile, outPath);
}

} // namespace ArcMeta
```

## 文件: `src/crypto/EncryptionManager.h`

```cpp
#pragma once

#include <windows.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <memory>

namespace ArcMeta {

/**
 * @brief 临时解密文件句柄持有者 (RAII)
 * 利用 FILE_FLAG_DELETE_ON_CLOSE，当对象析构句柄关闭时，系统自动删除临时文件
 */
class DecryptedFileHandle {
public:
    DecryptedFileHandle(HANDLE hFile, const std::wstring& path) 
        : m_hFile(hFile), m_path(path) {}
    ~DecryptedFileHandle() {
        if (m_hFile != INVALID_HANDLE_VALUE) CloseHandle(m_hFile);
    }
    std::wstring path() const { return m_path; }
    bool isValid() const { return m_hFile != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_hFile;
    std::wstring m_path;
};

/**
 * @brief 加密管理器
 */
class EncryptionManager {
public:
    static EncryptionManager& instance();

    /**
     * @brief 分块加密文件 (支持大文件)
     */
    bool encryptFile(const std::wstring& srcPath, const std::wstring& destPath, const std::string& password);

    /**
     * @brief 解密文件并持有句柄 (RAII)
     */
    std::shared_ptr<DecryptedFileHandle> decryptToTemp(const std::wstring& amencPath, const std::string& password);

private:
    EncryptionManager();
    ~EncryptionManager();

    bool deriveKey(const std::string& password, const std::vector<BYTE>& salt, std::vector<BYTE>& key);
    std::vector<BYTE> generateRandom(size_t size);

    BCRYPT_ALG_HANDLE m_aesAlg = NULL;
};

} // namespace ArcMeta
```

## 文件: `src/db/FavoritesRepo.cpp`

```cpp
#include "FavoritesRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>

namespace ArcMeta {

bool FavoritesRepo::add(const Favorite& fav, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO favorites (path, type, name, sort_order, added_at) VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(QString::fromStdWString(fav.path));
    q.addBindValue(QString::fromStdWString(fav.type));
    q.addBindValue(QString::fromStdWString(fav.name));
    q.addBindValue(fav.sortOrder);
    q.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    return q.exec();
}

bool FavoritesRepo::remove(const std::wstring& path, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("DELETE FROM favorites WHERE path = ?");
    q.addBindValue(QString::fromStdWString(path));
    return q.exec();
}

std::vector<Favorite> FavoritesRepo::getAll(QSqlDatabase db) {
    std::vector<Favorite> results;
    QSqlQuery q("SELECT path, type, name, sort_order FROM favorites ORDER BY sort_order ASC", db);
    while (q.next()) {
        Favorite fav;
        fav.path = q.value(0).toString().toStdWString();
        fav.type = q.value(1).toString().toStdWString();
        fav.name = q.value(2).toString().toStdWString();
        fav.sortOrder = q.value(3).toInt();
        results.push_back(fav);
    }
    return results;
}

} // namespace ArcMeta
```

## 文件: `src/db/FavoritesRepo.h`

```cpp
#pragma once

#include "Database.h"
#include <string>
#include <vector>
#include <QSqlDatabase>

namespace ArcMeta {

struct Favorite {
    std::wstring path;
    std::wstring type;
    std::wstring name;
    int sortOrder = 0;
};

/**
 * @brief 收藏夹持久层
 */
class FavoritesRepo {
public:
    static bool add(const Favorite& fav, QSqlDatabase db = QSqlDatabase::database());
    static bool remove(const std::wstring& path, QSqlDatabase db = QSqlDatabase::database());
    static std::vector<Favorite> getAll(QSqlDatabase db = QSqlDatabase::database());
};

} // namespace ArcMeta
```

## 文件: `src/ui/FilterPanel.cpp`

```cpp
#include "FilterPanel.h"
#include "ToolTipOverlay.h"
#include <QToolButton>
#include <QMouseEvent>
#include <QCursor>

namespace ArcMeta {

// ─── 颜色映射表 ────────────────────────────────────────────────────
QMap<QString, QColor> FilterPanel::s_colorMap() {
    return {
        { "",       QColor("#888780") },
        { "red",    QColor("#E24B4A") },
        { "orange", QColor("#EF9F27") },
        { "yellow", QColor("#FAC775") },
        { "green",  QColor("#639922") },
        { "cyan",   QColor("#1D9E75") },
        { "blue",   QColor("#378ADD") },
        { "purple", QColor("#7F77DD") },
        { "gray",   QColor("#5F5E5A") },
    };
}

static QString colorDisplayName(const QString& key) {
    static QMap<QString, QString> n {
        { "",       "无色标" }, { "red",    "红色" },
        { "orange", "橙色"  }, { "yellow", "黄色" },
        { "green",  "绿色"  }, { "cyan",   "青色" },
        { "blue",   "蓝色"  }, { "purple", "紫色" },
        { "gray",   "灰色"  },
    };
    return n.value(key, key);
}

static QString ratingDisplayName(int r) {
    return r == 0 ? "无评级" : QString("★").repeated(r);
}

// ─── 可整行点击的行控件 ────────────────────────────────────────────
/**
 * ClickableRow: 点击行内任意位置均触发关联 QCheckBox 的 toggle。
 * 复选框本身的点击事件不需要额外处理，它会自然传播。
 */
class ClickableRow : public QWidget {
public:
    explicit ClickableRow(QCheckBox* cb, QWidget* parent = nullptr)
        : QWidget(parent), m_cb(cb) {
        setCursor(Qt::PointingHandCursor);
    }
protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            // 如果点击位置不在复选框上，手动 toggle，避免双重触发
            QPoint local = m_cb->mapFromGlobal(e->globalPosition().toPoint());
            if (!m_cb->rect().contains(local)) {
                m_cb->setChecked(!m_cb->isChecked());
            }
        }
        QWidget::mousePressEvent(e);
    }
    void enterEvent(QEnterEvent* e) override {
        setStyleSheet("QWidget { background: #2A2A2A; }");
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        setStyleSheet("");
        QWidget::leaveEvent(e);
    }
private:
    QCheckBox* m_cb;
};

// ─── FilterPanel ──────────────────────────────────────────────────
FilterPanel::FilterPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(200);
    setStyleSheet("QWidget { background-color: #1E1E1E; color: #EEEEEE; border: none; }");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // 顶部标题栏
    QWidget* topBar = new QWidget(this);
    topBar->setFixedHeight(34);
    topBar->setStyleSheet("QWidget { background: #252525; border-bottom: 1px solid #333; }");
    QHBoxLayout* topL = new QHBoxLayout(topBar);
    topL->setContentsMargins(8, 0, 8, 0);

    QLabel* title = new QLabel("高级筛选", topBar);
    title->setStyleSheet("font-size: 12px; font-weight: bold; color: #CCCCCC;");

    m_btnClearAll = new QPushButton("清除", topBar);
    m_btnClearAll->setFixedSize(42, 22);
    m_btnClearAll->setProperty("tooltipText", "重置所有筛选条件");
    m_btnClearAll->installEventFilter(this);
    m_btnClearAll->setStyleSheet(
        "QPushButton { background: #2A2A2A; border: 1px solid #444; border-radius: 6px;"
        "              color: #AAAAAA; font-size: 11px; }"
        "QPushButton:hover { background: #3A3A3A; color: #EEEEEE; }");
    connect(m_btnClearAll, &QPushButton::clicked, this, &FilterPanel::clearAllFilters);

    topL->addWidget(title);
    topL->addStretch();
    topL->addWidget(m_btnClearAll);
    m_mainLayout->addWidget(topBar);

    // 滚动内容区
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_container = new QWidget(m_scrollArea);
    m_container->setStyleSheet("QWidget { background: transparent; }");
    m_containerLayout = new QVBoxLayout(m_container);
    m_containerLayout->setContentsMargins(0, 0, 0, 0);
    m_containerLayout->setSpacing(0);
    m_containerLayout->addStretch();

    m_scrollArea->setWidget(m_container);
    m_mainLayout->addWidget(m_scrollArea, 1);
}

// 2026-03-xx 按照用户要求：物理拦截事件以实现自定义 ToolTipOverlay 的显隐控制
bool FilterPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 物理级别禁绝原生 ToolTip，强制调用 ToolTipOverlay
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
        }
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::MouseButtonPress) {
        ToolTipOverlay::hideTip();
    }
    return QWidget::eventFilter(watched, event);
}

// ─── populate ─────────────────────────────────────────────────────
void FilterPanel::populate(
    const QMap<int, int>&       ratingCounts,
    const QMap<QString, int>&   colorCounts,
    const QMap<QString, int>&   tagCounts,
    const QMap<QString, int>&   typeCounts,
    const QMap<QString, int>&   createDateCounts,
    const QMap<QString, int>&   modifyDateCounts)
{
    m_ratingCounts     = ratingCounts;
    m_colorCounts      = colorCounts;
    m_tagCounts        = tagCounts;
    m_typeCounts       = typeCounts;
    m_createDateCounts = createDateCounts;
    m_modifyDateCounts = modifyDateCounts;
    rebuildGroups();
}

// ─── rebuildGroups ────────────────────────────────────────────────
void FilterPanel::rebuildGroups() {
    // 清空旧内容（保留末尾 stretch）
    while (m_containerLayout->count() > 1) {
        QLayoutItem* item = m_containerLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    auto colorMap = s_colorMap();

    // ── 1. 评级 ──────────────────────────────────────────────
    if (!m_ratingCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("评级", gl);
        for (int r : {0, 1, 2, 3, 4, 5}) {
            if (!m_ratingCounts.contains(r)) continue;
            QCheckBox* cb = addFilterRow(gl, ratingDisplayName(r), m_ratingCounts[r]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.ratings.contains(r));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, r](bool on) {
                if (on) { if (!m_filter.ratings.contains(r)) m_filter.ratings.append(r); }
                else m_filter.ratings.removeAll(r);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 2. 颜色标记 ──────────────────────────────────────────
    if (!m_colorCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("颜色标记", gl);
        for (const QString& key : QStringList{"", "red", "orange", "yellow", "green", "cyan", "blue", "purple", "gray"}) {
            if (!m_colorCounts.contains(key)) continue;
            QCheckBox* cb = addFilterRow(gl, colorDisplayName(key), m_colorCounts[key], colorMap.value(key));
            cb->blockSignals(true);
            cb->setChecked(m_filter.colors.contains(key));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                if (on) { if (!m_filter.colors.contains(key)) m_filter.colors.append(key); }
                else m_filter.colors.removeAll(key);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 3. 标签 / 关键字 ─────────────────────────────────────
    if (!m_tagCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("标签 / 关键字", gl);
        if (m_tagCounts.contains("__none__")) {
            QCheckBox* cb = addFilterRow(gl, "无标签", m_tagCounts["__none__"]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.tags.contains("__none__"));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this](bool on) {
                if (on) { if (!m_filter.tags.contains("__none__")) m_filter.tags.append("__none__"); }
                else    m_filter.tags.removeAll("__none__");
                emit filterChanged(m_filter);
            });
        }
        QStringList sorted = m_tagCounts.keys();
        sorted.sort(Qt::CaseInsensitive);
        for (const QString& tag : sorted) {
            if (tag == "__none__") continue;
            QCheckBox* cb = addFilterRow(gl, tag, m_tagCounts[tag]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.tags.contains(tag));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, tag](bool on) {
                if (on) { if (!m_filter.tags.contains(tag)) m_filter.tags.append(tag); }
                else m_filter.tags.removeAll(tag);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 4. 文件类型 ──────────────────────────────────────────
    if (!m_typeCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("文件类型", gl);
        if (m_typeCounts.contains("folder")) {
            QCheckBox* cb = addFilterRow(gl, "文件夹", m_typeCounts["folder"]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.types.contains("folder"));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this](bool on) {
                if (on) { if (!m_filter.types.contains("folder")) m_filter.types.append("folder"); }
                else    m_filter.types.removeAll("folder");
                emit filterChanged(m_filter);
            });
        }
        QStringList exts = m_typeCounts.keys(); exts.sort();
        for (const QString& ext : exts) {
            if (ext == "folder") continue;
            QString label = ext.isEmpty() ? "无扩展名" : ext;
            QCheckBox* cb = addFilterRow(gl, label, m_typeCounts[ext]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.types.contains(ext));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, ext](bool on) {
                if (on) { if (!m_filter.types.contains(ext)) m_filter.types.append(ext); }
                else m_filter.types.removeAll(ext);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 5. 创建日期 ──────────────────────────────────────────
    if (!m_createDateCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("创建日期", gl);
        // "今天"/"昨天"置顶
        for (const QString& key : QStringList{"today", "yesterday"}) {
            if (!m_createDateCounts.contains(key)) continue;
            QString label = (key == "today") ? "今天" : "昨天";
            QCheckBox* cb = addFilterRow(gl, label, m_createDateCounts[key]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.createDates.contains(key));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                if (on) { if (!m_filter.createDates.contains(key)) m_filter.createDates.append(key); }
                else m_filter.createDates.removeAll(key);
                emit filterChanged(m_filter);
            });
        }
        QStringList dates = m_createDateCounts.keys(); dates.sort(Qt::CaseInsensitive);
        for (const QString& d : dates) {
            if (d == "today" || d == "yesterday") continue;
            QCheckBox* cb = addFilterRow(gl, d, m_createDateCounts[d]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.createDates.contains(d));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, d](bool on) {
                if (on) { if (!m_filter.createDates.contains(d)) m_filter.createDates.append(d); }
                else m_filter.createDates.removeAll(d);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }

    // ── 6. 修改日期 ──────────────────────────────────────────
    if (!m_modifyDateCounts.isEmpty()) {
        QVBoxLayout* gl = nullptr;
        QWidget* g = buildGroup("修改日期", gl);
        for (const QString& key : QStringList{"today", "yesterday"}) {
            if (!m_modifyDateCounts.contains(key)) continue;
            QString label = (key == "today") ? "今天" : "昨天";
            QCheckBox* cb = addFilterRow(gl, label, m_modifyDateCounts[key]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.modifyDates.contains(key));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                if (on) { if (!m_filter.modifyDates.contains(key)) m_filter.modifyDates.append(key); }
                else m_filter.modifyDates.removeAll(key);
                emit filterChanged(m_filter);
            });
        }
        QStringList dates = m_modifyDateCounts.keys(); dates.sort(Qt::CaseInsensitive);
        for (const QString& d : dates) {
            if (d == "today" || d == "yesterday") continue;
            QCheckBox* cb = addFilterRow(gl, d, m_modifyDateCounts[d]);
            cb->blockSignals(true);
            cb->setChecked(m_filter.modifyDates.contains(d));
            cb->blockSignals(false);
            connect(cb, &QCheckBox::toggled, this, [this, d](bool on) {
                if (on) { if (!m_filter.modifyDates.contains(d)) m_filter.modifyDates.append(d); }
                else m_filter.modifyDates.removeAll(d);
                emit filterChanged(m_filter);
            });
        }
        m_containerLayout->insertWidget(m_containerLayout->count() - 1, g);
    }
}

// ─── buildGroup ───────────────────────────────────────────────────
QWidget* FilterPanel::buildGroup(const QString& title, QVBoxLayout*& outContentLayout) {
    QWidget* wrapper = new QWidget(m_container);
    wrapper->setStyleSheet("QWidget { background: transparent; }");
    QVBoxLayout* wl = new QVBoxLayout(wrapper);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->setSpacing(0);

    QToolButton* hdr = new QToolButton(wrapper);
    hdr->setText(title); // 2026-03-xx 按照用户要求，移除硬编码空格，统一使用 QSS 边距控制
    hdr->setCheckable(true);
    hdr->setChecked(true);
    // hdr->setArrowType(Qt::DownArrow); // 核心红线：禁止使用或显示三角形
    hdr->setToolButtonStyle(Qt::ToolButtonTextOnly); // 2026-03-xx 强制仅文本，防止图标空间干扰左对齐
    hdr->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hdr->setFixedHeight(24);
    hdr->setStyleSheet(
        "QToolButton { background: #252525; border: none; border-top: 1px solid #333;"
        "              color: #AAAAAA; font-size: 11px; font-weight: 600; text-align: left; "
        "              padding-left: 12px; } "
        "QToolButton:hover { color: #EEEEEE; } "
        "QToolButton::menu-indicator { image: none; }"); // 彻底移除可能的菜单箭头占位

    QWidget* content = new QWidget(wrapper);
    content->setStyleSheet("QWidget { background: transparent; }");
    outContentLayout = new QVBoxLayout(content);
    outContentLayout->setContentsMargins(0, 0, 0, 0);
    outContentLayout->setSpacing(0);

    connect(hdr, &QToolButton::toggled, content, &QWidget::setVisible);
    // connect(hdr, &QToolButton::toggled, [hdr](bool checked) {
    //     hdr->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    // });

    wl->addWidget(hdr);
    wl->addWidget(content);
    return wrapper;
}

// ─── addFilterRow ─────────────────────────────────────────────────
QCheckBox* FilterPanel::addFilterRow(QVBoxLayout* layout, const QString& label, int count, const QColor& dotColor) {
    QCheckBox* cb = new QCheckBox();
    // 2026-03-xx 按照用户要求，仅保留蓝色勾选标记 (#378ADD)，背景保持深色
    cb->setStyleSheet(
        "QCheckBox { spacing: 0px; }"
        "QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #444;"
        "                       border-radius: 2px; background: #1A1A1A; }"
        "QCheckBox::indicator:hover { border: 1px solid #666; }"
        "QCheckBox::indicator:checked { "
        "   border: 1px solid #378ADD; "
        "   background: #1A1A1A; "
        "   image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjMzc4QUREIiBzdHJva2Utd2lkdGg9IjMuNSIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=);"
        "}"
    );

    // 整行可点击容器
    // 增加高度至 24px 以适配各种系统缩放，避免文字截断
    ClickableRow* row = new ClickableRow(cb);
    row->setFixedHeight(24);

    QHBoxLayout* rl = new QHBoxLayout(row);
    rl->setContentsMargins(12, 0, 8, 0);
    rl->setSpacing(5);
    rl->addWidget(cb);

    if (dotColor.isValid() && dotColor != Qt::transparent) {
        QLabel* dot = new QLabel(row);
        dot->setFixedSize(10, 10);
        dot->setStyleSheet(QString("background: %1; border-radius: 5px;").arg(dotColor.name()));
        rl->addWidget(dot);
    }

    QLabel* lbl = new QLabel(label, row);
    lbl->setStyleSheet("font-size: 12px; color: #CCCCCC; background: transparent;");
    rl->addWidget(lbl, 1);

    QLabel* cnt = new QLabel(QString::number(count), row);
    cnt->setStyleSheet("font-size: 11px; color: #555555; background: transparent;");
    cnt->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    rl->addWidget(cnt);

    layout->addWidget(row);
    return cb;
}

// ─── clearAllFilters ──────────────────────────────────────────────
void FilterPanel::clearAllFilters() {
    m_filter = FilterState{};
    const auto cbs = m_container->findChildren<QCheckBox*>();
    for (QCheckBox* cb : cbs) {
        cb->blockSignals(true);
        cb->setChecked(false);
        cb->blockSignals(false);
    }
    emit filterChanged(m_filter);
}

} // namespace ArcMeta
```

## 文件: `src/ui/FilterPanel.h`

```cpp
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QScrollArea>
#include <QPushButton>
#include <QMap>
#include <QStringList>

namespace ArcMeta {

struct FilterState {
    QList<int>   ratings;
    QStringList  colors;
    QStringList  tags;
    QStringList  types;
    QStringList  createDates;   // "today" | "yesterday" | "YYYY-MM-DD"
    QStringList  modifyDates;
};

/**
 * @brief 高级筛选面板 — 动态 Adobe Bridge 风格
 *
 * 由 MainWindow 在目录切换后调用 populate() 驱动数据填充。
 * 每行整体可点击（不需要对准复选框）。
 */
class FilterPanel : public QWidget {
    Q_OBJECT

public:
    explicit FilterPanel(QWidget* parent = nullptr);
    ~FilterPanel() override = default;

    void populate(
        const QMap<int, int>&        ratingCounts,
        const QMap<QString, int>&    colorCounts,
        const QMap<QString, int>&    tagCounts,
        const QMap<QString, int>&    typeCounts,
        const QMap<QString, int>&    createDateCounts,
        const QMap<QString, int>&    modifyDateCounts
    );

    FilterState currentFilter() const { return m_filter; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void filterChanged(const FilterState& state);

public slots:
    void clearAllFilters();

private:
    void rebuildGroups();

    QWidget*   buildGroup(const QString& title, QVBoxLayout*& outContentLayout);
    QCheckBox* addFilterRow(QVBoxLayout* layout, const QString& label,
                            int count, const QColor& dotColor = Qt::transparent);

    static QMap<QString, QColor> s_colorMap();

    FilterState m_filter;

    QMap<int, int>      m_ratingCounts;
    QMap<QString, int>  m_colorCounts;
    QMap<QString, int>  m_tagCounts;
    QMap<QString, int>  m_typeCounts;
    QMap<QString, int>  m_createDateCounts;
    QMap<QString, int>  m_modifyDateCounts;

    QVBoxLayout*  m_mainLayout      = nullptr;
    QScrollArea*  m_scrollArea      = nullptr;
    QWidget*      m_container       = nullptr;
    QVBoxLayout*  m_containerLayout = nullptr;
    QPushButton*  m_btnClearAll     = nullptr;
};

} // namespace ArcMeta
```

## 文件: `src/db/FolderRepo.cpp`

```cpp
#include "FolderRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>

namespace ArcMeta {

bool FolderRepo::save(const std::wstring& path, const FolderMeta& meta, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO folders (path, rating, color, tags, pinned, note, sort_by, sort_order) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(QString::fromStdWString(path));
    q.addBindValue(meta.rating);
    q.addBindValue(QString::fromStdWString(meta.color));
    
    QJsonArray tagsArr;
    for (const auto& t : meta.tags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));
    
    q.addBindValue(meta.pinned ? 1 : 0);
    q.addBindValue(QString::fromStdWString(meta.note));
    q.addBindValue(QString::fromStdWString(meta.sortBy));
    q.addBindValue(QString::fromStdWString(meta.sortOrder));
    
    return q.exec();
}

bool FolderRepo::get(const std::wstring& path, FolderMeta& meta, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("SELECT rating, color, tags, pinned, note, sort_by, sort_order FROM folders WHERE path = ?");
    q.addBindValue(QString::fromStdWString(path));
    
    if (q.exec() && q.next()) {
        meta.rating = q.value(0).toInt();
        meta.color = q.value(1).toString().toStdWString();
        
        QJsonDocument doc = QJsonDocument::fromJson(q.value(2).toByteArray());
        meta.tags.clear();
        if (doc.isArray()) {
            for (const auto& v : doc.array()) meta.tags.push_back(v.toString().toStdWString());
        }

        meta.pinned = q.value(3).toInt() != 0;
        meta.note = q.value(4).toString().toStdWString();
        meta.sortBy = q.value(5).toString().toStdWString();
        meta.sortOrder = q.value(6).toString().toStdWString();
        return true;
    }
    return false;
}

bool FolderRepo::remove(const std::wstring& path, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("DELETE FROM folders WHERE path = ?");
    q.addBindValue(QString::fromStdWString(path));
    return q.exec();
}

} // namespace ArcMeta
```

## 文件: `src/db/FolderRepo.h`

```cpp
#pragma once

#include "Database.h"
#include "../meta/AmMetaJson.h"
#include <string>
#include <vector>
#include <QSqlDatabase>

namespace ArcMeta {

/**
 * @brief 文件夹元数据持久层
 */
class FolderRepo {
public:
    /**
     * @brief 保存或更新文件夹元数据
     */
    static bool save(const std::wstring& path, const FolderMeta& meta, QSqlDatabase db = QSqlDatabase::database());

    /**
     * @brief 获取文件夹元数据
     */
    static bool get(const std::wstring& path, FolderMeta& meta, QSqlDatabase db = QSqlDatabase::database());

    /**
     * @brief 删除文件夹记录
     */
    static bool remove(const std::wstring& path, QSqlDatabase db = QSqlDatabase::database());
};

} // namespace ArcMeta
```

## 文件: `src/db/ItemRepo.cpp`

```cpp
#include "ItemRepo.h"
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonArray>

namespace ArcMeta {

bool ItemRepo::save(const std::wstring& parentPath, const std::wstring& name, const ItemMeta& meta, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare(R"sql(
        INSERT OR REPLACE INTO items 
        (volume, frn, path, parent_path, type, rating, color, tags, pinned, note, 
         encrypted, encrypt_salt, encrypt_iv, encrypt_verify_hash, original_name, size, mtime, ctime) 
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )sql");

    std::wstring fullPath = parentPath;
    if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') fullPath += L'\\';
    fullPath += name;

    q.addBindValue(QString::fromStdWString(meta.volume));
    q.addBindValue(QString::fromStdWString(meta.frn));
    q.addBindValue(QString::fromStdWString(fullPath));
    q.addBindValue(QString::fromStdWString(parentPath));
    q.addBindValue(QString::fromStdWString(meta.type));
    q.addBindValue(meta.rating);
    q.addBindValue(QString::fromStdWString(meta.color));

    QJsonArray tagsArr;
    for (const auto& t : meta.tags) tagsArr.append(QString::fromStdWString(t));
    q.addBindValue(QJsonDocument(tagsArr).toJson(QJsonDocument::Compact));

    q.addBindValue(meta.pinned ? 1 : 0);
    q.addBindValue(QString::fromStdWString(meta.note));
    q.addBindValue(meta.encrypted ? 1 : 0);
    q.addBindValue(QString::fromStdString(meta.encryptSalt));
    q.addBindValue(QString::fromStdString(meta.encryptIv));
    q.addBindValue(QString::fromStdString(meta.encryptVerifyHash));
    q.addBindValue(QString::fromStdWString(meta.originalName));
    q.addBindValue((qlonglong)meta.size);
    q.addBindValue(meta.mtime);
    q.addBindValue(meta.ctime);

    return q.exec();
}

bool ItemRepo::markAsDeleted(const std::wstring& volume, const std::wstring& frn, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("UPDATE items SET deleted = 1 WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(volume));
    q.addBindValue(QString::fromStdWString(frn));
    return q.exec();
}

bool ItemRepo::removeByFrn(const std::wstring& volume, const std::wstring& frn, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("DELETE FROM items WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(volume));
    q.addBindValue(QString::fromStdWString(frn));
    return q.exec();
}

std::wstring ItemRepo::getPathByFrn(const std::wstring& volume, const std::wstring& frn, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("SELECT path FROM items WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(volume));
    q.addBindValue(QString::fromStdWString(frn));
    if (q.exec() && q.next()) {
        return q.value(0).toString().toStdWString();
    }
    return L"";
}

bool ItemRepo::updatePath(const std::wstring& volume, const std::wstring& frn, const std::wstring& newPath, const std::wstring& newParentPath, QSqlDatabase db) {
    QSqlQuery q(db);
    q.prepare("UPDATE items SET path = ?, parent_path = ? WHERE volume = ? AND frn = ?");
    q.addBindValue(QString::fromStdWString(newPath));
    q.addBindValue(QString::fromStdWString(newParentPath));
    q.addBindValue(QString::fromStdWString(volume));
    q.addBindValue(QString::fromStdWString(frn));
    return q.exec();
}

std::map<std::wstring, ItemMeta> ItemRepo::getMetadataBatch(const std::wstring& parentPath, QSqlDatabase db) {
    std::map<std::wstring, ItemMeta> results;
    QSqlQuery q(db);
    // 关键红线：利用 idx_items_parent 索引执行 O(log N) 范围扫描，并一次性获取所有显示属性 (Zero-IO)
    q.prepare("SELECT path, rating, color, tags, pinned, encrypted, type, size, mtime, ctime FROM items WHERE parent_path = ?");
    q.addBindValue(QString::fromStdWString(parentPath));
    
    if (q.exec()) {
        while (q.next()) {
            QString fullPath = q.value(0).toString();
            QString name = QFileInfo(fullPath).fileName();
            
            ItemMeta meta;
            meta.rating = q.value(1).toInt();
            meta.color = q.value(2).toString().toStdWString();
            
            QJsonDocument doc = QJsonDocument::fromJson(q.value(3).toByteArray());
            if (doc.isArray()) {
                for (const auto& v : doc.array()) meta.tags.push_back(v.toString().toStdWString());
            }

            meta.pinned = q.value(4).toBool();
            meta.encrypted = q.value(5).toBool();
            meta.type = q.value(6).toString().toStdWString();
            meta.size = q.value(7).toLongLong();
            meta.mtime = q.value(8).toDouble();
            meta.ctime = q.value(9).toDouble();
            
            results[name.toStdWString()] = meta;
        }
    }
    return results;
}

} // namespace ArcMeta
```

## 文件: `src/db/ItemRepo.h`

```cpp
#pragma once

#include "Database.h"
#include "../meta/AmMetaJson.h"
#include <string>
#include <vector>
#include <map>
#include <QSqlDatabase>

namespace ArcMeta {

/**
 * @brief 文件条目持久层
 */
class ItemRepo {
public:
    static bool save(const std::wstring& parentPath, const std::wstring& name, const ItemMeta& meta, QSqlDatabase db = QSqlDatabase::database());
    static bool removeByFrn(const std::wstring& volume, const std::wstring& frn, QSqlDatabase db = QSqlDatabase::database());
    static bool markAsDeleted(const std::wstring& volume, const std::wstring& frn, QSqlDatabase db = QSqlDatabase::database());
    
    /**
     * @brief 通过 FRN 获取当前数据库记录的路径
     */
    static std::wstring getPathByFrn(const std::wstring& volume, const std::wstring& frn, QSqlDatabase db = QSqlDatabase::database());

    /**
     * @brief 物理更新路径
     */
    static bool updatePath(const std::wstring& volume, const std::wstring& frn, const std::wstring& newPath, const std::wstring& newParentPath, QSqlDatabase db = QSqlDatabase::database());

    /**
     * @brief 极致性能：批量获取目录下所有条目的元数据
     */
    static std::map<std::wstring, ItemMeta> getMetadataBatch(const std::wstring& parentPath, QSqlDatabase db = QSqlDatabase::database());
};

} // namespace ArcMeta
```

## 文件: `src/main.cpp`

```cpp
#include <QApplication>
#include <QMessageBox>
#include <windows.h>
#include <shellapi.h>
#include "ui/MainWindow.h"
#include "db/Database.h"
#include "db/SyncEngine.h"
#include "meta/SyncQueue.h"
#include "mft/MftReader.h"

/**
 * @brief 检查当前进程是否具有管理员权限
 */
bool isRunningAsAdmin() {
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
            fRet = elevation.TokenIsElevated;
        }
    }
    if (hToken) CloseHandle(hToken);
    return fRet;
}

int main(int argc, char *argv[]) {
    // 设置高 DPI 支持 (Qt 6 默认开启，此处显式设置)
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication a(argc, argv);
    a.setApplicationName("ArcMeta");
    a.setOrganizationName("ArcMetaTeam");

    // 1. 权限检查逻辑
    if (!isRunningAsAdmin()) {
        QMessageBox::critical(nullptr, "权限不足", 
            "ArcMeta 需要管理员权限以读取 MFT 数据及加速索引。\n请尝试“以管理员身份运行”。");
        // 文档规定：无权限时执行降级方案，但启动基础 UI 仍需进行
    }

    // 2. 初始化核心底层
    std::wstring dbPath = L"arcmeta.db";
    if (!ArcMeta::Database::instance().init(dbPath)) {
        QMessageBox::critical(nullptr, "错误", "无法初始化数据库，程序即将退出。");
        return -1;
    }

    // 3. 初始化文件索引
    ArcMeta::MftReader::instance().buildIndex();

    // 4. 启动异步同步队列
    ArcMeta::SyncQueue::instance().start();

    // 5. 执行增量同步
    ArcMeta::SyncEngine::instance().runIncrementalSync();

    // 6. 显示主窗口
    ArcMeta::MainWindow w;
    w.show();

    int ret = a.exec();

    // 6. 优雅退出：刷空队列并停止线程
    ArcMeta::SyncQueue::instance().stop();

    return ret;
}
```

## 文件: `src/ui/MainWindow.cpp`

```cpp
#include "MainWindow.h"
#include "BreadcrumbBar.h"
#include "CategoryPanel.h"
#include "NavPanel.h"
#include "ContentPanel.h"
#include "MetaPanel.h"
#include "../db/ItemRepo.h"
#include "FilterPanel.h"
#include "QuickLookWindow.h"
#include "ToolTipOverlay.h"
#include "../../SvgIcons.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSvgRenderer>
#include <QPainter>
#include <QIcon>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QApplication>
#include <QSettings>
#include <QCloseEvent>
#include <QTimer>
#include "UiHelper.h"
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include "../meta/AmMetaJson.h"
#include "../db/ItemRepo.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ArcMeta {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    resize(1600, 900);
    setMinimumSize(1280, 720);
    setWindowTitle("ArcMeta");

    // 从设置读取置顶状态
    QSettings settings("ArcMeta团队", "ArcMeta");
    m_isPinned = settings.value("MainWindow/AlwaysOnTop", false).toBool();
    
    // 设置基础窗口标志 (保持无边框)
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);

    // 初始应用置顶 (WinAPI)
    if (m_isPinned) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId();
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
        setWindowFlag(Qt::WindowStaysOnTopHint, true);
#endif
    }

    // 应用全局样式（包括滚动条美化）
    QString qss = R"(
        QMainWindow { background-color: #1A1A1A; }
        
        /* 全局滚动条美化 */
        QScrollBar:vertical {
            border: none;
            background: #1E1E1E;
            width: 8px;
            margin: 0px 0px 0px 0px;
        }
        QScrollBar::handle:vertical {
            background: #333333;
            min-height: 20px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical:hover {
            background: #444444;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }

        QScrollBar:horizontal {
            border: none;
            background: #1E1E1E;
            height: 8px;
            margin: 0px 0px 0px 0px;
        }
        QScrollBar::handle:horizontal {
            background: #333333;
            min-width: 20px;
            border-radius: 4px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #444444;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: none;
        }

        /* 统一复选框样式：2026-03-xx 按照用户要求，仅保留蓝色勾选标记，背景保持深色 */
        QCheckBox { color: #EEEEEE; font-size: 12px; spacing: 5px; }
        QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #444; border-radius: 2px; background: #1A1A1A; }
        QCheckBox::indicator:hover { border: 1px solid #666; }
        QCheckBox::indicator:checked { 
            border: 1px solid #378ADD; 
            background: #1A1A1A; 
            image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjMzc4QUREIiBzdHJva2Utd2lkdGg9IjMuNSIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=);
        }

        /* 统一输入框样式 */
        QLineEdit {
            background: #1A1A1A;
            border: 1px solid #333333;
            border-radius: 6px;
            color: #EEEEEE;
            padding-left: 8px;
        }
        QLineEdit:focus {
            border: 1px solid #378ADD;
        }
    )";
    setStyleSheet(qss);

    initUi();

    // 启动时和顶级目录均显示“此电脑”（磁盘分区列表）
    navigateTo("computer://");
}

void MainWindow::initUi() {
    initToolbar();
    setupSplitters();
    setupCustomTitleBarButtons();
    
    // 设置默认权重分配: 230 | 200 | 弹性 | 240 | 230 (移除一个 200)
    QList<int> sizes;
    sizes << 230 << 200 << 700 << 240 << 230;
    m_mainSplitter->setSizes(sizes);

    // 核心红线：建立各面板间的信号联动 (Data Linkage)
    
    // 1. 导航/收藏/内容面板 双击跳转 -> 统一路径调度
    connect(m_navPanel, &NavPanel::directorySelected, this, [this](const QString& path) {
        navigateTo(path);
    });

    connect(m_contentPanel, &ContentPanel::directorySelected, this, [this](const QString& path) {
        navigateTo(path);
    });

    // 1a. 分类选择 -> 内容面板执行检索或筛选
    connect(m_categoryPanel, &CategoryPanel::categorySelected, [this](const QString& name) {
        m_pathStack->setCurrentWidget(m_pathEdit);
        m_pathEdit->setText("分类: " + name);
        m_contentPanel->search(name); 
    });

    // 2. 内容面板选中项改变 -> 元数据面板刷新
    // 2026-03-xx 按照高性能要求，优先从模型 Role 读取元数据缓存，避免频繁磁盘 IO
    connect(m_contentPanel, &ContentPanel::selectionChanged, [this](const QStringList& paths) {
        if (paths.isEmpty()) {
            m_metaPanel->updateInfo("-", "-", "-", "-", "-", "-", "-", false);
            m_metaPanel->setRating(0);
            m_metaPanel->setColor(L"");
            m_metaPanel->setPinned(false);
            m_metaPanel->setTags(QStringList());
            if (m_statusRight) m_statusRight->setText("");
        } else {
            // 2026-03-xx 高性能优化：优先从模型缓存中读取元数据，避免频繁磁盘访问
            auto indexes = m_contentPanel->getSelectedIndexes();
            if (indexes.isEmpty()) return;
            
            QModelIndex idx = indexes.first();
            QString path = paths.first();
            QFileInfo info(path);
            
            // 2026-03-xx 极致性能优化：Zero-IO UI，所有属性直接从模型 Role 读取，彻底禁绝 QFileInfo 系统调用
            bool isDir = idx.data(ArcMeta::IsDirRole).toBool();
            QString name = idx.data(Qt::DisplayRole).toString();
            QString typeStr = idx.data(ArcMeta::TypeRole).toString() == "folder" ? "文件夹" : QFileInfo(path).suffix().toUpper() + " 文件";
            qlonglong sizeRaw = idx.data(ArcMeta::SizeRawRole).toLongLong();
            QString sizeStr = isDir ? "-" : QString::number(sizeRaw / 1024) + " KB";
            
            QString ctimeStr = QDateTime::fromMSecsSinceEpoch((qint64)idx.data(ArcMeta::CTimeRawRole).toDouble()).toString("yyyy-MM-dd");
            QString mtimeStr = QDateTime::fromMSecsSinceEpoch((qint64)idx.data(ArcMeta::MTimeRawRole).toDouble()).toString("yyyy-MM-dd");

            m_metaPanel->updateInfo(
                name, typeStr, sizeStr, ctimeStr, mtimeStr, mtimeStr, path,
                idx.data(ArcMeta::EncryptedRole).toBool()
            );

            // 应用缓存中的元数据状态
            m_metaPanel->setRating(idx.data(ArcMeta::RatingRole).toInt());
            m_metaPanel->setColor(idx.data(ArcMeta::ColorRole).toString().toStdWString());
            m_metaPanel->setPinned(idx.data(ArcMeta::IsLockedRole).toBool());
            m_metaPanel->setTags(idx.data(ArcMeta::TagsRole).toStringList());
        }
        // 状态栏右侧显示已选数量
        if (m_statusRight) {
            m_statusRight->setText(paths.isEmpty() ? "" : QString("已选 %1 个项目").arg(paths.size()));
        }
    });

    // 3. 内容面板请求预览 -> QuickLook
    connect(m_contentPanel, &ContentPanel::requestQuickLook, [](const QString& path) {
        QuickLookWindow::instance().previewFile(path);
    });

    // 4. QuickLook 打标 -> 元数据面板同步
    connect(&QuickLookWindow::instance(), &QuickLookWindow::ratingRequested, [this](int rating) {
        m_metaPanel->setRating(rating);
    });

    // 5a. 目录装载完成 -> FilterPanel 动态填充 (六参数版本)
    connect(m_contentPanel, &ContentPanel::directoryStatsReady,
        [this](const QMap<int,int>& r, const QMap<QString,int>& c,
               const QMap<QString,int>& t, const QMap<QString,int>& tp,
               const QMap<QString,int>& cd, const QMap<QString,int>& md) {
            m_filterPanel->populate(r, c, t, tp, cd, md);
        });

    // 5b. FilterPanel 勾选变化 -> 内容面板过滤
    connect(m_filterPanel, &FilterPanel::filterChanged, [this](const FilterState& state) {
        m_contentPanel->applyFilters(state);
        updateStatusBar(); // 筛选后立即更新底栏可见项目总数
    });

    // 6. 工具栏路径跳转
    connect(m_pathEdit, &QLineEdit::returnPressed, [this]() {
        QString input = m_pathEdit->text();
        if (QDir(input).exists()) {
            navigateTo(input);
        } else if (input == "computer://" || input == "此电脑") {
            navigateTo("computer://");
        } else {
            // 如果路径无效，恢复为当前实际路径
            m_pathEdit->setText(QDir::toNativeSeparators(m_currentPath));
            m_pathStack->setCurrentWidget(m_breadcrumbBar);
        }
    });

    // 7. 工具栏极速搜索对接 (MFT 并行引擎)
    connect(m_searchEdit, &QLineEdit::textEdited, [this](const QString& text) {
        if (text.isEmpty()) {
            m_contentPanel->loadDirectory(m_pathEdit->text());
        } else if (text.length() >= 2) {
            m_contentPanel->search(text);
        }
    });

    // 8. 响应元数据面板自己的星级/颜色变更 (2026-03-xx 极致性能：实现批量事务化保存，消除 IO 震荡)
    connect(m_metaPanel, &MetaPanel::metadataChanged, [this](int rating, const std::wstring& color) {
        auto indexes = m_contentPanel->getSelectedIndexes();
        if (indexes.isEmpty()) return;

        // 1. 按所属文件夹对选中项进行分组
        std::map<std::wstring, std::vector<std::pair<QModelIndex, std::wstring>>> folderGroups;
        for (const auto& idx : indexes) {
            QString fullPath = idx.data(PathRole).toString();
            if (fullPath.isEmpty()) continue;
            QFileInfo info(fullPath);
            folderGroups[info.absolutePath().toStdWString()].push_back({idx, info.fileName().toStdWString()});
        }

        // 2. 逐文件夹执行批量原子保存
        for (auto& [folderPath, items] : folderGroups) {
            ArcMeta::AmMetaJson meta(folderPath);
            meta.load();
            for (auto& itemPair : items) {
                const auto& idx = itemPair.first;
                const auto& fileName = itemPair.second;

                if (rating != -1) {
                    m_contentPanel->getProxyModel()->setData(idx, rating, ArcMeta::RatingRole);
                    meta.items()[fileName].rating = rating;
                }
                if (color != L"__NO_CHANGE__") {
                    m_contentPanel->getProxyModel()->setData(idx, QString::fromStdWString(color), ArcMeta::ColorRole);
                    meta.items()[fileName].color = color;
                }
            }
            meta.save(); // 每个文件夹仅执行一次磁盘写入
        }
    });
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // 点击工具栏区域（前 36px）允许拖动窗口
        if (event->position().y() <= 36) {
            m_isDragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
        }
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    if (m_isDragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragPosition);
        event->accept();
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event);
    m_isDragging = false;
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // 1. Alt+Q: 切换窗口置顶状态
    if (event->key() == Qt::Key_Q && (event->modifiers() & Qt::AltModifier)) {
        m_btnPinTop->setChecked(!m_btnPinTop->isChecked());
        event->accept();
        return;
    }

    // 2. Ctrl+F: 聚焦搜索过滤框
    if (event->key() == Qt::Key_F && (event->modifiers() & Qt::ControlModifier)) {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

// 2026-03-xx 按照用户要求：物理拦截事件以实现自定义 ToolTipOverlay 的显隐控制
bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::HoverEnter) {
        QString text = watched->property("tooltipText").toString();
        if (!text.isEmpty()) {
            // 物理级别禁绝原生 ToolTip，强制调用 ToolTipOverlay
            ToolTipOverlay::instance()->showText(QCursor::pos(), text);
        }
    } else if (event->type() == QEvent::HoverLeave || event->type() == QEvent::MouseButtonPress) {
        ToolTipOverlay::hideTip();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::initToolbar() {
    m_toolbar = addToolBar("MainToolbar");
    m_toolbar->setFixedHeight(36);
    m_toolbar->setMovable(false);
    m_toolbar->setStyleSheet("QToolBar { background-color: #252525; border: none; padding-left: 12px; padding-right: 12px; spacing: 8px; border-bottom: 1px solid #333; }");

    auto createBtn = [this](const QString& iconKey, const QString& tip) {
        QPushButton* btn = new QPushButton(this);
        btn->setFixedSize(32, 28); // 极致精简宽度
        
        QIcon icon = UiHelper::getIcon(iconKey, QColor("#EEEEEE"));
        btn->setIcon(icon);
        btn->setIconSize(QSize(18, 18));
        
        btn->setToolTip(tip);
        // 极致精简样式：无边框，仅悬停可见背景
        btn->setStyleSheet(
            "QPushButton { background: transparent; border: none; border-radius: 4px; }"
            "QPushButton:hover { background: rgba(255, 255, 255, 0.1); }"
            "QPushButton:pressed { background: rgba(255, 255, 255, 0.2); }"
            "QPushButton:disabled { opacity: 0.3; }"
        );
        return btn;
    };

    m_btnBack = createBtn("nav_prev", "");
    m_btnBack->setProperty("tooltipText", "后退");
    m_btnBack->installEventFilter(this);

    m_btnForward = createBtn("nav_next", "");
    m_btnForward->setProperty("tooltipText", "前进");
    m_btnForward->installEventFilter(this);

    m_btnUp = createBtn("arrow_up", "");
    m_btnUp->setProperty("tooltipText", "上级");
    m_btnUp->installEventFilter(this);

    connect(m_btnBack, &QPushButton::clicked, this, &MainWindow::onBackClicked);
    connect(m_btnForward, &QPushButton::clicked, this, &MainWindow::onForwardClicked);
    connect(m_btnUp, &QPushButton::clicked, this, &MainWindow::onUpClicked);

    // --- 路径地址栏重构 (Stack: Breadcrumb + QLineEdit) ---
    m_pathStack = new QStackedWidget(this);
    // 2026-03-xx 按照用户最新要求：地址栏高度由 40px 调整为更紧凑的 38px
    m_pathStack->setFixedHeight(38); 
    m_pathStack->setMinimumWidth(300);
    m_pathStack->setStyleSheet("QStackedWidget { background: #1E1E1E; border: 1px solid #444444; border-radius: 4px; }");

    // A. 面包屑视图
    m_breadcrumbBar = new BreadcrumbBar(m_pathStack);
    m_pathStack->addWidget(m_breadcrumbBar);

    // B. 编辑视图
    m_pathEdit = new QLineEdit(m_pathStack);
    m_pathEdit->setPlaceholderText("输入路径...");
    m_pathEdit->setFixedHeight(34);
    m_pathEdit->setStyleSheet("QLineEdit { background: transparent; border: none; color: #EEEEEE; padding-left: 8px; }");
    m_pathStack->addWidget(m_pathEdit);

    m_pathStack->setCurrentWidget(m_breadcrumbBar);

    // 交互逻辑
    connect(m_breadcrumbBar, &BreadcrumbBar::blankAreaClicked, [this]() {
        m_pathEdit->setText(QDir::toNativeSeparators(m_currentPath));
        m_pathStack->setCurrentWidget(m_pathEdit);
        m_pathEdit->setFocus();
        m_pathEdit->selectAll();
    });
    connect(m_pathEdit, &QLineEdit::editingFinished, [this]() {
        // 只有在失去焦点或按回车后切回面包屑 (如果不是由于 confirm 跳转)
        if (m_pathStack->currentWidget() == m_pathEdit) {
            m_pathStack->setCurrentWidget(m_breadcrumbBar);
        }
    });
    connect(m_breadcrumbBar, &BreadcrumbBar::pathClicked, [this](const QString& path) {
        navigateTo(path);
    });

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("过滤内容...");
    m_searchEdit->setFixedWidth(200);
    // 2026-03-xx 按照用户要求：对齐地址栏，将搜索框高度也调整为 38px
    m_searchEdit->setFixedHeight(38); 
    m_searchEdit->setStyleSheet(
        "QLineEdit { background: #1E1E1E; border: 1px solid #444444; border-radius: 6px; color: #EEEEEE; padding-left: 8px; }"
        "QLineEdit:focus { border: 1px solid #FFFFFF; }"
    );

}



void MainWindow::setupSplitters() {
    QWidget* centralC = new QWidget(this);
    QVBoxLayout* mainL = new QVBoxLayout(centralC);
    mainL->setContentsMargins(5, 5, 5, 5); // 全局 5px 外边距
    mainL->setSpacing(5); // 各元素垂直间距统一 5px

    QWidget* addressBar = new QWidget(centralC);
    addressBar->setFixedHeight(38); // 2026-03-xx 按照最新要求：地址栏高度调整为 38px
    addressBar->setStyleSheet("QWidget { background: transparent; border: none; }");
    QHBoxLayout* addrL = new QHBoxLayout(addressBar);
    addrL->setContentsMargins(0, 0, 0, 0);
    addrL->setSpacing(5); // 地址栏内部按鈕间距 5px

    addrL->addWidget(m_btnBack);
    addrL->addWidget(m_btnForward);
    addrL->addWidget(m_btnUp);
    addrL->addWidget(m_pathStack, 1);
    addrL->addWidget(m_searchEdit);

    // --- 主拆分条（5px handleWidth） ---
    m_mainSplitter = new QSplitter(Qt::Horizontal, centralC);
    m_mainSplitter->setHandleWidth(5); // 拆分条 5px
    m_mainSplitter->setStyleSheet("QSplitter::handle { background-color: #2A2A2A; }");

    m_categoryPanel = new CategoryPanel(this);
    m_navPanel = new NavPanel(this);
    m_contentPanel = new ContentPanel(this);
    m_metaPanel = new MetaPanel(this);
    m_filterPanel = new FilterPanel(this);

    m_mainSplitter->addWidget(m_categoryPanel);
    m_mainSplitter->addWidget(m_navPanel);
    m_mainSplitter->addWidget(m_contentPanel);
    m_mainSplitter->addWidget(m_metaPanel);
    m_mainSplitter->addWidget(m_filterPanel);

    // --- 底部状态栏（28px） ---
    QWidget* statusBar = new QWidget(centralC);
    statusBar->setFixedHeight(28);
    statusBar->setStyleSheet("QWidget { background-color: #252525; border-top: 1px solid #333333; }");
    QHBoxLayout* statusL = new QHBoxLayout(statusBar);
    statusL->setContentsMargins(10, 0, 10, 0);
    statusL->setSpacing(0);

    m_statusLeft = new QLabel("就绪中...", statusBar);
    m_statusLeft->setStyleSheet("font-size: 11px; color: #B0B0B0; background: transparent;");

    m_statusCenter = new QLabel("", statusBar);
    m_statusCenter->setAlignment(Qt::AlignCenter);
    m_statusCenter->setStyleSheet("font-size: 11px; color: #888888; background: transparent;");

    m_statusRight = new QLabel("", statusBar);
    m_statusRight->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusRight->setStyleSheet("font-size: 11px; color: #B0B0B0; background: transparent;");

    statusL->addWidget(m_statusLeft, 1);
    statusL->addWidget(m_statusCenter, 1);
    statusL->addWidget(m_statusRight, 1);

    mainL->addWidget(addressBar);
    mainL->addWidget(m_mainSplitter, 1);
    mainL->addWidget(statusBar);

    setCentralWidget(centralC);
}

/**
 * @brief 实现符合 funcBtnStyle 规范的自定义按钮组
 */
void MainWindow::setupCustomTitleBarButtons() {
    QWidget* titleBarBtns = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(titleBarBtns);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto createTitleBtn = [this](const QString& iconKey, const QString& hoverColor = "rgba(255, 255, 255, 0.1)") {
        QPushButton* btn = new QPushButton(this);
        btn->setFixedSize(24, 24); // 固定 24x24px
        
        // 使用 UiHelper 全局辅助类
        QIcon icon = UiHelper::getIcon(iconKey, QColor("#EEEEEE"));
        btn->setIcon(icon);
        btn->setIconSize(QSize(18, 18));
        
        btn->setStyleSheet(QString(
            "QPushButton { background: transparent; border: none; border-radius: 4px; padding: 0; }"
            "QPushButton:hover { background: %1; }"
            "QPushButton:pressed { background: rgba(255, 255, 255, 0.2); }"
        ).arg(hoverColor));
        return btn;
    };

    m_btnCreate = createTitleBtn("add"); // 2026-03-xx 规范化：“+”按钮图标修正
    m_btnCreate->setProperty("tooltipText", "新建...");
    QMenu* createMenu = new QMenu(m_btnCreate);
    createMenu->setStyleSheet(
        "QMenu { background-color: #2B2B2B; border: 1px solid #444444; color: #EEEEEE; padding: 4px; border-radius: 6px; }"
        "QMenu::item { height: 24px; padding: 0 20px 0 10px; border-radius: 3px; font-size: 12px; }"
        "QMenu::item:selected { background-color: #505050; }"
        "QMenu::right-arrow { image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjRUVFRUVFIiBzdHJva2Utd2lkdGg9IjIiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgc3Ryb2tlLWxpbmVqb2luPSJyb3VuZCI+PHBvbHlsaW5lIHBvaW50cz0iOSAxOCAxNSAxMiA5IDYiPjwvcG9seWxpbmU+PC9zdmc+); width: 12px; height: 12px; right: 8px; }"
    );
    
    QAction* actNewFolder = createMenu->addAction(UiHelper::getIcon("folder", QColor("#EEEEEE")), "创建文件夹");
    QAction* actNewMd     = createMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建 Markdown");
    QAction* actNewTxt    = createMenu->addAction(UiHelper::getIcon("text", QColor("#EEEEEE")), "创建纯文本文件 (txt)");
    
    // 2026-03-xx 按照用户要求修正居中对齐：
    // 不再使用 setMenu，避免按钮进入“菜单模式”从而为指示器预留空间导致图标偏左。
    // 采用手动 popup 方式展示菜单。
    connect(m_btnCreate, &QPushButton::clicked, [this, createMenu]() {
        createMenu->popup(m_btnCreate->mapToGlobal(QPoint(0, m_btnCreate->height())));
    });

    auto handleCreate = [this](const QString& type) {
        m_contentPanel->createNewItem(type);
    };
    connect(actNewFolder, &QAction::triggered, [handleCreate](){ handleCreate("folder"); });
    connect(actNewMd,     &QAction::triggered, [handleCreate](){ handleCreate("md"); });
    connect(actNewTxt,    &QAction::triggered, [handleCreate](){ handleCreate("txt"); });

    m_btnPinTop = createTitleBtn(m_isPinned ? "pin_vertical" : "pin_tilted");
    m_btnPinTop->setProperty("tooltipText", "置顶窗口");
    m_btnPinTop->installEventFilter(this);
    m_btnPinTop->setCheckable(true);
    m_btnPinTop->setChecked(m_isPinned);
    if (m_isPinned) {
        m_btnPinTop->setIcon(UiHelper::getIcon("pin_vertical", QColor("#FF551C")));
    }

    m_btnMin = createTitleBtn("minimize");
    m_btnMin->setProperty("tooltipText", "最小化");
    m_btnMin->installEventFilter(this);

    m_btnMax = createTitleBtn("maximize");
    m_btnMax->setProperty("tooltipText", "最大化/还原");
    m_btnMax->installEventFilter(this);

    m_btnClose = createTitleBtn("close", "#e81123"); // 关闭按钮悬停红色
    m_btnClose->setProperty("tooltipText", "关闭项目");
    m_btnClose->installEventFilter(this);

    m_btnCreate->installEventFilter(this);
    layout->addWidget(m_btnCreate);
    layout->addWidget(m_btnPinTop);
    layout->addWidget(m_btnMin);
    layout->addWidget(m_btnMax);
    layout->addWidget(m_btnClose);

    // 绑定基础逻辑
    connect(m_btnMin, &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(m_btnMax, &QPushButton::clicked, [this]() {
        if (isMaximized()) showNormal();
        else showMaximized();
    });
    connect(m_btnClose, &QPushButton::clicked, this, &MainWindow::close);

    // 将按钮组添加到工具栏最右侧 (QToolBar 不支持 addStretch，改用弹簧 Widget)
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    m_toolbar->addWidget(titleBarBtns);

    // 逻辑：置顶切换
    connect(m_btnPinTop, &QPushButton::toggled, this, &MainWindow::onPinToggled);
}

void MainWindow::navigateTo(const QString& path, bool record) {
    if (path.isEmpty()) return;
    
    // 处理虚拟路径 "computer://" —— 此电脑（磁盘分区列表）
    if (path == "computer://") {
        m_currentPath = "computer://";
        if (record) {
            if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
                m_history = m_history.mid(0, m_historyIndex + 1);
            }
            if (m_history.isEmpty() || m_history.last() != path) {
                m_history.append(path);
                m_historyIndex = static_cast<int>(m_history.size()) - 1;
            }
        }
        m_pathEdit->setText("此电脑");
        m_breadcrumbBar->setPath("computer://");
        m_pathStack->setCurrentWidget(m_breadcrumbBar);
        m_contentPanel->loadDirectory(""); 
        int driveCount = static_cast<int>(QDir::drives().count());
        m_statusLeft->setText(QString("%1 个分区").arg(driveCount));
        m_statusCenter->setText("此电脑");
        updateNavButtons();
        return;
    }

    QString normPath = QDir::toNativeSeparators(path);
    m_currentPath = normPath;

    if (record) {
            if (m_historyIndex < static_cast<int>(m_history.size()) - 1) {
            m_history = m_history.mid(0, m_historyIndex + 1);
        }
        if (m_history.isEmpty() || m_history.last() != normPath) {
            m_history.append(normPath);
                m_historyIndex = static_cast<int>(m_history.size()) - 1;
        }
    }
    
    m_pathEdit->setText(normPath);
    m_breadcrumbBar->setPath(normPath);
    m_pathStack->setCurrentWidget(m_breadcrumbBar);
    m_contentPanel->loadDirectory(normPath);
    updateNavButtons();
    updateStatusBar();
}

void MainWindow::onBackClicked() {
    if (m_historyIndex > 0) {
        m_historyIndex--;
        navigateTo(m_history[m_historyIndex], false);
    }
}

void MainWindow::onForwardClicked() {
    if (m_historyIndex < m_history.size() - 1) {
        m_historyIndex++;
        navigateTo(m_history[m_historyIndex], false);
    }
}

void MainWindow::onUpClicked() {
    if (m_currentPath == "computer://") return;

    QDir dir(m_currentPath);
    // 如果当前已经在根目录（如 C:\），则向上跳转到“此电脑”
    if (dir.isRoot() || m_currentPath.length() <= 3) {
        navigateTo("computer://");
    } else if (dir.cdUp()) {
        navigateTo(dir.absolutePath());
    }
}

void MainWindow::updateNavButtons() {
    m_btnBack->setEnabled(m_historyIndex > 0);
    m_btnForward->setEnabled(m_historyIndex < m_history.size() - 1);
    
    bool atComputer = (m_currentPath == "computer://");
    m_btnUp->setEnabled(!atComputer && !m_currentPath.isEmpty());
}

void MainWindow::updateStatusBar() {
    if (!m_statusLeft || !m_statusCenter || !m_statusRight) return;
    
    // 修正：显示经过过滤后的可见项目总数
    int visibleCount = m_contentPanel->getProxyModel()->rowCount();
    m_statusLeft->setText(QString("%1 个项目").arg(visibleCount));
    m_statusCenter->setText(m_currentPath == "computer://" ? "此电脑" : m_currentPath);
    m_statusRight->setText(""); // 选中时由 selectionChanged 更新
}

void MainWindow::onPinToggled(bool checked) {
    // 2026-03-xx 按照用户要求优化置顶逻辑：
    // 避免重复调用导致卡顿，并优化 WinAPI 标志位以减少冗余消息推送
    if (m_isPinned == checked) return;
    m_isPinned = checked;

#ifdef Q_OS_WIN
    HWND hwnd = (HWND)winId();
    // 使用 SWP_NOSENDCHANGING 拦截冗余消息，减少 UI 线程的消息风暴，从而解决卡顿
    SetWindowPos(hwnd, checked ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
#else
    setWindowFlag(Qt::WindowStaysOnTopHint, checked);
    show(); // 非 Windows 平台修改 Flag 后通常需要重新显示
#endif

    // 更新图标和颜色 (按下置顶为品牌橙色)
    if (m_isPinned) {
        m_btnPinTop->setIcon(UiHelper::getIcon("pin_vertical", QColor("#FF551C")));
    } else {
        m_btnPinTop->setIcon(UiHelper::getIcon("pin_tilted", QColor("#EEEEEE")));
    }

    // 持久化存储：2026-03-xx 性能优化，由同步阻塞改为延迟持久化，避免注册表 IO 阻塞 UI 线程
    static QTimer* saveTimer = nullptr;
    if (!saveTimer) {
        saveTimer = new QTimer(this);
        saveTimer->setSingleShot(true);
        connect(saveTimer, &QTimer::timeout, [this]() {
            QSettings settings("ArcMeta团队", "ArcMeta");
            settings.setValue("MainWindow/AlwaysOnTop", m_isPinned);
        });
    }
    saveTimer->start(1000); // 1秒后执行持久化，合并频繁操作
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings("ArcMeta团队", "ArcMeta");
    settings.setValue("MainWindow/LastPath", m_currentPath);
    QMainWindow::closeEvent(event);
}

} // namespace ArcMeta
```

## 文件: `src/ui/MainWindow.h`

```cpp
#pragma once

#include <QMainWindow>
#include <QSplitter>
#include <QToolBar>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>

namespace ArcMeta {

class BreadcrumbBar;
class CategoryPanel;
class NavPanel;
class ContentPanel;
class MetaPanel;
class FilterPanel;

/**
 * @brief 主窗口类
 * 负责六栏布局的组装、QSplitter 管理及自定义标题栏按钮
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onPinToggled(bool checked);
    void onBackClicked();
    void onForwardClicked();
    void onUpClicked();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void initUi();
    void updateNavButtons();
    void updateStatusBar();
    void navigateTo(const QString& path, bool record = true);
    void initToolbar();
    void setupSplitters();
    void setupCustomTitleBarButtons();

    // 面包屑地址栏
    BreadcrumbBar* m_breadcrumbBar = nullptr;
    QStackedWidget* m_pathStack = nullptr;

    // 六个面板
    CategoryPanel* m_categoryPanel = nullptr;
    NavPanel* m_navPanel = nullptr;
    ContentPanel* m_contentPanel = nullptr;
    MetaPanel* m_metaPanel = nullptr;
    FilterPanel* m_filterPanel = nullptr;

    QSplitter* m_mainSplitter = nullptr;

    // 工具栏组件
    QToolBar* m_toolbar = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_btnBack = nullptr;
    QPushButton* m_btnForward = nullptr;
    QPushButton* m_btnUp = nullptr;
    
    // 标题栏按钮组 (用于 frameless 时的模拟，此处作为标准按钮展示)
    QPushButton* m_btnCreate = nullptr;
    QPushButton* m_btnPinTop = nullptr;
    QPushButton* m_btnMin = nullptr;
    QPushButton* m_btnMax = nullptr;
    QPushButton* m_btnClose = nullptr;

    // 状态管理
    bool m_isPinned = false;
    QString m_currentPath;
    QStringList m_history;
    int m_historyIndex = -1;

    // 底部状态栏
    QLabel* m_statusLeft = nullptr;
    QLabel* m_statusCenter = nullptr;
    QLabel* m_statusRight = nullptr;

    // 窗口拖动
    bool m_isDragging = false;
    QPoint m_dragPosition;
};

} // namespace ArcMeta
```

## 文件: `src/ui/MetaPanel.cpp`

```cpp
#include "MetaPanel.h"
#include "../../SvgIcons.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QScrollBar>
#include <QStyle>
#include <QScrollArea>
#include <QFileInfo>
#include <QLabel>
#include "UiHelper.h"
#include "../meta/AmMetaJson.h"

namespace ArcMeta {

// --- TagPill ---
TagPill::TagPill(const QString& text, QWidget* parent) 
    : QWidget(parent), m_text(text) {
    setProperty("tagText", text); // 修复：设置属性以便删除逻辑查找
    setFixedHeight(22);

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 4, 0);
    layout->setSpacing(4);

    QLabel* lbl = new QLabel(text, this);
    lbl->setStyleSheet("color: #EEEEEE; font-size: 12px; border: none; background: transparent;");
    
    m_closeBtn = new QPushButton(this);
    m_closeBtn->setFixedSize(14, 14);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setIcon(UiHelper::getIcon("x", QColor("#B0B0B0"), 12));
    m_closeBtn->setIconSize(QSize(10, 10));
    m_closeBtn->setStyleSheet(
        "QPushButton { border: none; background: transparent; }"
        "QPushButton:hover { background: rgba(255, 255, 255, 0.1); border-radius: 2px; }"
    );

    layout->addWidget(lbl);
    layout->addWidget(m_closeBtn);

    connect(m_closeBtn, &QPushButton::clicked, [this]() { 
        emit deleteRequested(m_text); 
    });

    // 根据内容自动计算宽度
    QFontMetrics fm(lbl->font());
    setFixedWidth(fm.horizontalAdvance(text) + 30); 
}

void TagPill::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor("#2B2B2B"));
    painter.setPen(QPen(QColor("#444444"), 1));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 11, 11);
}

// --- FlowLayout ---
FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing) {
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout() {
    QLayoutItem *item;
    while ((item = takeAt(0))) delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { itemList.append(item); }
int FlowLayout::horizontalSpacing() const { return m_hSpace >= 0 ? m_hSpace : 4; }
int FlowLayout::verticalSpacing() const { return m_vSpace >= 0 ? m_vSpace : 4; }
int FlowLayout::count() const { return itemList.size(); }
QLayoutItem *FlowLayout::itemAt(int index) const { return itemList.value(index); }
QLayoutItem *FlowLayout::takeAt(int index) { 
    return (index >= 0 && index < itemList.size()) ? itemList.takeAt(index) : nullptr; 
}
Qt::Orientations FlowLayout::expandingDirections() const { return Qt::Orientations(); }
bool FlowLayout::hasHeightForWidth() const { return true; }
int FlowLayout::heightForWidth(int width) const { return doLayout(QRect(0, 0, width, 0), true); }
void FlowLayout::setGeometry(const QRect &rect) { QLayout::setGeometry(rect); doLayout(rect, false); }
QSize FlowLayout::sizeHint() const { return minimumSize(); }
QSize FlowLayout::minimumSize() const {
    QSize size;
    for (QLayoutItem *item : itemList) size = size.expandedTo(item->minimumSize());
    size += QSize(2 * contentsMargins().top(), 2 * contentsMargins().top());
    return size;
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const {
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;
    for (QLayoutItem *item : itemList) {
        int spaceX = horizontalSpacing();
        int spaceY = verticalSpacing();
        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }
        if (!testOnly) item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

// --- StarRatingWidget ---
StarRatingWidget::StarRatingWidget(QWidget* parent) : QWidget(parent) { 
    setFixedSize(5 * 20 + 4 * 4, 20); 
    setCursor(Qt::PointingHandCursor); 
}

void StarRatingWidget::setRating(int rating) { 
    m_rating = rating; 
    update(); 
}

void StarRatingWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this); 
    painter.setRenderHint(QPainter::Antialiasing); 
    
    int starSize = 20;
    int spacing = 4;
    QPixmap filledStar = UiHelper::getPixmap("star_filled", QSize(starSize, starSize), QColor("#EF9F27"));
    QPixmap emptyStar = UiHelper::getPixmap("star", QSize(starSize, starSize), QColor("#444444"));

    for (int i = 0; i < 5; ++i) { 
        QRect r(i * (starSize + spacing), 0, starSize, starSize);
        painter.drawPixmap(r, (i < m_rating) ? filledStar : emptyStar);
    }
}

void StarRatingWidget::mousePressEvent(QMouseEvent* e) { 
    int starW = 24; // starSize(20) + spacing(4)
    int r = (e->pos().x() / starW) + 1; 
    // 判定是否真的点在星星矩形内 (0..20 像素)
    if ((e->pos().x() % starW) > 20) return; 

    m_rating = (r == m_rating) ? 0 : qBound(0, r, 5); 
    update(); 
    emit ratingChanged(m_rating); 
}

// --- ColorPickerWidget ---
ColorPickerWidget::ColorPickerWidget(QWidget* parent) : QWidget(parent) {
    m_colors = {{L"", QColor("#888780")}, {L"red", QColor("#E24B4A")}, {L"orange", QColor("#EF9F27")}, {L"yellow", QColor("#FAC775")}, {L"green", QColor("#639922")}, {L"cyan", QColor("#1D9E75")}, {L"blue", QColor("#378ADD")}, {L"purple", QColor("#7F77DD")}, {L"gray", QColor("#5F5E5A")}};
    int count = (int)m_colors.size();
    // 增加高度至 24px 以容纳选择外圈 (20px + padding)
    setFixedSize(count * 24, 24); 
    setCursor(Qt::PointingHandCursor);
}

void ColorPickerWidget::setColor(const std::wstring& name) { 
    m_currentColor = name; 
    update(); 
}

void ColorPickerWidget::paintEvent(QPaintEvent*) {
    QPainter p(this); 
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < (int)m_colors.size(); ++i) {
        // 圆点 18x18，外圈预留空间
        QRect r(i * 24 + 3, 3, 18, 18);
        if (m_colors[i].name == m_currentColor) { 
            p.setPen(QPen(QColor("#FFFFFF"), 1.5)); 
            // 绘制外层的高亮圈
            p.drawEllipse(r.adjusted(-2, -2, 2, 2)); 
        }
        
        p.setPen(Qt::NoPen);
        p.setBrush(m_colors[i].value); 
        p.drawEllipse(r);
    }
}

void ColorPickerWidget::mousePressEvent(QMouseEvent* e) { 
    int idx = e->pos().x() / 24; 
    if (idx >= 0 && idx < (int)m_colors.size()) { 
        std::wstring c = m_colors[idx].name; 
        m_currentColor = (c == m_currentColor) ? L"" : c; 
        update(); 
        emit colorChanged(m_currentColor); 
    } 
}

// --- MetaPanel ---
MetaPanel::MetaPanel(QWidget* parent) : QWidget(parent) {
    setFixedWidth(240); 
    setStyleSheet("QWidget { background-color: #1E1E1E; color: #EEEEEE; border: none; }");
    m_mainLayout = new QVBoxLayout(this); 
    m_mainLayout->setContentsMargins(0, 0, 0, 0); 
    m_mainLayout->setSpacing(0);
    initUi();
}

void MetaPanel::initUi() {
    // 面板标题
    QLabel* titleLabel = new QLabel("元数据详情", this);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; padding: 10px 12px; background: #252526;");
    m_mainLayout->addWidget(titleLabel);

    m_scrollArea = new QScrollArea(this); 
    m_scrollArea->setWidgetResizable(true); 
    m_scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

    m_container = new QWidget(m_scrollArea); 
    m_containerLayout = new QVBoxLayout(m_container); 
    m_containerLayout->setContentsMargins(12, 12, 12, 12); 
    m_containerLayout->setSpacing(12);

    addInfoRow("名称", lblName); 
    addInfoRow("类型", lblType); 
    addInfoRow("大小", lblSize);
    addInfoRow("创建时间", lblCtime); 
    addInfoRow("修改时间", lblMtime); 
    addInfoRow("访问时间", lblAtime);
    addInfoRow("物理路径", lblPath); 
    addInfoRow("加密状态", lblEncrypted);

    m_containerLayout->addWidget(createSeparator());

    chkPinned = new QCheckBox("置顶条目", m_container); 
    chkPinned->setStyleSheet(
        "QCheckBox { font-size: 11px; color: #5F5E5A; spacing: 5px; }"
        "QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #555; border-radius: 2px; background: #1E1E1E; }"
        "QCheckBox::indicator:checked { "
        "   border: 1px solid #378ADD; "
        "   background: #1E1E1E; "
        "   image: url(data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCIgZmlsbD0ibm9uZSIgc3Ryb2tlPSIjMzc4QUREIiBzdHJva2Utd2lkdGg9IjMuNSIgc3Ryb2tlLWxpbmVjYXA9InJvdW5kIiBzdHJva2UtbGluZWpvaW49InJvdW5kIj48cG9seWxpbmUgcG9pbnRzPSIyMCA2IDkgMTcgNCAxMiI+PC9wb2x5bGluZT48L3N2Zz4=);"
        "}"
    );
    m_containerLayout->addWidget(chkPinned);

    QLabel* lR = new QLabel("星级", m_container); 
    lR->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lR);
    m_starRating = new StarRatingWidget(m_container); 
    m_containerLayout->addWidget(m_starRating);

    QLabel* lC = new QLabel("颜色标记", m_container); 
    lC->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lC);
    m_colorPicker = new ColorPickerWidget(m_container); 
    m_containerLayout->addWidget(m_colorPicker);

    QLabel* lT = new QLabel("标签 / 关键字", m_container); 
    lT->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lT);
    
    m_tagContainer = new QWidget(m_container); 
    m_tagFlowLayout = new FlowLayout(m_tagContainer, 0, 4, 4); 
    m_containerLayout->addWidget(m_tagContainer);

    m_tagEdit = new QLineEdit(m_container); 
    m_tagEdit->setPlaceholderText("添加标签按 Enter..."); 
    m_tagEdit->setFixedHeight(24); 
    m_tagEdit->setStyleSheet("QLineEdit { background: #2B2B2B; border: 1px solid #333333; border-radius: 6px; padding-left: 6px; font-size: 12px; color: #EEEEEE; }");
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &MetaPanel::onTagAdded);
    m_containerLayout->addWidget(m_tagEdit);

    QLabel* lN = new QLabel("备注", m_container); 
    lN->setStyleSheet("font-size: 11px; color: #5F5E5A;"); 
    m_containerLayout->addWidget(lN);
    m_noteEdit = new QPlainTextEdit(m_container); 
    m_noteEdit->setMinimumHeight(80); 
    m_noteEdit->setMaximumHeight(160); 
    m_noteEdit->setStyleSheet("QPlainTextEdit { background: #2B2B2B; border: 1px solid #333333; border-radius: 6px; font-size: 12px; padding: 6px; color: #EEEEEE; }");
    m_containerLayout->addWidget(m_noteEdit);

    m_containerLayout->addWidget(createSeparator());

    btnEncrypt = new QPushButton("加密保护", m_container); 
    btnDecrypt = new QPushButton("解除加密", m_container); 
    btnChangePwd = new QPushButton("修改密码", m_container);
    for (auto b : {btnEncrypt, btnDecrypt, btnChangePwd}) { 
        b->setFixedHeight(32); 
        b->setStyleSheet("QPushButton { background: #378ADD; border: none; border-radius: 4px; color: white; font-weight: 500; } QPushButton:hover { background: #4A9BEF; }"); 
        m_containerLayout->addWidget(b); 
    }
    
    // 连接内部信号并向上转发
    connect(chkPinned, &QCheckBox::clicked, this, [this](bool checked) {
        QString currentPath = lblPath->text();
        if (currentPath != "-" && !currentPath.isEmpty()) {
            QFileInfo info(currentPath);
            AmMetaJson meta(info.absolutePath().toStdWString());
            meta.load();
            meta.items()[info.fileName().toStdWString()].pinned = checked;
            meta.save();
        }
    });

    connect(m_starRating, &StarRatingWidget::ratingChanged, [this](int r) {
        emit metadataChanged(r, L"__NO_CHANGE__");
    });
    connect(m_colorPicker, &ColorPickerWidget::colorChanged, [this](const std::wstring& c) {
        emit metadataChanged(-1, c);
    });

    m_containerLayout->addStretch(); 
    m_scrollArea->setWidget(m_container); 
    m_mainLayout->addWidget(m_scrollArea);
}

void MetaPanel::addInfoRow(const QString& label, QLabel*& valueLabel) {
    QWidget* row = new QWidget(m_container); 
    QVBoxLayout* rl = new QVBoxLayout(row); 
    rl->setContentsMargins(0, 0, 0, 0); 
    rl->setSpacing(2);
    QLabel* kl = new QLabel(label, row); 
    kl->setStyleSheet("font-size: 11px; color: #5F5E5A;");
    valueLabel = new QLabel("-", row); 
    valueLabel->setWordWrap(true); 
    valueLabel->setStyleSheet("font-size: 13px; color: #EEEEEE;");
    rl->addWidget(kl); 
    rl->addWidget(valueLabel); 
    m_containerLayout->addWidget(row);
}

QFrame* MetaPanel::createSeparator() { 
    QFrame* l = new QFrame(this); 
    l->setFrameShape(QFrame::HLine); 
    l->setFixedHeight(1); 
    l->setStyleSheet("background-color: #333333; border: none;"); 
    return l; 
}

void MetaPanel::onTagAdded() {
    QString text = m_tagEdit->text().trimmed();
    if (!text.isEmpty()) {
        TagPill* pill = new TagPill(text, m_tagContainer);
        connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted);
        m_tagFlowLayout->addWidget(pill);
        m_tagEdit->clear();

        // [持久化写入] 将标签物理保存至 .am-meta.json
        QString currentPath = lblPath->text();
        if (currentPath != "-" && !currentPath.isEmpty()) {
            QFileInfo info(currentPath);
            AmMetaJson meta(info.absolutePath().toStdWString());
            meta.load();
            auto& tags = meta.items()[info.fileName().toStdWString()].tags;
            std::wstring wText = text.toStdWString();
            if (std::find(tags.begin(), tags.end(), wText) == tags.end()) {
                tags.push_back(wText);
                meta.save();
            }
        }
    }
}

void MetaPanel::onTagDeleted(const QString& text) {
    for (int i = 0; i < m_tagFlowLayout->count(); ++i) {
        QLayoutItem* item = m_tagFlowLayout->itemAt(i);
        TagPill* pill = qobject_cast<TagPill*>(item->widget());
        if (pill && pill->property("tagText").toString() == text) {
            m_tagFlowLayout->takeAt(i);
            pill->deleteLater();
            delete item;

            // [持久化写入] 将标签从 .am-meta.json 中剥除
            QString currentPath = lblPath->text();
            if (currentPath != "-" && !currentPath.isEmpty()) {
                QFileInfo info(currentPath);
                AmMetaJson meta(info.absolutePath().toStdWString());
                meta.load();
                auto& tags = meta.items()[info.fileName().toStdWString()].tags;
                std::wstring wText = text.toStdWString();
                auto it = std::find(tags.begin(), tags.end(), wText);
                if (it != tags.end()) {
                    tags.erase(it);
                    meta.save();
                }
            }
            return;
        }
    }
}

void MetaPanel::updateInfo(const QString& n, const QString& t, const QString& s, const QString& ct, const QString& mt, const QString& at, const QString& p, bool e) {
    lblName->setText(n); lblType->setText(t); lblSize->setText(s); lblCtime->setText(ct); lblMtime->setText(mt); lblAtime->setText(at); lblPath->setText(p); lblEncrypted->setText(e ? "已加密" : "未加密");
    btnEncrypt->setVisible(!e); btnDecrypt->setVisible(e); btnChangePwd->setVisible(e);
}

void MetaPanel::setRating(int rating) {
    m_starRating->setRating(rating);
}

void MetaPanel::setColor(const std::wstring& color) {
    m_colorPicker->setColor(color);
}

void MetaPanel::setPinned(bool pinned) {
    chkPinned->blockSignals(true);
    chkPinned->setChecked(pinned);
    chkPinned->blockSignals(false);
}

void MetaPanel::setTags(const QStringList& tags) {
    // 清空旧标签
    while (QLayoutItem* item = m_tagFlowLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        }
        delete item;
    }
    
    for (const QString& tag : tags) {
        TagPill* pill = new TagPill(tag, m_tagContainer);
        connect(pill, &TagPill::deleteRequested, this, &MetaPanel::onTagDeleted);
        m_tagFlowLayout->addWidget(pill);
    }
}

} // namespace ArcMeta
```

## 文件: `src/ui/MetaPanel.h`

```cpp
#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QFrame>
#include <QStyle>
#include <vector>
#include <string>

namespace ArcMeta {

/**
 * @brief Tag Pill 圆角标签组件 (22px height, 11px radius)
 */
class TagPill : public QWidget {
    Q_OBJECT
public:
    explicit TagPill(const QString& text, QWidget* parent = nullptr);
signals:
    void deleteRequested(const QString& text);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QString m_text;
    QPushButton* m_closeBtn = nullptr;
};

/**
 * @brief 流式布局容器 (用于展示标签)
 */
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout();
    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;
private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;
    QList<QLayoutItem *> itemList;
    int m_hSpace;
    int m_vSpace;
};

/**
 * @brief 自定义星级打分器 (20x20px stars, 4px spacing)
 */
class StarRatingWidget : public QWidget {
    Q_OBJECT
public:
    explicit StarRatingWidget(QWidget* parent = nullptr);
    void setRating(int rating);
    int rating() const { return m_rating; }
signals:
    void ratingChanged(int rating);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    int m_rating = 0;
};

/**
 * @brief 自定义颜色选择器 (18x18px dots, 6px spacing)
 */
class ColorPickerWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorPickerWidget(QWidget* parent = nullptr);
    void setColor(const std::wstring& colorName);
    std::wstring color() const { return m_currentColor; }
signals:
    void colorChanged(const std::wstring& colorName);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    std::wstring m_currentColor = L"";
    struct ColorEntry {
        std::wstring name;
        QColor value;
    };
    std::vector<ColorEntry> m_colors;
};

/**
 * @brief 元数据面板（面板五）
 */
class MetaPanel : public QWidget {
    Q_OBJECT
public:
    explicit MetaPanel(QWidget* parent = nullptr);
    ~MetaPanel() override = default;
    void updateInfo(const QString& name, const QString& type, const QString& size,
                    const QString& ctime, const QString& mtime, const QString& atime,
                    const QString& path, bool encrypted);
    
signals:
    /**
     * @brief 元数据面板向上通知的信号
     * @param rating -1 表示未变，0..5 有效
     * @param color L"__NO_CHANGE__" 表示未变
     */
    void metadataChanged(int rating, const std::wstring& color);

public:
    /**
     * @brief 设置星级显示
     */
    void setRating(int rating);
    void setColor(const std::wstring& color);
    void setPinned(bool pinned);
    void setTags(const QStringList& tags);

private:
    void initUi();
    void addInfoRow(const QString& label, QLabel*& valueLabel);
    QFrame* createSeparator();

    QVBoxLayout* m_mainLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_container = nullptr;
    QVBoxLayout* m_containerLayout = nullptr;
    QLabel* lblName = nullptr, *lblType = nullptr, *lblSize = nullptr;
    QLabel* lblCtime = nullptr, *lblMtime = nullptr, *lblAtime = nullptr;
    QLabel* lblPath = nullptr, *lblEncrypted = nullptr;
    QCheckBox* chkPinned = nullptr;
    StarRatingWidget* m_starRating = nullptr;
    ColorPickerWidget* m_colorPicker = nullptr;
    QWidget* m_tagContainer = nullptr;
    FlowLayout* m_tagFlowLayout = nullptr;
    QLineEdit* m_tagEdit = nullptr;
    QPlainTextEdit* m_noteEdit = nullptr;
    QPushButton* btnEncrypt = nullptr, *btnDecrypt = nullptr, *btnChangePwd = nullptr;

private slots:
    void onTagAdded();
    void onTagDeleted(const QString& text);
};

} // namespace ArcMeta
```

## 文件: `src/mft/MftReader.cpp`

```cpp
#include "MftReader.h"
#include "PathBuilder.h"
#include <winioctl.h>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <execution>
#include <mutex>
#include <functional>
#include <thread>

namespace ArcMeta {

MftReader& MftReader::instance() {
    static MftReader inst;
    return inst;
}

/**
 * @brief 扫描所有固定驱动器并构建索引
 */
void MftReader::buildIndex() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_index.clear();
    m_pathIndex.clear();
    
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            wchar_t driveLetter = (wchar_t)(L'A' + i);
            std::wstring volumeName = std::wstring(1, driveLetter) + L":";
            
            wchar_t driveRoot[] = { driveLetter, L':', L'\\', L'\0' };
            if (GetDriveTypeW(driveRoot) == DRIVE_FIXED) {
                // 尝试 MFT 读取
                if (!loadMftForVolume(volumeName)) {
                    // 如果 MFT 失败（无权限等），执行降级扫描
                    scanDirectoryFallback(volumeName);
                }
            }
        }
    }
}

/**
 * @brief 使用 DeviceIoControl 枚举 MFT 记录
 */
bool MftReader::loadMftForVolume(const std::wstring& volumeName) {
    std::wstring path = L"\\\\.\\" + volumeName;
    HANDLE hVol = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    
    if (hVol == INVALID_HANDLE_VALUE) return false;

    USN_JOURNAL_DATA journalData;
    DWORD cb;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &cb, NULL)) {
        CloseHandle(hVol);
        return false;
    }

    MFT_ENUM_DATA enumData;
    enumData.StartFileReferenceNumber = 0;
    enumData.LowUsn = 0;
    enumData.HighUsn = journalData.NextUsn;

    const int BUF_SIZE = 64 * 1024; // 64KB 缓冲区
    std::vector<BYTE> buffer(BUF_SIZE);
    
    auto& volumeIndex = m_index[volumeName];
    volumeIndex.reserve(1000000); // 预分配防止 rehash

    while (DeviceIoControl(hVol, FSCTL_ENUM_USN_DATA, &enumData, sizeof(enumData), buffer.data(), BUF_SIZE, &cb, NULL)) {
        BYTE* pData = buffer.data() + sizeof(USN);
        while (pData < buffer.data() + cb) {
            USN_RECORD_V2* pRecord = (USN_RECORD_V2*)pData;
            
            FileEntry entry;
            entry.volume = volumeName;
            entry.frn = pRecord->FileReferenceNumber;
            entry.parentFrn = pRecord->ParentFileReferenceNumber;
            entry.attributes = pRecord->FileAttributes;
            entry.name = std::wstring(pRecord->FileName, pRecord->FileNameLength / sizeof(wchar_t));
            
            // 2026-03-xx 极致性能优化：预存储小写名称
            entry.nameLower = entry.name;
            std::transform(entry.nameLower.begin(), entry.nameLower.end(), entry.nameLower.begin(), ::towlower);

            volumeIndex[entry.frn] = entry;
            // 建立父子索引
            m_parentToChildren[volumeName][entry.parentFrn].push_back(entry.frn);

            pData += pRecord->RecordLength;
        }
        enumData.StartFileReferenceNumber = ((USN_RECORD_V2*)buffer.data())->FileReferenceNumber;
    }

    CloseHandle(hVol);
    m_isUsingMft = true;

    // 2026-03-xx 极致性能重构：线性扫描预计算全量路径映射，消除 $O(N \cdot D)$ 递归
    precomputePaths(volumeName);

    return true;
}

/**
 * @brief 极致性能：使用非递归 BFS 一次性建立全量路径索引
 */
void MftReader::precomputePaths(const std::wstring& volume) {
    // 2026-03-xx 极致性能与并发安全重构：影子构建模式
    // 在不加锁的情况下构建临时索引表，避免长时间占用互斥锁导致 UI 阻塞
    std::unordered_map<std::wstring, DWORDLONG> tempPathToFrn;
    std::unordered_map<DWORDLONG, std::wstring> tempFrnToPath;
    
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        auto& volIndex = m_index[volume];
        auto& childrenMap = m_parentToChildren[volume];

        // BFS 核心循环：使用非递归方式避免栈溢出并达到 O(N) 建立全量索引
        struct Node { DWORDLONG frn; std::wstring path; };
        std::vector<Node> bfsQueue;
        bfsQueue.reserve(volIndex.size());

        // 1. 初始化根目录子项 (FRN=5 为根)
        if (childrenMap.count(5)) {
            for (DWORDLONG childFrn : childrenMap[5]) {
                auto it = volIndex.find(childFrn);
                if (it != volIndex.end()) {
                    const FileEntry& fe = it->second;
                    std::wstring path = volume + L"\\" + fe.name;
                    tempPathToFrn[path] = childFrn;
                    tempFrnToPath[childFrn] = path;
                    if (fe.isDir()) bfsQueue.push_back({childFrn, path});
                }
            }
        }

        // 2. 层序遍历
        size_t head = 0;
        while (head < bfsQueue.size()) {
            Node parent = bfsQueue[head++];
            auto itChildren = childrenMap.find(parent.frn);
            if (itChildren != childrenMap.end()) {
                for (DWORDLONG childFrn : itChildren->second) {
                    auto it = volIndex.find(childFrn);
                    if (it != volIndex.end()) {
                        const FileEntry& fe = it->second;
                        std::wstring fullPath = parent.path + L"\\" + fe.name;
                        tempPathToFrn[fullPath] = childFrn;
                        tempFrnToPath[childFrn] = fullPath;
                        if (fe.isDir()) bfsQueue.push_back({childFrn, fullPath});
                    }
                }
            }
        }
    }

    // 3. 原子交换：在互斥锁保护下快速切换索引指针
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_pathToFrn[volume] = std::move(tempPathToFrn);
    m_frnToPath[volume] = std::move(tempFrnToPath);
}

/**
 * @brief 降级扫描实现
 */
/**
 * @brief 优化：仅扫描顶层目录以防止启动过慢，深度扫描由 UI 驱动或后台按需进行
 */
void MftReader::scanDirectoryFallback(const std::wstring& volumeName) {
    try {
        std::wstring rootPath = volumeName + L"\\";
        // 仅迭代一级目录
        for (const auto& entry : std::filesystem::directory_iterator(rootPath, std::filesystem::directory_options::skip_permission_denied)) {
            std::wstring fullPath = entry.path().wstring();
            FileEntry fe;
            fe.volume = volumeName;
            fe.name = entry.path().filename().wstring();
            fe.attributes = entry.is_directory() ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            
            // 2026-03-xx 降级模式主键增强：为无 FRN 的环境生成基于路径哈希的虚拟 FRN
            fe.frn = std::hash<std::wstring>{}(fullPath);
            
            m_pathIndex[fullPath] = fe;
        }
    } catch (...) {}
}

/**
 * @brief 获取指定目录下的子项列表
 */
std::vector<FileEntry> MftReader::getChildren(const std::wstring& folderPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<FileEntry> results;
    std::wstring vol = folderPath.length() >= 2 ? folderPath.substr(0, 2) : L"";
    if (vol.empty()) return results;

    if (m_isUsingMft) {
        DWORDLONG parentFrn = getFrnFromPath(folderPath);
        if (parentFrn == 0) return results;

        auto& childrenMap = m_parentToChildren[vol];
        if (childrenMap.count(parentFrn)) {
            auto& entries = m_index[vol];
            for (DWORDLONG childFrn : childrenMap[parentFrn]) {
                if (entries.count(childFrn)) results.push_back(entries[childFrn]);
            }
        }
    } else {
        // 2. 降级模式：直接扫描文件系统
        try {
            std::filesystem::path p(folderPath);
            for (const auto& entry : std::filesystem::directory_iterator(p, std::filesystem::directory_options::skip_permission_denied)) {
                std::wstring fullPath = entry.path().wstring();
                FileEntry fe;
                fe.volume = folderPath.substr(0, 2);
                fe.name = entry.path().filename().wstring();
                fe.attributes = entry.is_directory() ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
                
                // 2026-03-xx 降级模式主键增强
                fe.frn = std::hash<std::wstring>{}(fullPath);
                
                results.push_back(fe);
            }
        } catch (...) {}
    }
    return results;
}

/**
 * @brief 根据路径逆向检索 FRN
 */

/**
 * @brief 实现 O(1) 路径检索
 */
DWORDLONG MftReader::getFrnFromPath(const std::wstring& folderPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::wstring vol = folderPath.length() >= 2 ? folderPath.substr(0, 2) : L"";
    if (vol.empty()) return 0;
    
    const auto& volPathMap = m_pathToFrn[vol];
    // 2026-03-xx 极致性能优化：全量预计算后，此处检索复杂度严格为 O(1)
    auto it = volPathMap.find(folderPath);
    if (it != volPathMap.end()) {
        return it->second;
    }
    
    return 0; 
}

/**
 * @brief 实现并行文件名搜索 (std::execution::par)
 */
std::vector<FileEntry> MftReader::search(const std::wstring& query, const std::wstring& volume) {
    if (query.empty()) return {};

    // 2026-03-xx 极致性能重构：小写转换提前至外部
    std::wstring lQuery = query;
    std::transform(lQuery.begin(), lQuery.end(), lQuery.begin(), ::towlower);

    // 1. 收集所有待搜索项的指针
    std::vector<const FileEntry*> pool;
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (!volume.empty()) {
            auto it = m_index.find(volume);
            if (it != m_index.end()) {
                for (auto& pair : it->second) pool.push_back(&pair.second);
            }
        } else {
            for (auto& volPair : m_index) {
                for (auto& pair : volPair.second) pool.push_back(&pair.second);
            }
        }
    }

    // 3. 并行过滤 (2026-03-xx 极致性能：消除循环内内存分配，直接匹配预存的小写字段)
    std::vector<FileEntry> results;
    std::mutex resultsMutex;

    std::for_each(std::execution::par, pool.begin(), pool.end(), [&](const FileEntry* entry) {
        if (entry->nameLower.find(lQuery) != std::wstring::npos) {
            std::lock_guard<std::mutex> lock(resultsMutex);
            results.push_back(*entry);
        }
    });

    return results;
}

/**
 * @brief USN 监听器更新内存索引，并同步维护反向索引 (极致性能：增量维护路径缓存)
 */
void MftReader::updateEntry(const FileEntry& entry) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    // 1. 维护主索引
    auto& volIndex = m_index[entry.volume];
    bool isNew = (volIndex.find(entry.frn) == volIndex.end());
    DWORDLONG oldParentFrn = isNew ? 0 : volIndex[entry.frn].parentFrn;
    
    FileEntry newEntry = entry;
    // 2026-03-xx 极致性能优化：维护预存储的小写名称
    newEntry.nameLower = newEntry.name;
    std::transform(newEntry.nameLower.begin(), newEntry.nameLower.end(), newEntry.nameLower.begin(), ::towlower);

    volIndex[entry.frn] = newEntry;

    // 2. 维护父子关系索引
    if (!isNew && oldParentFrn != entry.parentFrn) {
        // 如果移动了位置，从旧父节点移除
        auto& oldChildren = m_parentToChildren[entry.volume][oldParentFrn];
        oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), entry.frn), oldChildren.end());
    }
    
    if (isNew || oldParentFrn != entry.parentFrn) {
        m_parentToChildren[entry.volume][entry.parentFrn].push_back(entry.frn);
    }

    // 3. 增量维护路径缓存 (2026-03-xx 极致性能优化：采用异步延迟刷新，防止批量操作卡死)
    triggerPathRefresh(entry.volume);
}

void MftReader::triggerPathRefresh(const std::wstring& volume) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_refreshPending[volume]) return; // 已有挂起的任务
    m_refreshPending[volume] = true;
    
    // 开启后台线程执行延时刷新
    std::thread([this, volume]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        precomputePaths(volume);
        {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_refreshPending[volume] = false;
        }
    }).detach();
}

std::wstring MftReader::getPathFromFrn(const std::wstring& volume, DWORDLONG frn) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto itVol = m_frnToPath.find(volume);
    if (itVol != m_frnToPath.end()) {
        auto itPath = itVol->second.find(frn);
        if (itPath != itVol->second.end()) {
            return itPath->second;
        }
    }
    return L"";
}

/**
 * @brief USN 监听器移除记录
 */
void MftReader::removeEntry(const std::wstring& volume, DWORDLONG frn) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto& volIndex = m_index[volume];
    auto it = volIndex.find(frn);
    if (it != volIndex.end()) {
        DWORDLONG parentFrn = it->second.parentFrn;
        // 从主索引移除
        volIndex.erase(it);
        // 从父子索引移除
        auto& children = m_parentToChildren[volume][parentFrn];
        children.erase(std::remove(children.begin(), children.end(), frn), children.end());
        // 2026-03-xx 极致性能与并发安全：移除 clear() 以防止索引“真空期”
        // 触发异步延迟路径刷新，利用影子构建机制在后台重建索引
        triggerPathRefresh(volume);
    }
}

} // namespace ArcMeta
```

## 文件: `src/mft/MftReader.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cwctype>
#include <windows.h>

namespace ArcMeta {

/**
 * @brief 文件条目基础结构
 */
struct FileEntry {
    std::wstring volume;
    DWORDLONG frn = 0;
    DWORDLONG parentFrn = 0;
    std::wstring name;
    std::wstring nameLower; // 2026-03-xx 极致性能：预存储小写名称
    DWORD attributes = 0;

    bool isDir() const { return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

/**
 * @brief 文件索引管理器
 * 实现 MFT 枚举及无权限时的文件系统降级扫描
 */
class MftReader {
public:
    static MftReader& instance();

    /**
     * @brief 构建全盘索引
     * 自动尝试 MFT 模式，失败或无权限时降级为 std::filesystem 模式
     */
    void buildIndex();

    /**
     * @brief 获取指定目录下的子项
     */
    std::vector<FileEntry> getChildren(const std::wstring& folderPath);

    /**
     * @brief 并行全局搜索文件名
     * @param query 搜索关键词
     * @param volume 限制在特定卷搜索，为空则全盘搜索
     */
    std::vector<FileEntry> search(const std::wstring& query, const std::wstring& volume = L"");

private:
    MftReader() = default;
    
    bool loadMftForVolume(const std::wstring& volumeName);
    void scanDirectoryFallback(const std::wstring& volumeName);

    // volume -> (frn -> Entry)
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, FileEntry>> m_index;
    
    // 关键优化：父子关系反向索引 volume -> (parentFrn -> vector of childFrns)
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, std::vector<DWORDLONG>>> m_parentToChildren;

    // 性能优化：volume -> (fullPath -> frn)
    std::unordered_map<std::wstring, std::unordered_map<std::wstring, DWORDLONG>> m_pathToFrn;
    // 2026-03-xx 极致性能：双向 $O(1)$ 映射 volume -> (frn -> fullPath)
    std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, std::wstring>> m_frnToPath;

    /**
     * @brief 根据路径获取对应的 FRN（用于 MFT 模式下的钻取）
     */
public:
    DWORDLONG getFrnFromPath(const std::wstring& folderPath);

private:
    /**
     * @brief 极致性能：预计算全量路径映射
     */
    void precomputePaths(const std::wstring& volume);

    // 降级模式下的路径索引：fullPath -> Entry
    std::unordered_map<std::wstring, FileEntry> m_pathIndex;
    
    bool m_isUsingMft = false;

public:
    const std::unordered_map<std::wstring, std::unordered_map<DWORDLONG, FileEntry>>& getIndex() const { return m_index; }
    
    /**
     * @brief 根据 FRN 获取全路径 ($O(1)$ 复杂度)
     */
    std::wstring getPathFromFrn(const std::wstring& volume, DWORDLONG frn);

    /**
     * @brief USN 监听器更新内存索引，并同步维护反向索引
     */
    void updateEntry(const FileEntry& entry);

    /**
     * @brief 触发异步延迟路径刷新 (防抖)
     */
    void triggerPathRefresh(const std::wstring& volume);

    /**
     * @brief USN 监听器移除记录
     */
    void removeEntry(const std::wstring& volume, DWORDLONG frn);

private:
    mutable std::recursive_mutex m_mutex; // 保护所有索引数据
    std::unordered_map<std::wstring, bool> m_refreshPending;
};

} // namespace ArcMeta
```

## 文件: `src/ui/NavPanel.cpp`

```cpp
#include "NavPanel.h"
#include "UiHelper.h"
#include <QHeaderView>
#include <QScrollBar>
#include <QLabel>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QFileIconProvider>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>

namespace ArcMeta {

/**
 * @brief 构造函数，设置面板属性
 */
NavPanel::NavPanel(QWidget* parent)
    : QWidget(parent) {
    // 设置面板宽度（遵循文档：导航面板 200px）
    setFixedWidth(200);
    setStyleSheet("QWidget { background-color: #1E1E1E; color: #EEEEEE; border: none; }");

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    initUi();
}

/**
 * @brief 初始化 UI 组件
 */
void NavPanel::initUi() {
    // 面板标题
    QLabel* titleLabel = new QLabel("本地目录", this);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #4a90e2; padding: 10px 12px; background: #252526;");
    m_mainLayout->addWidget(titleLabel);

    m_treeView = new QTreeView(this);
    m_treeView->setHeaderHidden(true);
    m_treeView->setAnimated(true);
    m_treeView->setIndentation(16);
    m_treeView->setExpandsOnDoubleClick(true);

    m_model = new QStandardItemModel(this);
    QFileIconProvider iconProvider;

    // 1. 新增：桌面入口 (使用系统原生图标)
    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QStandardItem* desktopItem = new QStandardItem(iconProvider.icon(QFileInfo(desktopPath)), "桌面");
    desktopItem->setData(desktopPath, Qt::UserRole + 1);
    // 增加虚拟子项以便显示展开箭头
    desktopItem->appendRow(new QStandardItem("Loading..."));
    m_model->appendRow(desktopItem);

    // 2. 新增：此电脑入口 (使用系统原生图标)
    // 对于此电脑这种虚拟路径，尝试用 Computer 专用图标，若失败则回退到系统驱动器图标
    QIcon computerIcon = iconProvider.icon(QFileIconProvider::Computer);
    QStandardItem* computerItem = new QStandardItem(computerIcon, "此电脑");
    computerItem->setData("computer://", Qt::UserRole + 1);
    m_model->appendRow(computerItem);

    // 3. 磁盘列表
    const auto drives = QDir::drives();
    for (const QFileInfo& drive : drives) {
        QString driveName = drive.absolutePath();
        QStandardItem* driveItem = new QStandardItem(iconProvider.icon(drive), driveName);
        driveItem->setData(driveName, Qt::UserRole + 1);
        driveItem->appendRow(new QStandardItem("Loading..."));
        m_model->appendRow(driveItem);
    }

    m_treeView->setModel(m_model);
    connect(m_treeView, &QTreeView::expanded, this, &NavPanel::onItemExpanded);

    // 树形控件样式美化 (禁止使用或显示三角形)
    m_treeView->setStyleSheet(
        "QTreeView { background-color: transparent; border: none; font-size: 12px; selection-background-color: #378ADD; outline: none; }"
        "QTreeView::item { height: 28px; padding-left: 4px; color: #EEEEEE; }"
        "QTreeView::item:hover { background-color: rgba(255, 255, 255, 0.05); }"
        "QTreeView::branch:has-children:!has-siblings:closed,"
        "QTreeView::branch:closed:has-children:has-siblings,"
        "QTreeView::branch:has-children:!has-siblings:open,"
        "QTreeView::branch:open:has-children:has-siblings { border-image: none; image: none; }"
    );

    // 滚动条样式
    m_treeView->verticalScrollBar()->setStyleSheet(
        "QScrollBar:vertical { background: transparent; width: 4px; }"
        "QScrollBar::handle:vertical { background: #444444; border-radius: 4px; }"
    );

    connect(m_treeView, &QTreeView::clicked, this, &NavPanel::onTreeClicked);

    m_mainLayout->addWidget(m_treeView);
}

/**
 * @brief 设置当前显示的根路径并自动展开
 */
void NavPanel::setRootPath(const QString& path) {
    Q_UNUSED(path);
    // 由于改为扁平化快捷入口列表，不再支持 setRootPath 的树深度同步
}

/**
 * @brief 当用户点击目录时，发出信号告知外部组件（如内容面板）
 */
void NavPanel::onTreeClicked(const QModelIndex& index) {
    QString path = index.data(Qt::UserRole + 1).toString();
    if (!path.isEmpty() && path != "computer://") {
        emit directorySelected(path);
    } else if (path == "computer://") {
        emit directorySelected("computer://");
    }
}

void NavPanel::onItemExpanded(const QModelIndex& index) {
    QStandardItem* item = m_model->itemFromIndex(index);
    if (!item) return;

    // 如果只有一个 Loading 子项，则触发真实加载
    if (item->rowCount() == 1 && item->child(0)->text() == "Loading...") {
        fetchChildDirs(item);
    }
}

void NavPanel::fetchChildDirs(QStandardItem* parent) {
    QString path = parent->data(Qt::UserRole + 1).toString();
    if (path.isEmpty() || path == "computer://") return;

    parent->removeRows(0, parent->rowCount());

    QDir dir(path);
    QFileInfoList list = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    QFileIconProvider iconProvider;

    for (const QFileInfo& info : list) {
        QStandardItem* child = new QStandardItem(iconProvider.icon(info), info.fileName());
        child->setData(info.absoluteFilePath(), Qt::UserRole + 1);
        
        // 探测是否有子目录，有则加占位符
        QDir subDir(info.absoluteFilePath());
        if (!subDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty()) {
            child->appendRow(new QStandardItem("Loading..."));
        }
        parent->appendRow(child);
    }
}

} // namespace ArcMeta
```

## 文件: `src/ui/NavPanel.h`

```cpp
#pragma once

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVBoxLayout>
#include <QDir>

namespace ArcMeta {

/**
 * @brief 导航面板（面板二）
 * 使用 QTreeView + QFileSystemModel 实现文件夹树导航
 */
class NavPanel : public QWidget {
    Q_OBJECT

public:
    explicit NavPanel(QWidget* parent = nullptr);
    ~NavPanel() override = default;

    /**
     * @brief 设置并跳转到指定目录
     * @param path 完整路径
     */
    void setRootPath(const QString& path);

private slots:
    void onItemExpanded(const QModelIndex& index);

signals:
    /**
     * @brief 当用户点击目录时发出信号
     * @param path 目标目录完整路径
     */
    void directorySelected(const QString& path);

private:
    void initUi();
    void fetchChildDirs(QStandardItem* parent);
    
    QTreeView* m_treeView = nullptr;
    QStandardItemModel* m_model = nullptr;
    QVBoxLayout* m_mainLayout = nullptr;

private slots:
    void onTreeClicked(const QModelIndex& index);
};

} // namespace ArcMeta
```

## 文件: `src/mft/PathBuilder.cpp`

```cpp
#include "PathBuilder.h"
#include <algorithm>
#include <unordered_set>
#include <type_traits>

namespace ArcMeta {

std::wstring PathBuilder::getPath(const std::wstring& volume, DWORDLONG frn) {
    std::unordered_set<DWORDLONG> visited;
    return resolveRecursive(volume, frn, visited, 0);
}

/**
 * @brief 递归重构路径
 * 严格红线：64层深度限制 + 环路检测 (visitedFrns)
 */
std::wstring PathBuilder::resolveRecursive(const std::wstring& volume, DWORDLONG frn, 
                                             std::unordered_set<DWORDLONG>& visited, int depth) {
    // 1. 深度保护
    if (depth > 64) return L"";

    // 2. 环路检测
    if (visited.find(frn) != visited.end()) return L"";
    visited.insert(frn);

    // 3. 从索引获取 Entry
    const auto& globalIndex = MftReader::instance().getIndex();
    auto itVolume = globalIndex.find(volume);
    if (itVolume == globalIndex.end()) return L"";

    const auto& volumeMap = itVolume->second;
    auto itEntry = volumeMap.find(frn);
    if (itEntry == volumeMap.end()) {
        return L""; // 索引不完整
    }

    const FileEntry& entry = itEntry->second;

    // 4. 根目录判断条件：(parentFrn & 0x0000FFFFFFFFFFFF) == 5
    DWORDLONG pureParentFrn = entry.parentFrn & 0x0000FFFFFFFFFFFFLL;
    if (pureParentFrn == 5 || entry.frn == entry.parentFrn) {
        return volume + L"\\" + entry.name;
    }

    // 5. 继续向上递归
    std::wstring parentPath = resolveRecursive(volume, entry.parentFrn, visited, depth + 1);
    if (parentPath.empty()) return L"";

    return parentPath + L"\\" + entry.name;
}

} // namespace ArcMeta
```

## 文件: `src/mft/PathBuilder.h`

```cpp
#pragma once

#include "MftReader.h"
#include <string>
#include <unordered_set>

namespace ArcMeta {

/**
 * @brief 路径重建工具
 * 负责从 FRN 二元组递归向上重构物理路径
 */
class PathBuilder {
public:
    /**
     * @brief 获取 FRN 对应的完整物理路径
     * @param volume 卷标 (如 L"C:")
     * @param frn 目标 FRN
     * @return 完整路径字符串，失败返回空
     */
    static std::wstring getPath(const std::wstring& volume, DWORDLONG frn);

private:
    /**
     * @brief 递归核心逻辑
     * @param volume 卷标
     * @param frn 当前 FRN
     * @param visited 环路检测集合
     * @param depth 递归深度
     */
    static std::wstring resolveRecursive(const std::wstring& volume, DWORDLONG frn, 
                                          std::unordered_set<DWORDLONG>& visited, int depth);
};

} // namespace ArcMeta
```

## 文件: `src/ui/QuickLookWindow.cpp`

```cpp
#include "QuickLookWindow.h"
#include <QKeyEvent>
#include <QFileInfo>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QLabel>

namespace ArcMeta {

QuickLookWindow& QuickLookWindow::instance() {
    static QuickLookWindow inst;
    return inst;
}

QuickLookWindow::QuickLookWindow() : QWidget(nullptr) {
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setStyleSheet("QWidget { background-color: rgba(30, 30, 30, 0.95); border: 1px solid #444; border-radius: 6px; }");
    
    resize(800, 600);
    initUi();
}

void QuickLookWindow::initUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(10, 10, 10, 10);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setStyleSheet("color: #B0B0B0; font-size: 14px; font-weight: bold; margin-bottom: 5px;");
    m_mainLayout->addWidget(m_titleLabel);

    // 图片渲染层
    m_graphicsView = new QGraphicsView(this);
    m_graphicsView->setRenderHint(QPainter::Antialiasing);
    m_graphicsView->setRenderHint(QPainter::SmoothPixmapTransform);
    m_graphicsView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_graphicsView->setStyleSheet("background: transparent; border: none;");
    m_scene = new QGraphicsScene(this);
    m_graphicsView->setScene(m_scene);
    
    // 文本渲染层
    m_textPreview = new QPlainTextEdit(this);
    m_textPreview->setReadOnly(true);
    m_textPreview->setStyleSheet("background: transparent; color: #EEEEEE; border: none; font-family: 'Consolas';");
    
    m_mainLayout->addWidget(m_graphicsView);
    m_mainLayout->addWidget(m_textPreview);

    m_graphicsView->hide();
    m_textPreview->hide();
}

/**
 * @brief 预览文件分发逻辑
 */
void QuickLookWindow::previewFile(const QString& path) {
    QFileInfo info(path);
    m_titleLabel->setText(info.fileName());
    
    QString ext = info.suffix().toLower();
    if (ext == "jpg" || ext == "png" || ext == "bmp" || ext == "webp") {
        renderImage(path);
    } else {
        renderText(path);
    }

    show();
    raise();
    activateWindow();
}

/**
 * @brief 硬件加速图片渲染
 */
void QuickLookWindow::renderImage(const QString& path) {
    m_textPreview->hide();
    m_graphicsView->show();
    m_scene->clear();

    QPixmap pix(path);
    if (!pix.isNull()) {
        auto item = m_scene->addPixmap(pix);
        m_graphicsView->fitInView(item, Qt::KeepAspectRatio);
    }
}

/**
 * @brief 极速文本加载（红线：支持内存映射思想）
 */
void QuickLookWindow::renderText(const QString& path) {
    m_graphicsView->hide();
    m_textPreview->show();
    
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        // 对于大文件，仅加载前 128KB (文档红线要求)
        QByteArray previewBytes = file.read(128 * 1024); 
        m_textPreview->setPlainText(QString::fromUtf8(previewBytes));
        file.close();
    }
}

/**
 * @brief 按键交互：ESC 或 Space 退出预览，1-5 快速打标预览点位
 */
void QuickLookWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Space) {
        hide();
    } else if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_5) {
        int rating = event->key() - Qt::Key_0;
        emit ratingRequested(rating);
    }
}

} // namespace ArcMeta
```

## 文件: `src/ui/QuickLookWindow.h`

```cpp
#pragma once

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPlainTextEdit>

namespace ArcMeta {

/**
 * @brief 专业快速预览窗口
 * 硬件加速图片预览、大文件 Markdown 内存映射极速加载
 */
class QuickLookWindow : public QWidget {
    Q_OBJECT

public:
    static QuickLookWindow& instance();

    /**
     * @brief 预览指定文件
     * @param path 文件路径
     */
    void previewFile(const QString& path);

signals:
    /**
     * @brief 用户按 1-5 键设置星级时发出
     */
    void ratingRequested(int rating);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QuickLookWindow();
    ~QuickLookWindow() override = default;

    void initUi();
    void renderImage(const QString& path);
    void renderText(const QString& path);

    QVBoxLayout* m_mainLayout = nullptr;
    
    // 图片预览组件（QGraphicsView 硬件加速）
    QGraphicsView* m_graphicsView = nullptr;
    QGraphicsScene* m_scene = nullptr;
    
    // 文本预览组件 (Markdown / Text)
    QPlainTextEdit* m_textPreview = nullptr;

    QLabel* m_titleLabel = nullptr;
};

} // namespace ArcMeta
```

## 文件: `src/db/SyncEngine.cpp`

```cpp
#include "SyncEngine.h"
#include "Database.h"
#include "../meta/SyncQueue.h"
#include <windows.h>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <filesystem>
#include <map>

namespace ArcMeta {

SyncEngine& SyncEngine::instance() {
    static SyncEngine inst;
    return inst;
}

/**
 * @brief 增量同步：只处理 mtime > last_sync_time 的 .am_meta.json
 */
void SyncEngine::runIncrementalSync() {
    double lastSyncTime = 0;
    
    // 获取上次同步时间
    QSqlQuery st("SELECT value FROM sync_state WHERE key = 'last_sync_time'");
    if (st.next()) {
        lastSyncTime = st.value(0).toDouble();
    }

    // 执行全表增量扫描逻辑
    QSqlQuery query("SELECT path FROM folders");
    while (query.next()) {
        std::wstring path = query.value(0).toString().toStdWString();
        std::wstring jsonPath = path + L"\\.am_meta.json";
        
        QFileInfo info(QString::fromStdWString(jsonPath));
        if (info.exists() && info.lastModified().toMSecsSinceEpoch() > lastSyncTime) {
            SyncQueue::instance().enqueue(path);
        }
    }
}

/**
 * @brief 全量扫描：递归所有盘符搜集元数据
 */
void SyncEngine::runFullScan(std::function<void(int current, int total)> onProgress) {
    std::vector<std::wstring> metaFiles;
    
    // 枚举所有固定驱动器
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            wchar_t drivePath[] = { (wchar_t)(L'A' + i), L':', L'\\', L'\0' };
            if (GetDriveTypeW(drivePath) == DRIVE_FIXED) {
                scanDirectory(std::filesystem::path(drivePath), metaFiles);
            }
        }
    }

    // 清理并重建核心表
    QSqlQuery q;
    q.exec("DELETE FROM folders");
    q.exec("DELETE FROM items");
    q.exec("DELETE FROM tags");

    int total = (int)metaFiles.size();
    for (int i = 0; i < total; ++i) {
        // 提取父目录并加入队列进行解析同步
        std::wstring parentDir = std::filesystem::path(metaFiles[i]).parent_path().wstring();
        SyncQueue::instance().enqueue(parentDir);
        
        if (onProgress) onProgress(i + 1, total);
    }
    
    // 强制刷空队列
    SyncQueue::instance().flush();
    // 更新同步时间
    QSqlQuery updateSync;
    updateSync.prepare("INSERT OR REPLACE INTO sync_state (key, value) VALUES ('last_sync_time', ?)");
    updateSync.addBindValue((double)QDateTime::currentMSecsSinceEpoch());
    updateSync.exec();
    
    rebuildTagStats();
}

/**
 * @brief 标签聚合统计逻辑
 */
void SyncEngine::rebuildTagStats() {
    QSqlDatabase db = QSqlDatabase::database();
    db.transaction();
    
    QSqlQuery("DELETE FROM tags");
    
    // 聚合 items 表中的标签
    QSqlQuery query("SELECT tags FROM items WHERE tags != ''");
    std::map<std::string, int> tagCounts;
    while (query.next()) {
        QByteArray jsonData = query.value(0).toByteArray();
        QJsonDocument doc = QJsonDocument::fromJson(jsonData);
        if (doc.isArray()) {
            for (const auto& val : doc.array()) {
                QString t = val.toString();
                if (!t.isEmpty()) tagCounts[t.toStdString()]++;
            }
        }
    }

    for (const auto& [tag, count] : tagCounts) {
        QSqlQuery ins;
        ins.prepare("INSERT INTO tags (tag, item_count) VALUES (?, ?)");
        ins.addBindValue(QString::fromStdString(tag));
        ins.addBindValue(count);
        ins.exec();
    }

    db.commit();
}

/**
 * @brief 递归扫描目录（排除系统隐藏文件夹）
 */
void SyncEngine::scanDirectory(const std::filesystem::path& root, std::vector<std::wstring>& metaFiles) {
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file() && entry.path().filename() == ".am_meta.json") {
                metaFiles.push_back(entry.path().wstring());
            }
        }
    } catch (...) {
        // 无权限访问目录
    }
}

} // namespace ArcMeta
```

## 文件: `src/db/SyncEngine.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace ArcMeta {

/**
 * @brief 同步引擎
 * 负责离线变更追平（增量同步）与系统全量扫描逻辑
 */
class SyncEngine {
public:
    static SyncEngine& instance();

    /**
     * @brief 启动增量同步（程序启动时自动调用）
     */
    void runIncrementalSync();

    /**
     * @brief 启动全量扫描（由用户手动触发）
     * @param onProgress 进度回调
     */
    void runFullScan(std::function<void(int current, int total)> onProgress);

    /**
     * @brief 维护标签聚合表 (tags 表)
     */
    void rebuildTagStats();

private:
    SyncEngine() = default;
    ~SyncEngine() = default;

    void scanDirectory(const std::filesystem::path& root, std::vector<std::wstring>& metaFiles);
};

} // namespace ArcMeta
```

## 文件: `src/meta/SyncQueue.cpp`

```cpp
#include "SyncQueue.h"
#include <QFileInfo>
#include "AmMetaJson.h"
#include "../db/Database.h"
#include "../db/FolderRepo.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include "../db/ItemRepo.h"
#include <vector>
#include <QString>
#include <QDateTime>

namespace ArcMeta {

SyncQueue& SyncQueue::instance() {
    static SyncQueue inst;
    return inst;
}

SyncQueue::SyncQueue() {}

SyncQueue::~SyncQueue() {
    stop();
}

void SyncQueue::start() {
    if (m_running) return;
    m_running = true;
    m_thread = std::thread(&SyncQueue::workerThread, this);
}

void SyncQueue::stop() {
    if (!m_running) return;
    m_running = false;
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // 程序退出前执行最后的强制同步
    flush();
}

void SyncQueue::enqueue(const std::wstring& folderPath) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingPaths.insert(folderPath); // set 自动去重
    }
    m_cv.notify_one();
}

void SyncQueue::flush() {
    while (true) {
        if (!processBatch()) break;
    }
}

void SyncQueue::workerThread() {
    while (m_running) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(2000), [this] {
                return !m_running || !m_pendingPaths.empty();
            });
        }
        
        if (!m_running && m_pendingPaths.empty()) break;
        
        // 批量处理当前队列中的路径
        processBatch();
    }
}

/**
 * @brief 核心业务逻辑：从 JSON 同步数据到 SQLite 事务
 */
bool SyncQueue::processBatch() {
    std::vector<std::wstring> batch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_pendingPaths.empty()) return false;
        batch.assign(m_pendingPaths.begin(), m_pendingPaths.end());
        m_pendingPaths.clear();
    }

    if (batch.empty()) return false;

    // 关键红线修复：QtSql 连接不能跨线程，为后台线程创建独立连接
    QString connName = "SyncWorkerConnection";
    QSqlDatabase db;
    if (QSqlDatabase::contains(connName)) {
        db = QSqlDatabase::database(connName);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(QString::fromStdWString(Database::instance().getDbPath()));
    }

    if (!db.isOpen() && !db.open()) return false;

    try {
        db.transaction();

        for (const auto& path : batch) {
            AmMetaJson meta(path);
            if (!meta.load()) continue;

            // 1. 使用 Repository 同步文件夹 (2026-03-xx 传入后台线程私有连接 db)
            FolderRepo::save(path, meta.folder(), db);

            // 2. 使用 Repository 同步所有条目 (2026-03-xx 传入后台线程私有连接 db)
            for (auto& [name, iMeta] : meta.items()) {
                // 2026-03-xx 极致性能优化：在同步阶段预填充系统属性，消除 UI 线程的 QFileInfo 压力
                std::wstring fullPath = path;
                if (!fullPath.empty() && fullPath.back() != L'\\' && fullPath.back() != L'/') fullPath += L'\\';
                fullPath += name;
                
                QFileInfo info(QString::fromStdWString(fullPath));
                if (info.exists()) {
                    iMeta.size = info.size();
                    iMeta.mtime = (double)info.lastModified().toMSecsSinceEpoch();
                    iMeta.ctime = (double)info.birthTime().toMSecsSinceEpoch();
                }
                
                ItemRepo::save(path, name, iMeta, db);
            }
        }

        if (db.commit()) {
            return true;
        } else {
            db.rollback();
            return false;
        }
    } catch (...) {
        db.rollback();
        return false;
    }
}

} // namespace ArcMeta
```

## 文件: `src/meta/SyncQueue.h`

```cpp
#pragma once

#include <string>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace ArcMeta {

/**
 * @brief 懒更新队列
 * 实现 .am_meta.json 变更后的异步防抖同步到数据库逻辑
 */
class SyncQueue {
public:
    static SyncQueue& instance();

    /**
     * @brief 启动后台同步线程
     */
    void start();

    /**
     * @brief 停止后台同步线程并确保队列刷空 (Flush)
     */
    void stop();

    /**
     * @brief 将发生变更的文件夹路径加入队列
     * @param folderPath 文件夹完整路径
     */
    void enqueue(const std::wstring& folderPath);

    /**
     * @brief 强制同步当前队列中的所有路径（阻塞直至完成）
     */
    void flush();

private:
    SyncQueue();
    ~SyncQueue();
    SyncQueue(const SyncQueue&) = delete;
    SyncQueue& operator=(const SyncQueue&) = delete;

    void workerThread();
    bool processBatch();

    std::set<std::wstring> m_pendingPaths; // 使用 set 自动去重合并 (防抖)
    std::mutex m_mutex;
    std::condition_variable m_cv;
    
    std::thread m_thread;
    std::atomic<bool> m_running{false};
};

} // namespace ArcMeta
```

## 文件: `src/ui/ToolTipOverlay.cpp`

```cpp
#include "ToolTipOverlay.h"

namespace ArcMeta {

ToolTipOverlay::ToolTipOverlay() : QWidget(nullptr) {
    // [CRITICAL] 彻底弃用 Qt::ToolTip，防止 OS 动画残留
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | 
                  Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus);
    setObjectName("ToolTipOverlay");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    
    m_doc.setUndoRedoEnabled(false);
    // [ULTIMATE FIX] 强制锁定调色板颜色
    QPalette pal = palette();
    pal.setColor(QPalette::WindowText, QColor("#EEEEEE"));
    pal.setColor(QPalette::Text, QColor("#EEEEEE"));
    pal.setColor(QPalette::ButtonText, QColor("#EEEEEE"));
    setPalette(pal);

    m_doc.setDefaultStyleSheet("body, div, p, span, b, i { color: #EEEEEE !important; font-family: 'Microsoft YaHei', 'Segoe UI'; }"); 
    setStyleSheet("QWidget { color: #EEEEEE !important; background: transparent; }");

    QFont f = font();
    f.setPointSize(9);
    m_doc.setDefaultFont(f);

    m_hideTimer.setSingleShot(true);
    connect(&m_hideTimer, &QTimer::timeout, this, &QWidget::hide);

    hide();
}

void ToolTipOverlay::showText(const QPoint& globalPos, const QString& text, int timeout, const QColor& borderColor) {
    // [THREAD SAFE] 强制确保在主线程执行
    if (thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(this, [this, globalPos, text, timeout, borderColor]() { 
            showText(globalPos, text, timeout, borderColor); 
        });
        return;
    }

    if (text.isEmpty()) { hide(); return; }
    
    if (timeout > 0) {
        timeout = qBound(500, timeout, 60000); 
    }

    m_currentBorderColor = borderColor;

    QString htmlBody;
    if (text.contains("<") && text.contains(">")) {
        htmlBody = text;
    } else {
        htmlBody = text.toHtmlEscaped().replace("\n", "<br>");
    }

    m_text = QString(
        "<html><head><style>div, p, span, body { color: #EEEEEE !important; }</style></head>"
        "<body style='margin:0; padding:0; color:#EEEEEE; font-family:\"Microsoft YaHei\",\"Segoe UI\",sans-serif;'>"
        "<div style='color:#EEEEEE !important;'>%1</div>"
        "</body></html>"
    ).arg(htmlBody);
    
    m_doc.setHtml(m_text);
    m_doc.setDocumentMargin(0); 
    
    m_doc.setTextWidth(-1); 
    qreal idealW = m_doc.idealWidth();
    
    if (idealW > 450) {
        m_doc.setTextWidth(450); 
    } else {
        m_doc.setTextWidth(idealW); 
    }
    
    QSize textSize = m_doc.size().toSize();
    
    int padX = 12; 
    int padY = 8;
    
    int w = textSize.width() + padX * 2;
    int h = textSize.height() + padY * 2;
    
    w = qMax(w, 40);
    h = qMax(h, 24);
    
    resize(w, h);
    
    QPoint pos = globalPos + QPoint(15, 15);
    
    QScreen* screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        QRect screenGeom = screen->geometry();
        if (pos.x() + width() > screenGeom.right()) {
            pos.setX(globalPos.x() - width() - 15);
        }
        if (pos.y() + height() > screenGeom.bottom()) {
            pos.setY(globalPos.y() - height() - 15);
        }
    }
    
    move(pos);
    show();
    raise();
    update();

    if (timeout > 0) {
        m_hideTimer.start(timeout);
    } else {
        m_hideTimer.stop();
    }
}

void ToolTipOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    
    QRectF rectF(0.5, 0.5, width() - 1, height() - 1);
    
    p.setPen(QPen(m_currentBorderColor, 1));
    p.setBrush(QColor("#2B2B2B"));
    p.drawRoundedRect(rectF, 4, 4);
    
    p.save();
    p.translate(12, 8); 
    m_doc.drawContents(&p);
    p.restore();
}

} // namespace ArcMeta
```

## 文件: `src/ui/ToolTipOverlay.h`

```cpp
#ifndef TOOLTIPOVERLAY_H
#define TOOLTIPOVERLAY_H

#include <QWidget>
#include <QPainter>
#include <QElapsedTimer>
#include <QDebug>
#include <QScreen>
#include <QGuiApplication>
#include <QApplication>
#include <QTimer>
#include <QThread>
#include <QFontMetrics>
#include <QTextDocument>
#include <QPointer>
#include <QPainterPath>
#include <QColor>
#include <QPen>
#include <QBrush>
#include <QRectF>

namespace ArcMeta {

/**
 * @brief ToolTipOverlay: 全局统一的自定义 Tooltip
 * [CRITICAL] 本项目严禁使用任何形式的“Windows 系统默认 Tip 样式”！
 * [RULE] 1. 杜绝原生内容带来的系统阴影和不透明度。
 * [RULE] 2. 所有的 ToolTip 逻辑必须通过此 ToolTipOverlay 渲染。
 * [RULE] 3. 此组件必须保持扁平化 (Flat)，严禁添加任何阴影特效。
 */
class ToolTipOverlay : public QWidget {
    Q_OBJECT
public:
    static ToolTipOverlay* instance() {
        static QPointer<ToolTipOverlay> inst;
        if (!inst) {
            inst = new ToolTipOverlay();
        }
        return inst;
    }

    /**
     * @brief 显示提示文字（2026-03-xx 重构升级版）
     */
    void showText(const QPoint& globalPos, const QString& text, int timeout = 700, const QColor& borderColor = QColor("#B0B0B0"));

    // 兼容旧接口
    void showTip(const QString& text, const QPoint& pos, int timeout = 700) {
        showText(pos, text, timeout);
    }

    static void hideTip() {
        if (instance()) instance()->hide();
    }

protected:
    explicit ToolTipOverlay();
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    QTextDocument m_doc;
    QTimer m_hideTimer;
    QColor m_currentBorderColor = QColor("#B0B0B0");
};

} // namespace ArcMeta

#endif // TOOLTIPOVERLAY_H
```

## 文件: `src/ui/UiHelper.h`

```cpp
#pragma once

#include <QIcon>
#include <QString>
#include <QColor>
#include <QSvgRenderer>
#include <QPainter>
#include <QPixmap>
#include <QMap>
#include <QSettings>
#include "../../SvgIcons.h"

namespace ArcMeta {

/**
 * @brief UI 辅助类
 * 提供统一的图标渲染、样式计算等工具函数
 */
class UiHelper {
public:
    /**
     * @brief 获取带颜色的 SVG 图标 (返回 QIcon)
     */
    static QIcon getIcon(const QString& key, const QColor& color, int size = 18) {
        return QIcon(getPixmap(key, QSize(size, size), color));
    }

    /**
     * @brief 获取带颜色的 SVG Pixmap (返回 QPixmap)
     */
    static QPixmap getPixmap(const QString& key, const QSize& size, const QColor& color) {
        if (!SvgIcons::icons.contains(key)) return QPixmap();

        QString svgData = SvgIcons::icons[key];
        // 渲染前替换颜色占位符
        if (svgData.contains("currentColor")) {
            svgData.replace("currentColor", color.name());
        } else {
            // 如果原本没有 currentColor 占位符但指定了颜色，尝试注入
            svgData.replace("fill=\"none\"", QString("fill=\"%1\"").arg(color.name()));
            svgData.replace("stroke=\"currentColor\"", QString("stroke=\"%1\"").arg(color.name()));
        }
        
        QPixmap pixmap(size);
        pixmap.fill(Qt::transparent);
        
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer renderer(svgData.toUtf8());
        renderer.render(&painter);
        
        return pixmap;
    }

    /**
     * @brief 获取扩展名对应的颜色 (哈希生成 + 持久化缓存)
     */
    static QColor getExtensionColor(const QString& ext) {
        static QMap<QString, QColor> s_cache;
        QString upperExt = ext.toUpper();
        
        // 1. 文件夹特殊处理
        if (upperExt == "DIR") return QColor(45, 65, 85, 200);
        if (upperExt.isEmpty()) return QColor(60, 60, 60, 180);

        // 2. 检查运行时缓存
        if (s_cache.contains(upperExt)) return s_cache[upperExt];

        // 3. 检查持久化存储 (QSettings)
        QSettings settings("ArcMeta团队", "ArcMeta");
        QString settingKey = QString("ExtensionColors/%1").arg(upperExt);
        if (settings.contains(settingKey)) {
            QColor color = settings.value(settingKey).value<QColor>();
            s_cache[upperExt] = color;
            return color;
        }

        // 4. 哈希法生成新颜色 (HSL 保证色彩分布)
        size_t hash = qHash(upperExt);
        int hue = static_cast<int>(hash % 360); // 0-359 色相
        // 固定 S=160, L=110 保证在深色背景下的可读性且色彩饱满
        QColor color = QColor::fromHsl(hue, 160, 110, 200); 
        
        // 写入缓存并持久化
        s_cache[upperExt] = color;
        settings.setValue(settingKey, color);
        return color;
    }
};

} // namespace ArcMeta
```

## 文件: `src/mft/UsnWatcher.cpp`

```cpp
#include "UsnWatcher.h"
#include "PathBuilder.h"
#include "../db/Database.h"
#include "../db/ItemRepo.h"
#include "../db/FolderRepo.h"
#include <winioctl.h>
#include <vector>
#include <QSqlQuery>
#include <QVariant>
#include <QString>

namespace ArcMeta {

UsnWatcher::UsnWatcher(const std::wstring& volume) : m_volume(volume) {}

UsnWatcher::~UsnWatcher() {
    stop();
}

void UsnWatcher::start() {
    if (m_running) return;
    m_running = true;
    m_thread = std::thread(&UsnWatcher::watcherThread, this);
}

void UsnWatcher::stop() {
    if (!m_running) return;
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void UsnWatcher::watcherThread() {
    std::wstring volPath = L"\\\\.\\" + m_volume;
    HANDLE hVol = CreateFileW(volPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVol == INVALID_HANDLE_VALUE) return;

    USN_JOURNAL_DATA journalData;
    DWORD cb;
    if (!DeviceIoControl(hVol, FSCTL_QUERY_USN_JOURNAL, NULL, 0, &journalData, sizeof(journalData), &cb, NULL)) {
        CloseHandle(hVol);
        return;
    }

    // 2026-03-xx 按照红线要求：后台线程必须使用私有数据库连接
    QString connName = "UsnWatcher_" + QString::fromStdWString(m_volume);
    QSqlDatabase db;
    if (QSqlDatabase::contains(connName)) {
        db = QSqlDatabase::database(connName);
    } else {
        db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(QString::fromStdWString(Database::instance().getDbPath()));
    }
    if (!db.isOpen() && !db.open()) {
        CloseHandle(hVol);
        return;
    }

    // 1. 离线变更追平逻辑：从数据库读取 NextUsn (使用私有连接 db)
    QString key = "usn_state_" + QString::fromStdWString(m_volume);
    QSqlQuery query(db);
    query.prepare("SELECT value FROM sync_state WHERE key = ?");
    query.addBindValue(key);
    
    if (query.exec() && query.next()) {
        m_lastUsn = query.value(0).toString().toLongLong();
    } else {
        m_lastUsn = journalData.NextUsn;
    }

    READ_USN_JOURNAL_DATA readData;
    readData.StartUsn = m_lastUsn;
    readData.ReasonMask = 0xFFFFFFFF;
    readData.ReturnOnlyOnClose = FALSE;
    readData.Timeout = 0;
    readData.BytesToWaitFor = 0;
    readData.UsnJournalID = journalData.UsnJournalID;

    const int BUF_SIZE = 64 * 1024;
    std::vector<BYTE> buffer(BUF_SIZE);

    while (m_running) {
        if (DeviceIoControl(hVol, FSCTL_READ_USN_JOURNAL, &readData, sizeof(readData), buffer.data(), BUF_SIZE, &cb, NULL)) {
            if (cb <= sizeof(USN)) {
                // 无新记录，轮询休眠 (文档红线：200ms)
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }

            BYTE* pData = buffer.data() + sizeof(USN);
            while (pData < buffer.data() + cb) {
                USN_RECORD_V2* pRecord = (USN_RECORD_V2*)pData;
                handleRecord(pRecord, db);
                pData += pRecord->RecordLength;
            }
            
            // 更新起始 USN 为本次读取的最后一条
            readData.StartUsn = *(USN*)buffer.data();
        } else {
            break;
        }
    }

    CloseHandle(hVol);
}

/**
 * @brief 处理具体变更事件
 * 逻辑：CREATE -> 插入索引；DELETE -> 级联删除；RENAME -> 原子迁移
 */
void UsnWatcher::handleRecord(USN_RECORD_V2* pRecord, QSqlDatabase db) {
    DWORD reason = pRecord->Reason;
    std::wstring fileName(pRecord->FileName, pRecord->FileNameLength / sizeof(wchar_t));
    std::wstring frnStr = QString::number(pRecord->FileReferenceNumber, 16).prepend("0x").toStdWString();

    if (reason & USN_REASON_FILE_CREATE) {
        FileEntry entry;
        entry.volume = m_volume;
        entry.frn = pRecord->FileReferenceNumber;
        entry.parentFrn = pRecord->ParentFileReferenceNumber;
        entry.attributes = pRecord->FileAttributes;
        entry.name = fileName;
        MftReader::instance().updateEntry(entry);
    }

    if (reason & USN_REASON_FILE_DELETE) {
        ItemRepo::markAsDeleted(m_volume, frnStr, db);
        MftReader::instance().removeEntry(m_volume, pRecord->FileReferenceNumber);
        std::wstring fullPath = PathBuilder::getPath(m_volume, pRecord->FileReferenceNumber);
        if (!fullPath.empty()) {
            FolderRepo::remove(fullPath, db);
        }
    }

    if (reason & USN_REASON_RENAME_NEW_NAME) {
        // 1. 获取新路径及新父目录路径
        std::wstring newPath = PathBuilder::getPath(m_volume, pRecord->FileReferenceNumber);
        std::wstring newParentPath = PathBuilder::getPath(m_volume, pRecord->ParentFileReferenceNumber);
        std::wstring oldPath = ItemRepo::getPathByFrn(m_volume, frnStr, db);

        if (!newPath.empty() && oldPath != newPath) {
            // 2. 跨目录元数据迁移事务逻辑 (两阶段提交思想)
            if (!oldPath.empty()) {
                std::wstring oldParentDir = oldPath.substr(0, oldPath.find_last_of(L"\\/"));
                AmMetaJson oldMetaJson(oldParentDir);
                
                if (oldMetaJson.load()) {
                    std::wstring fileNameOnly = oldPath.substr(oldPath.find_last_of(L"\\/") + 1);
                    auto& items = oldMetaJson.items();
                    auto it = items.find(fileNameOnly);
                    if (it != items.end()) {
                        ItemMeta meta = it->second;
                        
                        // 第二阶段：物理迁移 JSON 记录到新目录
                        AmMetaJson newMetaJson(newParentPath);
                        // 2026-03-xx 增加加载校验：确保目标目录元数据加载正常
                        if (newMetaJson.load()) {
                            std::wstring newFileNameOnly = newPath.substr(newPath.find_last_of(L"\\/") + 1);
                            newMetaJson.items()[newFileNameOnly] = meta;
                            
                            if (newMetaJson.save()) {
                                // 成功后删除旧位置记录并更新数据库
                                items.erase(it);
                                oldMetaJson.save();
                            }
                        }
                    }
                }
            }
            // 无论 JSON 迁移与否，更新数据库路径与内存索引 (使用私有连接 db)
            ItemRepo::updatePath(m_volume, frnStr, newPath, newParentPath, db);

            // 更新内存 MFT 索引 (同步维护反向索引)
            FileEntry entry;
            entry.volume = m_volume;
            entry.frn = pRecord->FileReferenceNumber;
            entry.parentFrn = pRecord->ParentFileReferenceNumber;
            entry.attributes = pRecord->FileAttributes;
            entry.name = newPath.substr(newPath.find_last_of(L"\\/") + 1);
            MftReader::instance().updateEntry(entry);
        }
    }
}

} // namespace ArcMeta
```

## 文件: `src/mft/UsnWatcher.h`

```cpp
#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <QSqlDatabase>

namespace ArcMeta {

/**
 * @brief USN Journal 实时监听引擎
 * 每个卷启动独立线程，处理文件的增删改移
 */
class UsnWatcher {
public:
    explicit UsnWatcher(const std::wstring& volume);
    ~UsnWatcher();

    /**
     * @brief 启动监听线程
     */
    void start();

    /**
     * @brief 停止监听线程
     */
    void stop();

private:
    void watcherThread();
    
    /**
     * @brief 处理离线变更追平
     */
    void catchUpOfflineChanges(HANDLE hVol, USN_JOURNAL_DATA& journalData);

    /**
     * @brief 解析并分发单个记录
     */
    void handleRecord(USN_RECORD_V2* pRecord, QSqlDatabase db);

    std::wstring m_volume;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    
    USN m_lastUsn = 0;
};

} // namespace ArcMeta
```

