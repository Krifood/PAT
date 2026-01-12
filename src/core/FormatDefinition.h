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
    int recordSize = 0;
    QString endianness = QStringLiteral("little");
    std::vector<SignalFormat> signalFormats;
    QHash<QString, QString> groupDescriptions;
    QString timeAxisUnit = QStringLiteral("s");
};

bool LoadFormatFromJson(const QString& path, FormatDefinition& outFormat, QString& errorMessage);
bool LoadFormatFromJsonData(const QByteArray& data, FormatDefinition& outFormat, QString& errorMessage);

}  // namespace pat
