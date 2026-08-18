#ifndef STRINGUTILS_H
#define STRINGUTILS_H

#include <QString>
#include <QTextDocument>
#include <QMimeData>
#include <QClipboard>
#include <QApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QVariantList>
#include <QUrl>
#include <QDir>
#include <QProcess>
#include <QDateTime>
#include <QFileInfo>
#include <QDebug>
#include <QImage>
#include <QList>
#include <QMap>
#include <QVariantMap>
#include <QByteArray>
#include <QCoreApplication>
#include <vector>
#include <functional>
#include "../core/ClipboardMonitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

class StringUtils {
#ifdef Q_OS_WIN
    // [NEW] 基于事件驱动的浏览器检测缓存与回调
    inline static bool m_browserCacheValid = false;
    inline static bool m_isBrowserActiveCache = false;
    inline static std::function<void(bool)> m_focusCallback = nullptr;

    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG, LONG, DWORD, DWORD) {
        if (event == EVENT_SYSTEM_FOREGROUND) {
            m_browserCacheValid = false; // 前台窗口切换，失效缓存
            bool active = isBrowserActive(); 
            // qDebug() << "[StringUtils] 前台窗口切换 -> 浏览器激活状态:" << active;
            if (m_focusCallback) m_focusCallback(active);
        }
    }
#endif

public:
    // [DEPRECATED] 严禁使用原生 QToolTip
    static QString getToolTipStyle() {
        return "";
    }

    static QString wrapToolTip(const QString& text) {
        if (text.isEmpty()) return text;
        if (text.startsWith("<html>")) return text;
        return QString("<html><span style='white-space:nowrap;'>%1</span></html>").arg(text);
    }

    /**
     * @brief 注册焦点变化回调 (用于动态管理系统热键)
     */
    static void setFocusCallback(std::function<void(bool)> cb) {
#ifdef Q_OS_WIN
        m_focusCallback = cb;
#endif
    }

    /**
     * @brief [NEW] 判定指定窗口句柄是否为浏览器
     */
    static bool isBrowserWindow(HWND hwnd) {
#ifdef Q_OS_WIN
        if (!hwnd || !IsWindow(hwnd)) return false;
        
        DWORD pid;
        GetWindowThreadProcessId(hwnd, &pid);
        
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!process) process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        
        bool isBrowser = false;
        if (process) {
            wchar_t buffer[MAX_PATH];
            if (GetModuleFileNameExW(process, NULL, buffer, MAX_PATH)) {
                QString exePath = QString::fromWCharArray(buffer).toLower();
                QString exeName = QFileInfo(exePath).fileName();

                static QStringList browserExes;
                static qint64 lastLoadTime = 0;
                qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

                if (currentTime - lastLoadTime > 5000 || browserExes.isEmpty()) {
                    QSettings acquisitionSettings("RapidNotes", "Acquisition");
                    browserExes = acquisitionSettings.value("browserExes").toStringList();
                    if (browserExes.isEmpty()) {
                        browserExes = {
                            "chrome.exe", "msedge.exe", "firefox.exe", "brave.exe", 
                            "opera.exe", "iexplore.exe", "vivaldi.exe", "safari.exe",
                            "arc.exe", "sidekick.exe", "maxthon.exe", "thorium.exe",
                            "librewolf.exe", "waterfox.exe"
                        };
                    }
                    lastLoadTime = currentTime;
                }
                isBrowser = browserExes.contains(exeName, Qt::CaseInsensitive);
            }
            CloseHandle(process);
        }
        return isBrowser;
#else
        return false;
