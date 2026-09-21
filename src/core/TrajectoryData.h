#pragma once

#include "core/RecordParser.h"

namespace pat {

// Offline records pair by position; live series may omit invalid fields, so pair by sample index.
enum class TrajectoryPairing { RecordOrder, SampleIndex };

QVector<QPointF> PairTrajectorySamples(const Series& xSeries, const Series& ySeries,
                                      TrajectoryPairing pairing);
QVector<QPointF> DecimateTrajectory(const QVector<QPointF>& points, int maxPoints);

}  // namespace pat
