#include "core/MixedDataSession.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pat {

bool MixedDataSession::Open(const QString& path, const FormatDefinition& format,
                            QString& errorMessage) {
    IndexedMixedReader opened;
    if (!opened.Open(path, format, errorMessage)) return false;
    reader_ = std::move(opened);
    series_.resize(static_cast<qsizetype>(format.signalFormats.size()));
    for (int i = 0; i < series_.size(); ++i) {
        series_[i].name = format.signalFormats[static_cast<size_t>(i)].name;
        series_[i].unit = format.signalFormats[static_cast<size_t>(i)].unit;
    }
    timeUnit_ = format.timeAxisUnit;
    retainedPointCount_ = 0;
    loadedSignalIndices_.clear();
    ComputeStatistics();
    return true;
}

bool MixedDataSession::LoadSignals(const QVector<int>& signalIndices,
                                   QString& errorMessage) {
    QVector<int> normalized = signalIndices;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    if (normalized == loadedSignalIndices_) return true;
    MixedReadResult loaded;
    if (!reader_.ReadSignals(normalized, cacheBudgetBytes_, loaded, errorMessage)) return false;
    series_ = std::move(loaded.series);
    retainedPointCount_ = loaded.retainedPoints;
    loadedSignalIndices_ = std::move(normalized);
    ComputeStatistics();
    return true;
}

void MixedDataSession::Clear() {
    reader_.Close();
    series_.clear();
    statistics_ = SeriesStatistics{};
    timeUnit_.clear();
    retainedPointCount_ = 0;
    loadedSignalIndices_.clear();
}

void MixedDataSession::ComputeStatistics() {
    statistics_ = SeriesStatistics{};
    double minStep = std::numeric_limits<double>::infinity();
    bool hasStep = false;
    for (const auto& series : series_) {
        for (const auto& point : series.samples) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y())) continue;
            if (!statistics_.hasRange) {
                statistics_.minY = statistics_.maxY = point.y();
                statistics_.hasRange = true;
            } else {
                statistics_.minY = std::min(statistics_.minY, point.y());
                statistics_.maxY = std::max(statistics_.maxY, point.y());
            }
            statistics_.maxX = std::max(statistics_.maxX, point.x());
        }
        const int count = std::min<int>(series.samples.size(), 4096);
        for (int i = 1; i < count; ++i) {
            const double step = std::abs(series.samples[i].x() - series.samples[i - 1].x());
            if (step > 0.0 && step < minStep) { minStep = step; hasStep = true; }
        }
    }
    if (!statistics_.hasRange) {
        statistics_.minY = -1.0;
        statistics_.maxY = 1.0;
        statistics_.maxX = 0.0;
    } else if (qFuzzyCompare(statistics_.minY, statistics_.maxY)) {
        const double delta = std::abs(statistics_.minY) > 1.0
            ? std::abs(statistics_.minY) * 0.1 : 1.0;
        statistics_.minY -= delta;
        statistics_.maxY += delta;
    }
    statistics_.minStep = hasStep ? std::max(minStep * 0.01, 1e-3) : 1e-3;
}

}  // namespace pat
