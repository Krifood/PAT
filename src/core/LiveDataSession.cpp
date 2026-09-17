#include "core/LiveDataSession.h"

#include <algorithm>
#include <cmath>

namespace pat {

void LiveDataSession::Reset(const FormatDefinition& format, int maxSamplesPerSignal) {
    series_.clear();
    series_.resize(static_cast<qsizetype>(format.signalFormats.size()));
    for (qsizetype i = 0; i < series_.size(); ++i) {
        series_[i].name = format.signalFormats[static_cast<size_t>(i)].name;
        series_[i].unit = format.signalFormats[static_cast<size_t>(i)].unit;
    }
    maxSamplesPerSignal_ = std::max(1, maxSamplesPerSignal);
    statistics_ = SeriesStatistics{};
    hasData_ = false;
}

bool LiveDataSession::Append(quint64 sampleIndex, const QVector<double>& values) {
    if (values.size() != series_.size()) return false;
    for (qsizetype i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) continue;
        auto& samples = series_[i].samples;
        samples.append(QPointF(static_cast<double>(sampleIndex), values[i]));
        if (samples.size() > maxSamplesPerSignal_) {
            samples.remove(0, samples.size() - maxSamplesPerSignal_);
        }
    }
    statistics_.maxX = static_cast<double>(sampleIndex);
    hasData_ = true;
    return true;
}

void LiveDataSession::UpdateStatistics() {
    statistics_.hasRange = false;
    statistics_.minStep = 1.0;
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
    }
    if (!statistics_.hasRange) {
        statistics_.minY = -1.0;
        statistics_.maxY = 1.0;
    } else if (statistics_.minY == statistics_.maxY) {
        const double delta = std::max(1.0, std::abs(statistics_.minY) * 0.1);
        statistics_.minY -= delta;
        statistics_.maxY += delta;
    }
}

}  // namespace pat
