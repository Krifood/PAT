#pragma once

#include "core/RecordParser.h"

#include <QString>

namespace pat {

struct SeriesStatistics {
    double minY = -1.0;
    double maxY = 1.0;
    double maxX = 0.0;
    double minStep = 1e-3;
    bool hasRange = false;
};

class DataSession {
public:
    bool Load(const QString& path, const FormatDefinition& format, QString& errorMessage);
    void Clear();

    bool HasData() const { return hasData_; }
    const QVector<pat::Series>& Series() const { return series_; }
    const SeriesStatistics& Statistics() const { return statistics_; }
    const QString& Path() const { return path_; }
    const QString& TimeUnit() const { return timeUnit_; }

private:
    void ComputeStatistics();

    QVector<pat::Series> series_;
    QString path_;
    QString timeUnit_;
    bool hasData_ = false;
    SeriesStatistics statistics_;
};

}  // namespace pat
