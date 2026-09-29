#pragma once

#include "core/DataSession.h"
#include "core/IndexedMixedReader.h"

#include <QVector>

namespace pat {

class MixedDataSession {
public:
    bool Open(const QString& path, const FormatDefinition& format, QString& errorMessage);
    bool LoadSignals(const QVector<int>& signalIndices, QString& errorMessage,
                     const MixedReadWindow& window = {},
                     const std::function<bool()>& isCancelled = {},
                     const QVector<MixedAlignment>& alignments = {});
    MixedDataSession MetadataSnapshot() const;
    const QVector<pat::Series>& Relations() const { return relations_; }
    quint64 Revision() const { return revision_; }
    const MixedReadResult& LastRead() const { return lastRead_; }
    void Clear();

    void SetCacheBudgetBytes(qsizetype bytes) { cacheBudgetBytes_ = bytes; }
    qsizetype CacheBudgetBytes() const { return cacheBudgetBytes_; }
    bool HasData() const { return reader_.IsOpen(); }
    const QVector<pat::Series>& Series() const { return series_; }
    const SeriesStatistics& Statistics() const { return statistics_; }
    const QString& Path() const { return reader_.Path(); }
    const QString& TimeUnit() const { return timeUnit_; }
    quint64 RecordCount() const { return reader_.RecordCount(); }
    quint64 RetainedPointCount() const { return retainedPointCount_; }
    const QVector<int>& LoadedSignalIndices() const { return loadedSignalIndices_; }

private:
    void ComputeStatistics();

    IndexedMixedReader reader_;
    QVector<pat::Series> series_;
    SeriesStatistics statistics_;
    QString timeUnit_;
    qsizetype cacheBudgetBytes_ = 128 * 1024 * 1024;
    quint64 retainedPointCount_ = 0;
    QVector<int> loadedSignalIndices_;
    MixedReadWindow loadedWindow_;
    qsizetype loadedBudget_ = 0;
    quint64 revision_ = 0;
    MixedReadResult lastRead_;
    QVector<MixedAlignment> loadedAlignments_;
    QVector<pat::Series> relations_;
};

}  // namespace pat
