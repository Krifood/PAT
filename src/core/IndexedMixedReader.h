#pragma once

#include "core/FormatDefinition.h"
#include "core/RecordParser.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <functional>
#include <limits>

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

struct MixedAlignment {
    QVector<int> indices;
    int operation = -1;
    bool interpolate = false;
    bool holdPrevious = false;
    bool operator==(const MixedAlignment&) const = default;
};

struct MixedReadWindow {
    double minX = 0.0;
    double maxX = std::numeric_limits<double>::infinity();
    bool operator==(const MixedReadWindow&) const = default;
};

struct MixedSignalStatistics {
    quint64 count = 0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
};

struct MixedReadResult {
    QVector<Series> series;
    Series relation;
    quint64 decodedRecords = 0;
    quint64 readChunks = 0;
    quint64 readBytes = 0;
    bool reduced = false;
    QVector<MixedSignalStatistics> statistics;
    quint64 retainedPoints = 0;
    qsizetype cacheBudgetBytes = 0;
};

class IndexedMixedReader {
public:
    static bool SupportsFixedFile(const QString& path, const FormatDefinition& format);
    bool Open(const QString& path, const FormatDefinition& format, QString& errorMessage);
    void Close();

    bool ReadSignals(const QVector<int>& signalIndices,
                     qsizetype cacheBudgetBytes,
                     MixedReadResult& outResult,
                     QString& errorMessage,
                     const MixedReadWindow& window = {},
                     const std::function<bool()>& isCancelled = {}) const;

    // Exact timestamp join BEFORE reduction; operation: -1 coordinates only,
    // 0 difference, 1 sum. At most three source columns per query.
    // interpolate=true evaluates B at A's time using same-segment brackets;
    // no extrapolation. Arithmetic and interpolation both precede reduction.
    bool ReadAlignedSignals(const QVector<int>& indices, qsizetype budget,
                            MixedReadResult& result, QString& error,
                            const MixedReadWindow& window = {},
                            const std::function<bool()>& cancelled = {}, int operation = -1,
                            bool interpolate = false, bool holdPrevious = false) const;

    bool IsOpen() const { return !path_.isEmpty(); }
    const QString& Path() const { return path_; }
    const QVector<MixedChunkIndex>& Index() const { return index_; }
    quint64 RecordCount() const;
    double MaxX() const { return maxX_; }

private:
    bool OpenFixed(const QString& path, const FormatDefinition& format, QString& error);
    QString path_;
    FormatDefinition format_;
    QVector<MixedChunkIndex> index_;
    QByteArray layoutHash_;
    QHash<quint32, quint64> segmentMin_;
    QHash<quint32, double> segmentOffset_;
    double maxX_ = 0.0;
    quint64 fixedRecords_ = 0;
    QHash<quint32, double> fixedScales_;
};

}  // namespace pat
