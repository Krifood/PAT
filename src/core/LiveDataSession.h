#pragma once

#include "core/DataSession.h"

namespace pat {

class LiveDataSession {
public:
    void Reset(const FormatDefinition& format, int maxSamplesPerSignal = 30000);
    bool Append(quint64 sampleIndex, const QVector<double>& values);
    void UpdateStatistics();

    const QVector<Series>& SeriesData() const { return series_; }
    const SeriesStatistics& Statistics() const { return statistics_; }
    bool HasData() const { return hasData_; }

private:
    QVector<Series> series_;
    SeriesStatistics statistics_;
    int maxSamplesPerSignal_ = 30000;
    bool hasData_ = false;
};

}  // namespace pat
