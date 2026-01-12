#include "core/DataSession.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <QtGlobal>

namespace pat {

bool DataSession::Load(const QString& path, const FormatDefinition& format, QString& errorMessage) {
    RecordParser parser(format);
    QVector<pat::Series> parsed;
    if (!parser.ParseFile(path, parsed, errorMessage)) {
        return false;
    }

    series_ = std::move(parsed);
    path_ = path;
    timeUnit_ = format.timeAxisUnit;
    hasData_ = true;
    ComputeStatistics();
    return true;
}

void DataSession::Clear() {
    series_.clear();
    path_.clear();
    timeUnit_.clear();
    hasData_ = false;
    statistics_ = SeriesStatistics{};
}

void DataSession::ComputeStatistics() {
    statistics_ = SeriesStatistics{};
    statistics_.hasRange = false;

    double minStep = std::numeric_limits<double>::infinity();
    bool hasStep = false;

    for (const auto& series : series_) {
        for (const auto& point : series.samples) {
            if (!statistics_.hasRange) {
                statistics_.minY = statistics_.maxY = point.y();
                statistics_.hasRange = true;
            } else {
                statistics_.minY = std::min(statistics_.minY, point.y());
                statistics_.maxY = std::max(statistics_.maxY, point.y());
            }
        }
        if (!series.samples.isEmpty()) {
            statistics_.maxX = std::max(statistics_.maxX, series.samples.last().x());
        }

        constexpr int kMaxStepScan = 4096;
        const int count = std::min(static_cast<int>(series.samples.size()), kMaxStepScan);
        for (int i = 1; i < count; ++i) {
            const double dx = std::abs(series.samples.at(i).x() - series.samples.at(i - 1).x());
            if (dx > 0.0 && dx < minStep) {
                minStep = dx;
                hasStep = true;
            }
        }
    }

    if (!statistics_.hasRange) {
        statistics_.minY = -1.0;
        statistics_.maxY = 1.0;
        statistics_.maxX = 0.0;
    }

    if (qFuzzyCompare(statistics_.minY, statistics_.maxY)) {
        const double delta = std::abs(statistics_.minY) > 1.0 ? std::abs(statistics_.minY) * 0.1 : 1.0;
        statistics_.minY -= delta;
        statistics_.maxY += delta;
    }

    statistics_.minStep = hasStep ? std::max(minStep * 0.01, 1e-3) : 1e-3;
}

}  // namespace pat
