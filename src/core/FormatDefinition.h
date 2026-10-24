#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtGlobal>
#include <vector>

namespace pat {

struct SignalFormat {
    QString name;
    // UI-facing name without the stream prefix used to keep schema 3 signals unique.
    QString displayName;
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

    QString DisplayName() const {
        const QString local = displayName.trimmed();
        return local.isEmpty() ? name : local;
    }
    QString QualifiedDisplayName() const {
        const QString group = groupPath.trimmed();
        const QString local = DisplayName();
        return group.isEmpty() ? local : QStringLiteral("%1 - %2").arg(group, local);
    }
};

struct StreamFormat {
    quint32 streamId = 0;
    QString sourceMessageId;
    QString name;
    int payloadLength = 0;
    QString endianness = QStringLiteral("big");
    int firstSignalIndex = 0;
    int signalCount = 0;
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

    // schema_version 3 keeps each source message as an independently indexed stream.
    QString storageKind;
    int storageVersion = 0;
    QString layoutSha256;
    QString dataFile;
    QString timestampValueType;
    QString timestampUnit;
    double timestampScale = 1.0;
    bool timestampScaleKnown = false;
    std::vector<StreamFormat> streamFormats;

    bool HasFraming() const { return !frameHeader.isEmpty(); }
    bool HasSum8Checksum() const { return checksumByteOffset >= 0; }
    bool IsIndexedMixed() const {
        return schemaVersion == 3 && storageKind == QStringLiteral("indexed_stream_chunks");
    }
};

bool LoadFormatFromJson(const QString& path, FormatDefinition& outFormat, QString& errorMessage);
bool LoadFormatFromJsonData(const QByteArray& data, FormatDefinition& outFormat, QString& errorMessage);

}  // namespace pat
