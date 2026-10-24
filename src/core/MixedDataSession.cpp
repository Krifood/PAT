#include "core/MixedDataSession.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace pat {

bool MixedDataSession::Open(const QString& path, const FormatDefinition& format,
                            QString& errorMessage) {
    IndexedMixedReader opened;
    if (!opened.Open(path, format, errorMessage)) return false;
    static std::atomic<quint64> nextRevision{0};
    revision_ = ++nextRevision;
    reader_ = std::move(opened);
    series_.clear();
    lastRead_ = {};
    loadedAlignments_.clear();
    relations_.clear();
    loadedBudget_ = 0;
    series_.resize(static_cast<qsizetype>(format.signalFormats.size()));
    for (int i = 0; i < series_.size(); ++i) {
        series_[i].name = format.signalFormats[static_cast<size_t>(i)].QualifiedDisplayName();
        series_[i].unit = format.signalFormats[static_cast<size_t>(i)].unit;
    }
    timeUnit_ = format.timeAxisUnit;
    retainedPointCount_ = 0;
    loadedSignalIndices_.clear();
    ComputeStatistics();
    return true;
}

bool MixedDataSession::LoadSignals(const QVector<int>& signalIndices,
                                   QString& errorMessage,
                                   const MixedReadWindow& window,
                                   const std::function<bool()>& isCancelled, const QVector<MixedAlignment>& alignments) {
    QVector<int> normalized = signalIndices;
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    if (normalized == loadedSignalIndices_ && window == loadedWindow_
        && loadedBudget_ == cacheBudgetBytes_ && alignments == loadedAlignments_) return true;
    MixedReadResult loaded;
    if (alignments.size() > 128) { errorMessage = QStringLiteral("配对图数量超过 128，请减少同时展示的图"); return false; }
    const qsizetype rawBudget = alignments.isEmpty() ? cacheBudgetBytes_ : cacheBudgetBytes_ / 2;
    if (!reader_.ReadSignals(normalized, rawBudget, loaded, errorMessage, window, isCancelled)) return false;
    QVector<pat::Series> relations;
    for (const auto& alignment : alignments) {
        MixedReadResult paired;
        if (!reader_.ReadAlignedSignals(alignment.indices, cacheBudgetBytes_ / (2 * alignments.size()),
                paired, errorMessage, window, isCancelled, alignment.operation)) return false;
        loaded.readChunks += paired.readChunks;
        loaded.readBytes += paired.readBytes;
        loaded.reduced = loaded.reduced || paired.reduced;
        loaded.decodedRecords += paired.decodedRecords;
        if (alignment.operation >= 0) {
            relations.append(std::move(paired.relation));
        } else {
            // Spatial groups are joined on original timestamps before reduction.
            // Merge shared coordinate columns without joining unrelated streams.
            for (int flat : alignment.indices) {
                auto& target = loaded.series[flat];
                target.name = paired.series[flat].name;
                target.unit = paired.series[flat].unit;
                target.samples += paired.series[flat].samples;
                std::stable_sort(target.samples.begin(), target.samples.end(),
                    [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
                target.samples.erase(std::unique(target.samples.begin(), target.samples.end(),
                    [](const QPointF& a, const QPointF& b) {
                        return a.x() == b.x() && (a.y() == b.y() || (std::isnan(a.y()) && std::isnan(b.y())));
                    }), target.samples.end());
            }
        }
    }
    loaded.retainedPoints = 0;
    for (const auto& series : loaded.series) loaded.retainedPoints += series.samples.size();
    for (const auto& relation : relations) loaded.retainedPoints += relation.samples.size();
    series_ = std::move(loaded.series);
    relations_ = std::move(relations);
    loadedAlignments_ = alignments;
    loadedWindow_ = window;
    loadedBudget_ = cacheBudgetBytes_;
    retainedPointCount_ = loaded.retainedPoints;
    loadedSignalIndices_ = std::move(normalized);
    lastRead_ = std::move(loaded);
    ComputeStatistics();
    return true;
}

MixedDataSession MixedDataSession::MetadataSnapshot() const {
    MixedDataSession result;
    result.reader_ = reader_;
    result.timeUnit_ = timeUnit_;
    result.cacheBudgetBytes_ = cacheBudgetBytes_;
    result.revision_ = revision_;
    return result;
}

void MixedDataSession::Clear() {
    reader_.Close();
    revision_ = 0;
    loadedBudget_ = 0;
    lastRead_ = {};
    loadedAlignments_.clear();
    relations_.clear();
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
    statistics_.maxX = reader_.MaxX();
    statistics_.minStep = hasStep ? std::max(minStep * 0.01, 1e-3) : 1e-3;
}

}  // namespace pat