#endif
    }

    /**
     * @brief 判定当前活跃窗口是否为浏览器 (基于 WinEventHook 驱动的高效缓存与 HWND 即时校验)
     */
    static bool isBrowserActive() {
#ifdef Q_OS_WIN
        static bool hookInstalled = false;
        if (!hookInstalled) {
            SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, 
                           WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
            hookInstalled = true;
        }

        HWND hwnd = GetForegroundWindow();
        static HWND lastHwnd = nullptr;

        if (m_browserCacheValid && hwnd == lastHwnd) {
            return m_isBrowserActiveCache;
        }

        lastHwnd = hwnd;
        m_isBrowserActiveCache = isBrowserWindow(hwnd);
        m_browserCacheValid = true;
        return m_isBrowserActiveCache;
#else
        return false;
#endif
    }

    /**
     * @brief 判定文本是否包含非中文、非空白、非标点的“第二门语言”字符
     */
    static bool containsOtherLanguage(const QString& text) {
        static QRegularExpression otherLangRegex(R"([^\s\p{P}\x{4e00}-\x{9fa5}\x{3400}-\x{4dbf}\x{f900}-\x{faff}]+)");
        return text.contains(otherLangRegex);
    }

    /**
     * @brief 智能识别语言：判断文本是否包含中文 (扩展匹配范围以提高准确度)
     */
    static bool containsChinese(const QString& text) {
        // [OPTIMIZED] 扩展 CJK 范围，包含基本汉字及扩展区，确保如泰语+中文组合时能精准识别
        static QRegularExpression chineseRegex("[\\x{4e00}-\\x{9fa5}\\x{3400}-\\x{4dbf}\\x{f900}-\\x{faff}]+");
        return text.contains(chineseRegex);
    }

    /**
     * @brief 判断文本是否包含泰文
     */
    static bool containsThai(const QString& text) {
        static QRegularExpression thaiRegex("[\\x{0e00}-\\x{0e7f}]+");
        return text.contains(thaiRegex);
    }

    /**
     * @brief 智能语言拆分：中文作为标题，非中文作为内容 (增强单行及混合语言处理)
     */
    static void smartSplitLanguage(const QString& text, QString& title, QString& content) {
        QString trimmedText = text.trimmed();
        if (trimmedText.isEmpty()) {
            title = "新笔记";
            content = "";
            return;
        }

        static QRegularExpression chineseRegex("[\\x{4e00}-\\x{9fa5}\\x{3400}-\\x{4dbf}\\x{f900}-\\x{faff}]+");
        
        bool hasChinese = containsChinese(trimmedText);
        bool hasOther = containsOtherLanguage(trimmedText);

        if (hasChinese && hasOther) {
            // [CRITICAL] 混合语言拆分逻辑：提取所有中文块作为标题
            QStringList chineseBlocks;
            QRegularExpressionMatchIterator i = chineseRegex.globalMatch(trimmedText);
            while (i.hasNext()) {
                chineseBlocks << i.next().captured();
            }
            title = chineseBlocks.join(" ").simplified();

            // 移除中文块后的剩余部分作为正文内容 (保留原有外语结构)
            QString remaining = trimmedText;
            remaining.replace(chineseRegex, " ");
            content = remaining.simplified();
            
            if (title.isEmpty()) title = "未命名灵感";
            if (content.isEmpty()) content = trimmedText;
        } else {
            // 单一语种：首行作为标题，全文作为内容
            QStringList lines = trimmedText.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
            if (!lines.isEmpty()) {
                title = lines[0].trimmed();
                if (title.length() > 60) title = title.left(57) + "...";
                content = trimmedText;
            } else {
                title = "新笔记";
                content = trimmedText;
            }
        }
    }

    /**
     * @brief 增强版配对拆分：支持偶数行配对、单行拆分及多行混合拆分
     */
    static QList<QPair<QString, QString>> smartSplitPairs(const QString& text) {
        qDebug() << "[StringUtils] 开始对文本进行智能拆分，长度:" << text.length();
        QList<QPair<QString, QString>> results;
        QStringList lines = text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts);
        if (lines.isEmpty()) {
            qDebug() << "[StringUtils] 文本为空或无有效行";
            return results;
        }

        // [NEW] 检测是否每一行本身就是混合双语（如：Thai Chinese）
        bool allLinesMixed = true;
        for (const QString& line : lines) {
            if (!(containsChinese(line) && containsOtherLanguage(line))) {
                allLinesMixed = false;
                break;
            }
        }

        // 如果每一行都是混合的，则按行独立创建笔记
        if (allLinesMixed && lines.size() > 1) {
            qDebug() << "[StringUtils] 检测到全行混合模式，按行拆分，总行数:" << lines.size();
            for (const QString& line : lines) {
                QString t, c;
                smartSplitLanguage(line, t, c);
                results.append({t, c});
            }
            return results;
        }

        // 偶数行配对拆分：每两行为一组，中文优先级策略
        if (lines.size() > 1 && lines.size() % 2 == 0) {
            qDebug() << "[StringUtils] 检测到偶数行，尝试配对模式，对数:" << lines.size() / 2;
            for (int i = 0; i < lines.size(); i += 2) {
                QString line1 = lines[i].trimmed();
                QString line2 = lines[i+1].trimmed();
                
                bool c1 = containsChinese(line1);
                bool c2 = containsChinese(line2);
                
                if (c1 && !c2) {
                    results.append({line1, line2});
                } else if (!c1 && c2) {
                    results.append({line2, line1});
                } else {
                    results.append({line1, line2});
                }
            }
        } else {
            // 单文本块或奇数行：使用智能拆分逻辑
            qDebug() << "[StringUtils] 奇数行或单行，执行智能语言拆分";
            QString title, content;
            smartSplitLanguage(text, title, content);
            results.append({title, content});
        }
        
        return results;
    }

