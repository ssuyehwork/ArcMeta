#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <atomic>
#include "IndexedEntry.h"

namespace ArcMeta {

/**
 * @brief 核心中控类
 * 负责协调底层服务初始化、管理系统全局状态、并为 UI 提供异步通知接口。
 */
class CoreController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isIndexing READ isIndexing NOTIFY isIndexingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    static CoreController& instance();

    /**
     * @brief 启动异步初始化序列
     */
    void startSystem();

    bool isIndexing() const { return m_isIndexing; }
    QString statusText() const { return m_statusText; }
    int getCurrentSearchId() const { return m_currentSearchId; }

    /**
     * @brief 统一搜索接口
     * @param keyword 关键词
     * @param rootPath 锁定根路径（可选）
     * @return 匹配的文件路径列表
     */
    QStringList performSearch(const QString& keyword, const QString& rootPath = "");

signals:
    /**
     * @brief 异步搜索结果分批推送信号
     * @param searchId 搜索会话 ID
     * @param records 搜到的 ItemRecord 列表
     */
    void searchResultFound(int searchId, const std::vector<ItemRecord>& records);

    /**
     * @brief 异步搜索完成信号
     * @param searchId 搜索会话 ID
     */
    void searchFinished(int searchId);

    void isIndexingChanged(bool indexing);
    void statusTextChanged(const QString& text);
    void initializationFinished();

private:
    CoreController(QObject* parent = nullptr);
    ~CoreController() override;

    void setStatus(const QString& text, bool indexing);

    bool m_isIndexing = false;
    QString m_statusText = "就绪";
    std::atomic<int> m_currentSearchId{0};
};

} // namespace ArcMeta
