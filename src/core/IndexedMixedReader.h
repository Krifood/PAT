#pragma once

#include "core/FormatDefinition.h"
#include "core/RecordParser.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace pat {

struct MixedChunkIndex {
    quint64 chunkOffset = 0;
    quint32 streamId = 0;
    quint32 segmentId = 0;
    quint64 firstTimestamp = 0;
    quint64 lastTimestamp = 0;
    quint64 firstSequence = 0;
    quint32 recordCount = 0;
    quint32 payloadSize = 0;
    quint32 dataSize = 0;
    quint32 flags = 0;
    quint32 crc32 = 0;
};

struct MixedReadResult {
    QVector<Series> series;
    quint64 decodedRecords = 0;
    quint64 retainedPoints = 0;
    qsizetype cacheBudgetBytes = 0;
};

class IndexedMixedReader {
public:
    bool Open(const QString& path, const FormatDefinition& format, QString& errorMessage);
    void Close();

    bool ReadSignals(const QVector<int>& signalIndices,
                     qsizetype cacheBudgetBytes,
                     MixedReadResult& outResult,
                     QString& errorMessage) const;

    bool IsOpen() const { return !path_.isEmpty(); }
    const QString& Path() const { return path_; }
    const QVector<MixedChunkIndex>& Index() const { return index_; }
    quint64 RecordCount() const;

private:
    QString path_;
    FormatDefinition format_;
    QVector<MixedChunkIndex> index_;
    QByteArray layoutHash_;
};

}  // namespace pat
