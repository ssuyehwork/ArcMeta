#ifndef OCRMANAGER_H
#define OCRMANAGER_H
#include <QObject>
#include <QImage>
#include <QString>

class OCRManager : public QObject {
    Q_OBJECT
public:
    enum class EngineType {
        Tesseract,
        WindowsOCR,
        Unknown
    };

    static OCRManager& instance();
    void recognizeAsync(const QImage& image, int contextId = -1);
    
    // 设置 OCR 识别语言（默认: "chi_sim+eng"）
    // 可用语言见 traineddata 文件，多语言用 + 连接
    // 例如: "chi_sim+eng", "jpn+eng", "fra+deu"
    void setLanguage(const QString& lang);
    QString getLanguage() const;

    EngineType currentEngine() const { return m_engineType; }

private:
    void recognizeSync(const QImage& image, int contextId);
    QImage preprocessImage(const QImage& original);
    
    // 引擎特定实现
    QString recognizeWithTesseract(const QImage& image);
    QString recognizeWithWindowsOCR(const QImage& image);
    
    // 探测逻辑
    void detectAvailableEngine();
    QString findTesseractPath();

signals:
    void recognitionFinished(const QString& text, int contextId);

private:
    OCRManager(QObject* parent = nullptr);
    QString m_language = "chi_sim+eng"; // 默认中文简体+英文
    EngineType m_engineType = EngineType::Unknown;
    QString m_cachedTesseractPath;
};

#endif // OCRMANAGER_H