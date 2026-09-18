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
    int bitOffset = 0;  // Optional bit slice of an unsigned integer, bit 0 is the least significant bit.
    int bitWidth = 0;
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
    // Optional sum of inclusive byte range, stored as its low eight bits.
    int checksumStartByte = -1;
    int checksumEndByte = -1;
    int checksumByteOffset = -1;
    std::vector<SignalFormat> signalFormats;
    QHash<QString, QString> groupDescriptions;
    QString timeAxisUnit = QStringLiteral("s");

    bool HasFraming() const { return !frameHeader.isEmpty(); }
    bool HasSum8Checksum() const { return checksumByteOffset >= 0; }
};

bool LoadFormatFromJson(const QString& path, FormatDefinition& outFormat, QString& errorMessage);
bool LoadFormatFromJsonData(const QByteArray& data, FormatDefinition& outFormat, QString& errorMessage);

}  // namespace pat
