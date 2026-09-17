#pragma once

#include "core/FormatDefinition.h"

#include <QVector>

namespace pat {

struct MatchedFrame {
    int templateIndex = -1;
    QByteArray bytes;
};

class SerialFrameAssembler {
public:
    explicit SerialFrameAssembler(QVector<FormatDefinition> formats);

    QVector<MatchedFrame> Feed(const QByteArray& bytes);
    void Reset();

    quint64 SkippedBytes() const { return skippedBytes_; }
    qsizetype PendingBytes() const { return pending_.size(); }
    const QString& ConfigurationError() const { return configurationError_; }

private:
    QVector<FormatDefinition> formats_;
    QByteArray pending_;
    QString configurationError_;
    quint64 skippedBytes_ = 0;
    int longestHeader_ = 0;
};

}  // namespace pat
