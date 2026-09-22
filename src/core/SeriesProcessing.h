#pragma once

#include "core/FormatDefinition.h"
#include "core/RecordParser.h"

#include <QHash>

namespace pat {

struct DisplayTransform {
    double scale = 1.0;
    double offset = 0.0;

    bool IsIdentity() const;
    double Apply(double value) const { return value * scale + offset; }
};

enum class SeriesOperation { Difference, Sum };

struct DerivedSeriesDefinition {
    int leftSignal = -1;
    int rightSignal = -1;
    SeriesOperation operation = SeriesOperation::Difference;
    QString title;
    QString unit;
};

class SeriesProcessing {
public:
    void Reset(const FormatDefinition& format);

    DisplayTransform Transform(int signalIndex) const;
    void SetTransform(int signalIndex, const DisplayTransform& transform);
    void ResetTransform(int signalIndex);

    bool AddDerived(int leftSignal, int rightSignal, SeriesOperation operation,
                    QString& errorMessage);
    bool AddDifference(int leftSignal, int rightSignal, QString& errorMessage);
    bool AddSum(int leftSignal, int rightSignal, QString& errorMessage);
    void RemoveDerivedSignal(int displayIndex);
    void ClearDerivedSignals();
    void RebuildDerived(const QVector<Series>& source);
    void ClearDerivedSamples();
    void AppendLive(quint64 sampleIndex, const QVector<double>& values,
                    int maxSamplesPerSignal = 30000);

    const QVector<DerivedSeriesDefinition>& DerivedDefinitions() const { return definitions_; }
    const QVector<Series>& DerivedSeries() const { return derivedSeries_; }
    const QHash<int, DisplayTransform>& Transforms() const { return transforms_; }

    static int DerivedDisplayIndex(int derivedIndex) { return -derivedIndex - 1; }
    static int DerivedStorageIndex(int displayIndex) { return -displayIndex - 1; }

private:
    void InitializeDerivedSeries();

    FormatDefinition format_;
    QHash<int, DisplayTransform> transforms_;
    QVector<DerivedSeriesDefinition> definitions_;
    QVector<Series> derivedSeries_;
};

}  // namespace pat
