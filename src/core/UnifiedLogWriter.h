#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <functional>

namespace pat {

struct UnifiedLogWriteOptions {
    qsizetype targetChunkBytes = 1024 * 1024;
    qsizetype maxBufferedBytes = 32 * 1024 * 1024;
    QString timestampUnit = QStringLiteral("tick");
    double timestampScale = 0.0;  // Positive values are written to JSON; 0 keeps raw ticks.
    std::function<bool()> isCancelled;
};

struct UnifiedLogWriteResult {
    QString binaryPath;
    QString jsonPath;
    quint64 sourceRecordCount = 0;
    quint64 routedRecordCount = 0;
    quint64 metadataRecordCount = 0;
    quint64 terminalMarkerBytes = 0;
    quint64 trailingFillBytes = 0;
    quint64 discardedTailRecords = 0;
    quint64 discardedTailBytes = 0;
    quint64 chunkCount = 0;
    quint64 outputBytes = 0;
    qsizetype peakBufferedBytes = 0;
    int streamCount = 0;
};

class UnifiedLogWriter {
public:
    bool ConvertVmcFiles(const QStringList& sourcePaths,
                         const QString& binaryPath,
                         const QString& jsonPath,
                         const UnifiedLogWriteOptions& options,
                         UnifiedLogWriteResult& outResult,
                         QString& errorMessage) const;
};

}  // namespace pat
