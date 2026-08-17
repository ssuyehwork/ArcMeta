#include "PresetManager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include "../core/AppConfig.h"

namespace ArcMeta {

QString PresetManager::serializeRules(const std::vector<RenameRule>& rules) {
    QJsonArray arr;
    for (const auto& rule : rules) {
        QJsonObject obj;
        QString typeStr;
        switch (rule.type) {
            case RenameComponentType::Text: typeStr = "Text"; break;
            case RenameComponentType::Sequence: typeStr = "Sequence"; break;
            case RenameComponentType::OriginalName: typeStr = "OriginalName"; break;
            case RenameComponentType::Date: typeStr = "Date"; break;
            case RenameComponentType::Metadata: typeStr = "Metadata"; break;
            default: typeStr = "Unknown";
        }
        obj["type"] = typeStr;
        obj["value"] = rule.value;
        obj["start"] = rule.start;
        obj["step"] = rule.step;
        obj["padding"] = rule.padding;
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

std::vector<RenameRule> PresetManager::deserializeRules(const QString& jsonStr) {
    std::vector<RenameRule> rules;
    if (jsonStr.isEmpty()) return rules;

    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    if (!doc.isArray()) return rules;

    QJsonArray arr = doc.array();
    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        RenameRule rule;
        QString typeStr = obj["type"].toString();
        if (typeStr == "Text") rule.type = RenameComponentType::Text;
        else if (typeStr == "Sequence") rule.type = RenameComponentType::Sequence;
        else if (typeStr == "OriginalName") rule.type = RenameComponentType::OriginalName;
        else if (typeStr == "Date") rule.type = RenameComponentType::Date;
        else if (typeStr == "Metadata") rule.type = RenameComponentType::Metadata;
        else continue;

        rule.value = obj["value"].toString();
        rule.start = obj["start"].toInt(1);
        rule.step = obj["step"].toInt(1);
        if (rule.step <= 0) rule.step = 1; // 容错
        rule.padding = obj["padding"].toInt(3);
        rules.push_back(rule);
    }
    return rules;
}

bool PresetManager::exportToFile(const QString& filePath, const std::vector<RenameRule>& rules) {
    if (filePath.isEmpty()) return false;
    QString jsonStr = serializeRules(rules);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(jsonStr.toUtf8());
        file.close();
        return true;
    }
    return false;
}

std::vector<RenameRule> PresetManager::importFromFile(const QString& filePath) {
    std::vector<RenameRule> rules;
    if (filePath.isEmpty()) return rules;
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
        QString jsonStr = QString::fromUtf8(file.readAll());
        file.close();
        return deserializeRules(jsonStr);
    }
    return rules;
}

} // namespace ArcMeta
