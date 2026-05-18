#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QTcpServer>
#include <QObject>

class HttpServer : public QTcpServer {
    Q_OBJECT
public:
    static HttpServer& instance();
    bool start(quint16 port = 23333);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer() = default;
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
};

#endif // HTTPSERVER_H
