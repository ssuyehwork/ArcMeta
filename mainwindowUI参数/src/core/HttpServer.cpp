#include "HttpServer.h"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QDebug>
#include "DatabaseManager.h"
#include "../ui/StringUtils.h"

HttpServer& HttpServer::instance() {
    static HttpServer inst;
    return inst;
}

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent) {}

bool HttpServer::start(quint16 port) {
    if (isListening()) return true;
    bool ok = listen(QHostAddress::LocalHost, port);
    if (!ok) {
        qWarning() << "[HttpServer] 服务启动失败:" << errorString();
    }
    return ok;
}

void HttpServer::incomingConnection(qintptr socketDescriptor) {
    QTcpSocket *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        delete socket;
        return;
    }

    // [FIX] 使用 QObject 包装 QByteArray 以利用 deleteLater 安全释放内存，防止 Use-After-Free 导致的闪退
    class BufferHolder : public QObject { public: QByteArray data; };
    BufferHolder* holder = new BufferHolder();
    holder->setParent(socket);

    connect(socket, &QTcpSocket::readyRead, [this, socket, holder]() {
        QByteArray& dataBuffer = holder->data;
        // [SECURITY] 限制总接收缓冲区大小，防止恶意连接耗尽内存导致闪退
        if (dataBuffer.size() > 12 * 1024 * 1024) {
            qWarning() << "[HttpServer] 缓冲区溢出，强制断开连接";
            socket->disconnectFromHost();
            return;
        }

        dataBuffer.append(socket->readAll());
        
        if (dataBuffer.contains("\r\n\r\n")) {
            // 处理 OPTIONS 预检请求 (CORS)
            if (dataBuffer.contains("OPTIONS")) {
                socket->write("HTTP/1.1 204 No Content\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                              "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                              "Access-Control-Max-Age: 86400\r\n"
                              "\r\n");
                socket->flush();
                socket->disconnectFromHost();
                dataBuffer.clear();
                return;
            }

            if (dataBuffer.contains("GET /get_extension_config")) {
                int targetId = DatabaseManager::instance().extensionTargetCategoryId();
                QString catName = DatabaseManager::instance().getCategoryNameById(targetId);
                
                QJsonObject resp;
                resp["targetCategoryId"] = targetId;
                resp["targetCategoryName"] = catName;
                
                QByteArray json = QJsonDocument(resp).toJson(QJsonDocument::Compact);
                socket->write("HTTP/1.1 200 OK\r\n"
                              "Access-Control-Allow-Origin: *\r\n"
                              "Content-Type: application/json\r\n"
                              "Connection: close\r\n"
                              "\r\n" + json);
                socket->flush();
                socket->disconnectFromHost();
                dataBuffer.clear();
                return;
            }

            // [USER_REQUEST] 统一 API 响应分发逻辑：区分全权限 (/api/full/) 与只读权限 (/api/read/) 接口。
            
            // 提取第一行请求行
            int lineEnd = dataBuffer.indexOf("\r\n");
            QByteArray requestLine = dataBuffer.left(lineEnd);
            QString reqStr = QString::fromUtf8(requestLine);
            
            auto sendJsonResponse = [&](const QJsonObject& obj, int code = 200) {
                QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
                QString header = QString("HTTP/1.1 %1 %2\r\n"
                                         "Access-Control-Allow-Origin: *\r\n"
                                         "Content-Type: application/json\r\n"
                                         "Connection: close\r\n"
                                         "\r\n").arg(code).arg(code == 200 ? "OK" : "Error");
                socket->write(header.toUtf8() + json);
                socket->flush();
                socket->disconnectFromHost();
                dataBuffer.clear();
            };

            // 1. 处理只读查询接口 (Read-only Access)
            if (reqStr.contains("GET /api/read/search") || reqStr.contains("GET /api/full/search")) {
                QUrlQuery query(reqStr.split(' ')[1].split('?').value(1));
                QString keyword = query.queryItemValue("q");
                int page = query.queryItemValue("page").toInt();
                if (page < 1) page = 1;
                
                QList<QVariantMap> notes = DatabaseManager::instance().searchNotes(keyword, "all", -1, page, 50);
                
                QJsonArray arr;
                for (const auto& note : notes) {
                    QJsonObject item;
                    item["id"] = note["id"].toInt();
                    item["title"] = note["title"].toString();
                    item["content"] = note["content"].toString();
                    item["tags"] = note["tags"].toString();
                    item["item_type"] = note["item_type"].toString();
                    item["created_at"] = note["created_at"].toDateTime().toString(Qt::ISODate);
                    arr.append(item);
                }
                
                QJsonObject resp;
                resp["status"] = "success";
                resp["data"] = arr;
                resp["page"] = page;
                sendJsonResponse(resp);
                return;
            }

            if (reqStr.contains("GET /api/read/get") || reqStr.contains("GET /api/full/get")) {
                QUrlQuery query(reqStr.split(' ')[1].split('?').value(1));
                int id = query.queryItemValue("id").toInt();
                QVariantMap note = DatabaseManager::instance().getNoteById(id);
                
                if (note.isEmpty()) {
                    QJsonObject err; err["status"] = "error"; err["message"] = "note not found";
                    sendJsonResponse(err, 404);
                } else {
                    QJsonObject item;
                    item["id"] = note["id"].toInt();
                    item["title"] = note["title"].toString();
                    item["content"] = note["content"].toString();
                    item["tags"] = note["tags"].toString();
                    item["item_type"] = note["item_type"].toString();
                    item["created_at"] = note["created_at"].toDateTime().toString(Qt::ISODate);
                    
                    QJsonObject resp;
                    resp["status"] = "success";
                    resp["data"] = item;
                    sendJsonResponse(resp);
                }
                return;
            }

            if (dataBuffer.contains("POST /add_note") || dataBuffer.contains("POST /api/full/add") || 
                dataBuffer.contains("POST /api/full/update") || dataBuffer.contains("POST /api/full/delete")) {
                
                qDebug() << "[HttpServer] 收到 POST 写入请求:" << reqStr;
                int headerEndIndex = dataBuffer.indexOf("\r\n\r\n");
                QByteArray headers = dataBuffer.left(headerEndIndex);
                int bodyIndex = headerEndIndex + 4;

                // 获取 Content-Length (更加健壮的解析)
                int contentLength = 0;
                QList<QByteArray> headerLines = headers.split('\n');
                for (const auto& line : headerLines) {
                    QByteArray trimmedLine = line.trimmed().toLower();
                    if (trimmedLine.startsWith("content-length:")) {
                        contentLength = trimmedLine.mid(15).trimmed().toInt();
                        break;
                    }
                }

                // [SECURITY] 防御性校验：限制 Body 最大长度为 10MB，防止 OOM 闪退
                if (contentLength > 10 * 1024 * 1024) {
                    qWarning() << "[HttpServer] 拒绝超大数据包:" << contentLength;
                    socket->write("HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                    dataBuffer.clear();
                    return;
                }

                if (dataBuffer.size() < bodyIndex + contentLength) {
                    // [SECURITY] 如果缓冲区堆积过大（即使还没达到 contentLength），也应强制清理，防止缓慢攻击
                    if (dataBuffer.size() > 11 * 1024 * 1024) {
                        socket->disconnectFromHost();
                        dataBuffer.clear();
                    }
                    return; // 数据尚未接收完整
                }

                QByteArray body = dataBuffer.mid(bodyIndex, contentLength);
                
                // 尝试解析 JSON
                QJsonParseError err;
                QJsonDocument doc = QJsonDocument::fromJson(body, &err);
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject obj = doc.object();
                    
                    // A. 处理新增 (Add)
                    if (reqStr.contains("/add") || reqStr.contains("/add_note")) {
                        QString rawContent = obj.value("content").toString();
                        QString url = obj.value("url").toString();
                        QString pageTitle = obj.value("pageTitle").toString();
                        
                        QString title = rawContent.trimmed().left(40).replace("\r", " ").replace("\n", " ").simplified();
                        if (title.isEmpty()) title = pageTitle.isEmpty() ? "未命名灵感" : pageTitle;

                        QString content = rawContent;
                        if (!url.isEmpty() && !content.contains(url)) {
                            content += "\n\n内容来源：- " + url;
                        }

                        DatabaseManager::instance().beginBatch();
                        QStringList tags = {"插件采集"};
                        if (StringUtils::containsThai(rawContent)) tags << "泰文";
                        int targetCatId = DatabaseManager::instance().extensionTargetCategoryId();
                        
                        ClipboardMonitor::instance().setIgnore(true);
                        int noteId = DatabaseManager::instance().addNote(title, content, tags, "", targetCatId, "text", QByteArray(), "Browser", pageTitle);
                        
                        QTimer::singleShot(800, [](){ ClipboardMonitor::instance().setIgnore(false); });
                        DatabaseManager::instance().endBatch();
                        
                        QJsonObject resp; resp["status"] = "success"; resp["id"] = noteId;
                        sendJsonResponse(resp);
                        return;
                    }
                    
                    // B. 处理更新 (Update)
                    if (reqStr.contains("/update")) {
                        int id = obj.value("id").toInt();
                        QString title = obj.value("title").toString();
                        QString content = obj.value("content").toString();
                        QStringList tags;
                        QJsonArray tagsArr = obj.value("tags").toArray();
                        for(auto v : tagsArr) tags << v.toString();

                        if (id > 0 && DatabaseManager::instance().updateNote(id, title, content, tags)) {
                            QJsonObject resp; resp["status"] = "success";
                            sendJsonResponse(resp);
                        } else {
                            QJsonObject resp; resp["status"] = "error"; resp["message"] = "update failed";
                            sendJsonResponse(resp, 400);
                        }
                        return;
                    }

                    // C. 处理删除 (Delete)
                    if (reqStr.contains("/delete")) {
                        QList<int> ids;
                        QJsonArray idsArr = obj.value("ids").toArray();
                        for(auto v : idsArr) ids << v.toInt();

                        if (!ids.isEmpty() && DatabaseManager::instance().deleteNotesBatch(ids)) {
                            QJsonObject resp; resp["status"] = "success";
                            sendJsonResponse(resp);
                        } else {
                            QJsonObject resp; resp["status"] = "error"; resp["message"] = "delete failed";
                            sendJsonResponse(resp, 400);
                        }
                        return;
                    }

                } else if (err.error != QJsonParseError::NoError) {
                    // [NEW] 增强鲁棒性：如果 JSON 解析失败且数据已接收一定长度，则清理并报错，防止逻辑死锁
                    if (body.size() > 500) {
                        qWarning() << "[HttpServer] JSON 解析失败:" << err.errorString();
                        socket->write("HTTP/1.1 400 Bad Request\r\n"
                                      "Access-Control-Allow-Origin: *\r\n"
                                      "\r\n"
                                      "{\"status\":\"error\",\"message\":\"invalid json\"}");
                        socket->flush();
                        socket->disconnectFromHost();
                        dataBuffer.clear();
                    }
                }
            } else {
                socket->write("HTTP/1.1 404 Not Found\r\n\r\n");
                socket->disconnectFromHost();
            }
        }
    });

    connect(socket, &QTcpSocket::disconnected, [socket]() {
        socket->deleteLater();
    });
}
