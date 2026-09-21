#include "core/TrajectoryData.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pat {

QVector<QPointF> PairTrajectorySamples(const Series& xSeries, const Series& ySeries,
                                      TrajectoryPairing pairing) {
    QVector<QPointF> points;
    const auto& xs = xSeries.samples;
    const auto& ys = ySeries.samples;
    points.reserve(std::min(xs.size(), ys.size()));

    if (pairing == TrajectoryPairing::RecordOrder) {
        const qsizetype count = std::min(xs.size(), ys.size());
        for (qsizetype i = 0; i < count; ++i) {
            if (std::isfinite(xs[i].y()) && std::isfinite(ys[i].y())) {
                points.append(QPointF(xs[i].y(), ys[i].y()));
            }
        }
        return points;
    }

    qsizetype xi = 0;
    qsizetype yi = 0;
    while (xi < xs.size() && yi < ys.size()) {
        if (xs[xi].x() < ys[yi].x()) {
            ++xi;
        } else if (xs[xi].x() > ys[yi].x()) {
            ++yi;
        } else {
            if (std::isfinite(xs[xi].y()) && std::isfinite(ys[yi].y())) {
                points.append(QPointF(xs[xi].y(), ys[yi].y()));
            }
            ++xi;
            ++yi;
        }
    }
    return points;
}

QVector<QPointF> DecimateTrajectory(const QVector<QPointF>& points, int maxPoints) {
    maxPoints = std::max(6, maxPoints);
    if (points.size() <= maxPoints) return points;

    QVector<QPointF> reduced;
    reduced.reserve(maxPoints);
    reduced.append(points.first());
    const qsizetype interior = points.size() - 2;
    const qsizetype bucketCount = (maxPoints - 2) / 4;
    const qsizetype bucketSize = (interior + bucketCount - 1) / bucketCount;
    for (qsizetype begin = 1; begin < points.size() - 1; begin += bucketSize) {
        const qsizetype end = std::min(begin + bucketSize, points.size() - 1);
        qsizetype minX = begin, maxX = begin, minY = begin, maxY = begin;
        for (qsizetype i = begin + 1; i < end; ++i) {
            if (points[i].x() < points[minX].x()) minX = i;
            if (points[i].x() > points[maxX].x()) maxX = i;
            if (points[i].y() < points[minY].y()) minY = i;
            if (points[i].y() > points[maxY].y()) maxY = i;
        }
        std::array<qsizetype, 4> indices = {minX, maxX, minY, maxY};
        std::sort(indices.begin(), indices.end());
        const auto uniqueEnd = std::unique(indices.begin(), indices.end());
        for (auto it = indices.begin(); it != uniqueEnd; ++it) reduced.append(points[*it]);
    }
    reduced.append(points.last());
    return reduced;
}

}  // namespace pat