public:
    static bool isRichText(const QString& text) {
        if (text.isEmpty()) return false;
        
        // [PERF] 极致性能优化：优先通过前置特征快速判定，避免 Qt::mightBeRichText 的全量正则扫描。
        // 针对不同长度的文本采用分级探测策略。
        int len = text.length();
        if (len > 100000) {
            // 超大文本：仅检测前部特征及末尾闭合标签
            QStringView start = QStringView(text).left(1024);
            if (start.contains(u"<!DOCTYPE", Qt::CaseInsensitive) || start.contains(u"<html", Qt::CaseInsensitive) || 
                start.contains(u"<body", Qt::CaseInsensitive) || start.contains(u"<div", Qt::CaseInsensitive)) return true;
            return text.endsWith(u"</html>") || text.endsWith(u"</div>");
        }

        // [USER_REQUEST] 增加更多常见的富文本关键字探测
        if (text.startsWith(u"<!DOCTYPE", Qt::CaseInsensitive) || text.startsWith(u"<html", Qt::CaseInsensitive)) return true;
        
        // 只有当简单判断失效时，才调用开销较大的 mightBeRichText
        return Qt::mightBeRichText(text);
    }

    static bool isHtml(const QString& text) {
        return isRichText(text);
    }

    static QString htmlToPlainText(const QString& html) {
        if (!isHtml(html)) return html;
        QTextDocument doc;
        doc.setHtml(html);
        return doc.toPlainText();
    }

    static void copyNoteToClipboard(const QString& content) {
        ClipboardMonitor::instance().skipNext();
        QMimeData* mimeData = new QMimeData();
        if (isHtml(content)) {
            mimeData->setHtml(content);
            mimeData->setText(htmlToPlainText(content));
        } else {
            mimeData->setText(content);
        }
        QApplication::clipboard()->setMimeData(mimeData);
    }

    /**
     * @brief [NEW] 2026-03-11 按照用户要求，重构复制逻辑：复制内容优先策略，排除标题。
     * 支持根据 item_type 智能选择复制文本、图片或文件 URL。
     */
    static void copyNotesToClipboard(const QList<QVariantMap>& notes) {
        // [MODIFIED] 2026-03-11 按照用户要求，重构复制逻辑：内容优先，绝对排除标题。
        if (notes.isEmpty()) return;

        if (notes.size() == 1) {
            const QVariantMap& note = notes.first();
            QString type = note.value("item_type").toString();
            QString content = note.value("content").toString();
            QByteArray blob = note.value("data_blob").toByteArray();

            // 1. 图片类型：直接复制二进制图
            if (type == "image") {
                QImage img;
                img.loadFromData(blob);
                if (!img.isNull()) {
                    ClipboardMonitor::instance().skipNext();
                    QApplication::clipboard()->setImage(img);
                    return;
                }
            }

            // 2. 文件/文件夹/路径相关类型：复制为文件 URL (支持粘贴到资源管理器或聊天软件)
            bool isFilePath = (type == "file" || type == "local_file" || type == "folder" || type == "local_folder" || type == "local_batch");
            QString plainContent = htmlToPlainText(content).trimmed();
            if (!isFilePath) {
                // 智能检测文本是否为存在的物理路径
                if (QFileInfo(plainContent).exists() && QFileInfo(plainContent).isAbsolute()) {
                    isFilePath = true;
                    content = plainContent;
                }
            }

            if (isFilePath) {
                QStringList rawPaths;
                if (type == "local_batch") {
                    QString fullPath = content;
                    if (content.startsWith("attachments/")) {
                        fullPath = QCoreApplication::applicationDirPath() + "/" + content;
                    }
                    QDir dir(fullPath);
                    for (const QString& fileName : dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                        rawPaths << dir.absoluteFilePath(fileName);
                    }
                } else {
                    rawPaths << content;
                }

                QList<QUrl> urls;
                for (const QString& p : rawPaths) {
                    QString fullPath = p;
                    if (p.startsWith("attachments/")) {
                        fullPath = QCoreApplication::applicationDirPath() + "/" + p;
                    }
                    if (QFileInfo::exists(fullPath)) {
                        urls << QUrl::fromLocalFile(fullPath);
                    }
                }

                if (!urls.isEmpty()) {
                    ClipboardMonitor::instance().skipNext();
                    QMimeData* mimeData = new QMimeData();
                    mimeData->setUrls(urls);
                    QApplication::clipboard()->setMimeData(mimeData);
                    return;
                }
            }
            
            // 3. 其他情况均复制正文文本（确保排除标题并转换 HTML）
            // 兼容 ocr_text, captured_message 等类型
            copyNoteToClipboard(content);
        } else {
            // 多选模式：统一提取所有正文文本并合并 (绝对排除标题)
            QStringList texts;
            for (const auto& note : notes) {
                QString c = note.value("content").toString();
                QString type = note.value("item_type").toString();
                if (type == "image") texts << "[截图]";
                else texts << htmlToPlainText(c);
            }
            ClipboardMonitor::instance().skipNext();
            QApplication::clipboard()->setText(texts.join("\n---\n"));
        }
    }

    /**
     * @brief 简繁转换 (利用 Windows 原生 API)
     * @param toSimplified true 为转简体，false 为转繁体
     */
    static QString convertChineseVariant(const QString& text, bool toSimplified) {
#ifdef Q_OS_WIN
        if (text.isEmpty()) return text;
        
        // 转换为宽字符
        std::wstring wstr = text.toStdWString();
        DWORD flags = toSimplified ? LCMAP_SIMPLIFIED_CHINESE : LCMAP_TRADITIONAL_CHINESE;
        
        // 第一次调用获取长度
        int size = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, wstr.c_str(), -1, NULL, 0, NULL, NULL, 0);
        if (size > 0) {
            std::vector<wchar_t> buffer(size);
            // 第二次调用执行转换
            LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, wstr.c_str(), -1, buffer.data(), size, NULL, NULL, 0);
            return QString::fromWCharArray(buffer.data());
        }
