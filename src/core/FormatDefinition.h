#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <vector>

namespace pat {

struct SignalFormat {
    QString name;
    int byteOffset = 0;
    QString valueType;  // int16, uint16, int32, uint32, float32, float64
    double scale = 1.0;
    double bias = 0.0;
    double timeScale = 1.0;
    QString timeUnit = QStringLiteral("s");
    QString unit;
    QString description;
    QString groupPath;
};

struct FormatDefinition {
    int schemaVersion = 1;
    QString templateId;
    int recordSize = 0;
    QString endianness = QStringLiteral("little");
    QByteArray frameHeader;
    std::vector<SignalFormat> signalFormats;
    QHash<QString, QString> groupDescriptions;
    QString timeAxisUnit = QStringLiteral("s");

    bool HasFraming() const { return !frameHeader.isEmpty(); }
};

bool LoadFormatFromJson(const QString& path, FormatDefinition& outFormat, QString& errorMessage);
bool LoadFormatFromJsonData(const QByteArray& data, FormatDefinition& outFormat, QString& errorMessage);

}  // namespace pat
