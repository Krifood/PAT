#pragma once

#include "core/RecordDecoder.h"
#include "core/SerialFrameAssembler.h"

#include <QFile>
#include <QVector>

#include <memory>

namespace pat {

struct LiveSample {
    quint64 index = 0;
    QVector<double> values;
};

class CaptureSession {
public:
    explicit CaptureSession(FormatDefinition format);

    bool Start(const QString& logDirectory, QString& errorMessage);
    bool Feed(const QByteArray& bytes, QVector<LiveSample>& samples, QString& errorMessage);
    bool Stop(QString& errorMessage);

    const QString& RawPath() const { return rawPath_; }
    const QString& CsvPath() const { return csvPath_; }
    quint64 FrameCount() const { return nextSampleIndex_; }
    quint64 SkippedBytes() const { return assembler_.SkippedBytes(); }

private:
    FormatDefinition format_;
    RecordDecoder decoder_;
    SerialFrameAssembler assembler_;
    std::unique_ptr<QFile> rawLog_;
    std::unique_ptr<QFile> csvLog_;
    QString rawPath_;
    QString csvPath_;
    quint64 nextSampleIndex_ = 0;
    bool started_ = false;
};

}  // namespace pat

Q_DECLARE_METATYPE(pat::LiveSample)
Q_DECLARE_METATYPE(QVector<pat::LiveSample>)