#endif
        return text;
    }

    /**
     * @brief 记录最近访问或使用的分类
     */
    static void recordRecentCategory(int catId) {
        if (catId <= 0) return;
        QSettings settings("RapidNotes", "QuickWindow");
        QVariantList recentCats = settings.value("recentCategories").toList();
        
        // 转换为 int 列表方便操作
        QList<int> ids;
        for(const auto& v : recentCats) ids << v.toInt();
        
        ids.removeAll(catId);
        ids.prepend(catId);
        
        // 限制为最近 10 个
        while (ids.size() > 10) ids.removeLast();
        
        QVariantList result;
        for(int id : ids) result << id;
        settings.setValue("recentCategories", result);
        settings.sync();
    }

    /**
     * @brief 获取最近访问或使用的分类 ID 列表
     */
    static QVariantList getRecentCategories() {
        QSettings settings("RapidNotes", "QuickWindow");
        return settings.value("recentCategories").toList();
    }

    /**
     * [CRITICAL] 统一笔记预览 HTML 生成逻辑。
     * 1. 此函数为 MainWindow 预览卡片与 QuickPreview (空格预览) 的 Single Source of Truth。
     * 2. 若标题、内容、数据均为空，必须返回空字符串以消除视觉分割线。
     * 3. 修改此函数将同步影响全局预览效果，请务必保持两者视觉高度统一。
     */
    static QString generateNotePreviewHtml(const QString& title, const QString& content, const QString& type, const QByteArray& data, double zoomFactor = 1.0) {
        if (title.isEmpty() && content.isEmpty() && data.isEmpty()) return "";

        // [UX] 使用 em 相对单位以确保标题与正文缩放比例恒定。
        // zoomFactor 仅用于非字体属性（如图片宽度）的缩放。
        QString titleHtml = QString("<h3 style='color: #eee; margin-bottom: 5px; font-size: 1.35em;'>%1</h3>")
                            .arg(title.toHtmlEscaped());
        QString hrHtml = "<hr style='border: 0; border-top: 1px solid #444; margin: 10px 0;'>";
        QString html;

        if (type == "color") {
            int colorRectHeight = (int)(200 * zoomFactor);
            html = QString("%1%2"
                           "<div style='margin: 20px; text-align: center;'>"
                           "  <div style='background-color: %3; width: 100%; height: %4px; border-radius: 12px; border: 1px solid #555;'></div>"
                           "  <h1 style='color: white; margin-top: 20px; font-family: Consolas; font-size: 2.5em;'>%3</h1>"
                           "</div>")
                   .arg(titleHtml, hrHtml, content).arg(colorRectHeight);
        } else if (type == "image" && !data.isEmpty()) {
            // [OPTIMIZED] 动态计算图片预览宽度
            int imgWidth = (int)(450 * zoomFactor);
            html = QString("%1%2<div style='text-align: center;'><img src='data:image/png;base64,%3' width='%4'></div>")
                   .arg(titleHtml, hrHtml, QString(data.toBase64())).arg(imgWidth);
        } else {
            QString body;
            const int MAX_PREVIEW_LENGTH = 150000; // 预览最大限制 15 万字符，超过此长度将导致 Qt 渲染卡顿
            
            bool isTruncated = false;
            QString processedContent = content;
            if (content.length() > MAX_PREVIEW_LENGTH) {
                processedContent = content.left(MAX_PREVIEW_LENGTH);
                isTruncated = true;
            }

            // [FIX] 使用 1.0em 相对单位。由于 QuickPreview 已安装 eventFilter 并手动调用了 zoomIn/zoomOut，
            // 基础字号已改变。使用 em 可以让 HTML 自动继承并保持标题/正文比例。
            if (isRichText(processedContent)) {
                body = QString("<div style='line-height: 1.6; color: #ccc; font-size: 1.0em;'>%1</div>")
                       .arg(processedContent);
            } else {
                body = processedContent.toHtmlEscaped();
                body.replace("\n", "<br>");
                body = QString("<div style='line-height: 1.6; color: #ccc; font-size: 1.0em;'>%1</div>")
                       .arg(body);
            }

            if (isTruncated) {
                body += "<div style='margin-top: 20px; padding: 15px; background: #332211; border: 1px dashed #664422; color: #ffa500; border-radius: 6px; font-weight: bold;'>"
                        "[!] 内容过长（共 " + QString::number(content.length()) + " 字符），预览仅显示前 " + QString::number(MAX_PREVIEW_LENGTH) + " 字符。<br>"
                        "请按下 [Ctrl+B] 进入全量编辑模式查看完整数据。</div>";
            }

            html = QString("%1%2%3").arg(titleHtml, hrHtml, body);
        }
        return html;
    }

    /**
     * @brief 提取第一个网址，支持自动补全协议头
     */
    static QString extractFirstUrl(const QString& text) {
        if (text.isEmpty()) return "";
        // 支持识别纯文本或 HTML 中的 URL
        QString plainText = text.contains("<") ? htmlToPlainText(text) : text;
        static QRegularExpression urlRegex(R"((https?://[^\s<>"]+|www\.[^\s<>"]+))", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = urlRegex.match(plainText);
        if (match.hasMatch()) {
            QString url = match.captured(1);
            if (url.startsWith("www.", Qt::CaseInsensitive)) url = "http://" + url;
            return url;
        }
        return "";
    }

    /**
     * @brief [NEW] 从 MimeData 中健壮地提取本地文件路径，支持 URL 列表和文本形式的 file:/// 链接
     */
    static QStringList extractLocalPathsFromMime(const QMimeData* mime) {
        QStringList paths;
        if (mime->hasUrls()) {
            for (const QUrl& url : mime->urls()) {
                if (url.isLocalFile()) {
                    paths << QDir::toNativeSeparators(url.toLocalFile());
                } else {
                    // 处理可能带有 file:/// 但未被 Qt 识别为 localFile 的情况 (如特殊字符未转码)
                    QString s = url.toString();
                    if (s.startsWith("file:///")) {
                        paths << QDir::toNativeSeparators(QUrl(s).toLocalFile());
                    }
                }
            }
        }
        
        // 如果 Urls 为空，尝试从 Text 中提取 (处理某些应用只提供文本形式路径的情况)
        if (paths.isEmpty() && mime->hasText()) {
            QString text = mime->text().trimmed();
            // 处理单行 file:///
            if (text.startsWith("file:///")) {
                paths << QDir::toNativeSeparators(QUrl(text).toLocalFile());
            } else {
                // 处理可能是物理绝对路径的情况
                QFileInfo info(text);
                if (info.exists() && info.isAbsolute()) {
                    paths << QDir::toNativeSeparators(text);
                }
            }
        }
        return paths;
    }

    /**
     * @brief [NEW] 启用 WS_MINIMIZEBOX 以支持任务栏最小化，启用 WS_THICKFRAME 以允许 Windows 响应 NCHITTEST 缩放指令
     */
    static void applyTaskbarMinimizeStyle(void* winId) {
#ifdef Q_OS_WIN
        HWND hwnd = (HWND)winId;
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        // [CRITICAL] 必须包含 WS_THICKFRAME (即 WS_SIZEBOX)，否则系统会忽略 WM_NCHITTEST 返回的 HTLEFT/HTRIGHT 等缩放指令
        SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME);
#endif
    }

    /**
     * @brief 在资源管理器中定位路径，支持预处理
     */
    /**
     * @brief [NEW] 2026-03-xx 统一类型检测逻辑。
     * 整合分散在 main.cpp 和 NoteModel 中的识别规则。
     */
    static QString detectItemType(const QString& text) {
        QString stripped = text.trimmed();
        QString plain = htmlToPlainText(text).trimmed();

        if (stripped.startsWith("http://") || stripped.startsWith("https://") || stripped.startsWith("www.")) {
            return "link";
        }
        
        static const QRegularExpression hexColorRegex("^#([A-Fa-f0-9]{6}|[A-Fa-f0-9]{3})$");
        if (hexColorRegex.match(plain).hasMatch()) {
            return "color";
        }

        if (plain.startsWith("import ") || plain.startsWith("class ") || 
            plain.startsWith("def ") || plain.startsWith("function") || 
            plain.startsWith("var ") || plain.startsWith("const ") ||
            (plain.startsWith("<") && [](){
                static const QRegularExpression htmlTagRegex("^<(html|div|p|span|table|h[1-6]|script|!DOCTYPE|body|head)", QRegularExpression::CaseInsensitiveOption);
                return htmlTagRegex;
            }().match(plain).hasMatch()) ||
            (plain.startsWith("{") && plain.contains("\":") && plain.contains("}"))) {
            return "code";
        }

        // 检测路径
        QString cleanPath = stripped;
        if ((cleanPath.startsWith("\"") && cleanPath.endsWith("\"")) || 
            (cleanPath.startsWith("'") && cleanPath.endsWith("'"))) {
            cleanPath = cleanPath.mid(1, cleanPath.length() - 2);
        }
        if (cleanPath.length() < 260 && (
            (cleanPath.length() > 2 && cleanPath[1] == ':') || 
            cleanPath.startsWith("\\\\") || cleanPath.startsWith("/") || 
            cleanPath.startsWith("./") || cleanPath.startsWith("../"))) {
            QFileInfo info(cleanPath);
            if (info.exists()) {
                return info.isDir() ? "folder" : "file";
            }
        }

        return "text";
    }

    static void locateInExplorer(const QString& path, bool select = true) {
#ifdef Q_OS_WIN
        if (path.isEmpty()) return;
        // 使用 QUrl::fromUserInput 处理包含 file:/// 协议或 URL 编码字符的路径
        QString localPath = QUrl::fromUserInput(path).toLocalFile();
        if (localPath.isEmpty()) localPath = path;
        // 统一转换为系统原生路径格式
        localPath = QDir::toNativeSeparators(localPath);
        
        QStringList args;
        if (select) {
            args << "/select," << localPath;
        } else {
            args << localPath;
        }
        QProcess::startDetached("explorer.exe", args);
#endif
    }
};

#endif // STRINGUTILS_H
