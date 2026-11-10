#pragma once

#include "core/RecordParser.h"

namespace pat {

// X=longitude, Y=latitude. Spherical surface distance, not altitude or path length.
// Invalid coordinates return NaN. Geographic input is explicitly degrees or radians.
double CoordinateDistance(QPointF a, QPointF b, bool geographic, bool radians = false);

// Offline records pair by position; live series may omit invalid fields, so pair by sample index.
enum class TrajectoryPairing { RecordOrder, SampleIndex, OfflineTimestamp };

struct TrajectoryPoint2D {
    double sampleKey = 0.0;
    QPointF value;
};

struct TrajectoryPoint3D {
    double sampleKey = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

QVector<QPointF> PairTrajectorySamples(const Series& xSeries, const Series& ySeries,
                                      TrajectoryPairing pairing);
QVector<TrajectoryPoint2D> PairTrajectorySamplesWithKeys(
    const Series& xSeries, const Series& ySeries, TrajectoryPairing pairing);
QVector<TrajectoryPoint3D> PairTrajectorySamples3D(
    const Series& xSeries, const Series& ySeries, const Series& zSeries,
    TrajectoryPairing pairing);
QVector<QPointF> DecimateTrajectory(const QVector<QPointF>& points, int maxPoints);
QVector<TrajectoryPoint3D> DecimateTrajectory3D(
    const QVector<TrajectoryPoint3D>& points, int maxPoints);

}  // namespace pat
