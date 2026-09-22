#include "core/SeriesProcessing.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace pat {

bool DisplayTransform::IsIdentity() const {
    return qFuzzyCompare(scale, 1.0) && qFuzzyIsNull(offset);
}

void SeriesProcessing::Reset(const FormatDefinition& format) {
    format_ = format;
    transforms_.clear();
    definitions_.clear();
    derivedSeries_.clear();
}

DisplayTransform SeriesProcessing::Transform(int signalIndex) const {
    return transforms_.value(signalIndex, DisplayTransform{});
}

void SeriesProcessing::SetTransform(int signalIndex, const DisplayTransform& transform) {
    if (signalIndex < 0 || signalIndex >= static_cast<int>(format_.signalFormats.size())) return;
    if (transform.IsIdentity()) transforms_.remove(signalIndex);
    else transforms_.insert(signalIndex, transform);
}

void SeriesProcessing::ResetTransform(int signalIndex) {
    transforms_.remove(signalIndex);
}

bool SeriesProcessing::AddDifference(int leftSignal, int rightSignal, QString& errorMessage) {
    return AddDerived(leftSignal, rightSignal, SeriesOperation::Difference, errorMessage);
}

bool SeriesProcessing::AddSum(int leftSignal, int rightSignal, QString& errorMessage) {
    return AddDerived(leftSignal, rightSignal, SeriesOperation::Sum, errorMessage);
}

bool SeriesProcessing::AddDerived(int leftSignal, int rightSignal, SeriesOperation operation,
                                  QString& errorMessage) {
    errorMessage.clear();
    const int count = static_cast<int>(format_.signalFormats.size());
    if (leftSignal < 0 || rightSignal < 0 || leftSignal >= count || rightSignal >= count) {
        errorMessage = QStringLiteral("派生信号索引无效");
        return false;
    }
    if (leftSignal == rightSignal) {
        errorMessage = QStringLiteral("请选择两个不同的信号");
        return false;
    }
    const auto& left = format_.signalFormats[static_cast<size_t>(leftSignal)];
    const auto& right = format_.signalFormats[static_cast<size_t>(rightSignal)];
    if (left.unit != right.unit) {
        errorMessage = QStringLiteral("两个信号单位不同（%1 / %2），不能直接运算")
                           .arg(left.unit.isEmpty() ? QStringLiteral("无单位") : left.unit,
                                right.unit.isEmpty() ? QStringLiteral("无单位") : right.unit);
        return false;
    }
    for (const auto& existing : definitions_) {
        if (existing.leftSignal == leftSignal && existing.rightSignal == rightSignal
            && existing.operation == operation) {
            errorMessage = QStringLiteral("该关系图已经存在");
            return false;
        }
    }

    DerivedSeriesDefinition definition;
    definition.leftSignal = leftSignal;
    definition.rightSignal = rightSignal;
    definition.operation = operation;
    const QString operationName = operation == SeriesOperation::Difference
        ? QStringLiteral("差值") : QStringLiteral("和值");
    const QString symbol = operation == SeriesOperation::Difference
        ? QStringLiteral("-") : QStringLiteral("+");
    definition.title = QStringLiteral("%1: %2 %3 %4（来源: %2、%4）")
                           .arg(operationName, left.name, symbol, right.name);
    definition.unit = left.unit;
    definitions_.append(definition);
    InitializeDerivedSeries();
    return true;
}

void SeriesProcessing::RemoveDerivedSignal(int displayIndex) {
    const int index = DerivedStorageIndex(displayIndex);
    if (displayIndex >= 0 || index < 0 || index >= definitions_.size()) return;
    definitions_.removeAt(index);
    InitializeDerivedSeries();
}

void SeriesProcessing::ClearDerivedSignals() {
    definitions_.clear();
    derivedSeries_.clear();
}

void SeriesProcessing::InitializeDerivedSeries() {
    derivedSeries_.resize(definitions_.size());
    for (int i = 0; i < definitions_.size(); ++i) {
        derivedSeries_[i].name = definitions_[i].title;
        derivedSeries_[i].unit = definitions_[i].unit;
        derivedSeries_[i].samples.clear();
    }
}

void SeriesProcessing::RebuildDerived(const QVector<Series>& source) {
    InitializeDerivedSeries();
    for (int definitionIndex = 0; definitionIndex < definitions_.size(); ++definitionIndex) {
        const auto& definition = definitions_[definitionIndex];
        if (definition.leftSignal >= source.size() || definition.rightSignal >= source.size()) continue;
        const auto& left = source[definition.leftSignal].samples;
        const auto& right = source[definition.rightSignal].samples;
        auto& output = derivedSeries_[definitionIndex].samples;
        output.reserve(std::min(left.size(), right.size()));

        qsizetype leftIndex = 0;
        qsizetype rightIndex = 0;
        while (leftIndex < left.size() && rightIndex < right.size()) {
            const double leftX = left[leftIndex].x();
            const double rightX = right[rightIndex].x();
            const double tolerance = 1e-9 * std::max({1.0, std::abs(leftX), std::abs(rightX)});
            if (std::abs(leftX - rightX) <= tolerance) {
                const double value = definition.operation == SeriesOperation::Difference
                    ? left[leftIndex].y() - right[rightIndex].y()
                    : left[leftIndex].y() + right[rightIndex].y();
                if (std::isfinite(value)) output.append(QPointF(leftX, value));
                ++leftIndex;
                ++rightIndex;
            } else if (leftX < rightX) {
                ++leftIndex;
            } else {
                ++rightIndex;
            }
        }
    }
}

void SeriesProcessing::ClearDerivedSamples() {
    for (auto& series : derivedSeries_) series.samples.clear();
}

void SeriesProcessing::AppendLive(quint64 sampleIndex, const QVector<double>& values,
                                  int maxSamplesPerSignal) {
    maxSamplesPerSignal = std::max(1, maxSamplesPerSignal);
    for (int i = 0; i < definitions_.size(); ++i) {
        const auto& definition = definitions_[i];
        if (definition.leftSignal >= values.size() || definition.rightSignal >= values.size()) continue;
        const double value = definition.operation == SeriesOperation::Difference
            ? values[definition.leftSignal] - values[definition.rightSignal]
            : values[definition.leftSignal] + values[definition.rightSignal];
        if (!std::isfinite(value)) continue;
        auto& samples = derivedSeries_[i].samples;
        samples.append(QPointF(static_cast<double>(sampleIndex), value));
        if (samples.size() > maxSamplesPerSignal) {
            samples.remove(0, samples.size() - maxSamplesPerSignal);
        }
    }
}

}  // namespace pat
