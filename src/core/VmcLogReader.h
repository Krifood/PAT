#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <functional>

namespace pat {

struct VmcFieldDefinition {
    QString name;
    QString sourceName;
    QString sourceType;
    QString valueType;
    int byteOffset = 0;
    int byteSize = 0;
};

struct VmcStreamDescriptor {
    quint8 sourceControl = 0;
    quint16 messageId = 0;
    QString topic;
    QByteArray sourceFormat;
    QVector<VmcFieldDefinition> fields;
    int payloadLength = 0;
    QByteArray structureSha256;
};

struct VmcSourceHeader {
    QByteArray globalHeader;
    QVector<VmcStreamDescriptor> descriptors;
    QHash<quint16, int> descriptorByMessageId;
    qint64 dataOffset = 0;
};

struct VmcRecord {
    quint8 sourceControl = 0;
    quint16 messageId = 0;
    quint64 timestamp = 0;
    quint64 sourceSequence = 0;
    QByteArray payload;
};

struct VmcReadSummary {
    quint64 recordCount = 0;
    quint64 routedRecordCount = 0;
    quint64 metadataRecordCount = 0;
    quint64 trailingFillBytes = 0;
};

class VmcLogReader {
public:
    using RecordVisitor = std::function<bool(const VmcRecord&, const VmcStreamDescriptor*)>;

    bool ReadHeader(const QString& path, VmcSourceHeader& outHeader,
                    QString& errorMessage) const;
    bool VisitRecords(const QString& path, const VmcSourceHeader& header,
                      const RecordVisitor& visitor, VmcReadSummary& outSummary,
                      QString& errorMessage) const;
};

}  // namespace pat
