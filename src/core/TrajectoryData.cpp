#include "core/TrajectoryData.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pat {

QVector<QPointF> PairTrajectorySamples(const Series& xSeries, const Series& ySeries,
                                      TrajectoryPairing pairing) {
    const auto paired = PairTrajectorySamplesWithKeys(xSeries, ySeries, pairing);
    QVector<QPointF> points;
    points.reserve(paired.size());
    for (const auto& point : paired) points.append(point.value);
    return points;
}

QVector<TrajectoryPoint2D> PairTrajectorySamplesWithKeys(
    const Series& xSeries, const Series& ySeries, TrajectoryPairing pairing) {
    QVector<TrajectoryPoint2D> points;
    const auto& xs = xSeries.samples;
    const auto& ys = ySeries.samples;
    points.reserve(std::min(xs.size(), ys.size()));

    if (pairing == TrajectoryPairing::RecordOrder) {
        const qsizetype count = std::min(xs.size(), ys.size());
        for (qsizetype i = 0; i < count; ++i) {
            if (std::isfinite(xs[i].y()) && std::isfinite(ys[i].y())) {
                points.append({static_cast<double>(i), QPointF(xs[i].y(), ys[i].y())});
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
                points.append({xs[xi].x(), QPointF(xs[xi].y(), ys[yi].y())});
            }
            ++xi;
            ++yi;
        }
    }
    return points;
}

QVector<TrajectoryPoint3D> PairTrajectorySamples3D(
    const Series& xSeries, const Series& ySeries, const Series& zSeries,
    TrajectoryPairing pairing) {
    QVector<TrajectoryPoint3D> points;
    const auto& xs = xSeries.samples;
    const auto& ys = ySeries.samples;
    const auto& zs = zSeries.samples;
    points.reserve(std::min({xs.size(), ys.size(), zs.size()}));

    if (pairing == TrajectoryPairing::RecordOrder) {
        const qsizetype count = std::min({xs.size(), ys.size(), zs.size()});
        for (qsizetype i = 0; i < count; ++i) {
            if (std::isfinite(xs[i].y()) && std::isfinite(ys[i].y())
                && std::isfinite(zs[i].y())) {
                points.append({static_cast<double>(i), xs[i].y(), ys[i].y(), zs[i].y()});
            }
        }
        return points;
    }

    qsizetype xi = 0;
    qsizetype yi = 0;
    qsizetype zi = 0;
    while (xi < xs.size() && yi < ys.size() && zi < zs.size()) {
        const double key = std::max({xs[xi].x(), ys[yi].x(), zs[zi].x()});
        while (xi < xs.size() && xs[xi].x() < key) ++xi;
        while (yi < ys.size() && ys[yi].x() < key) ++yi;
        while (zi < zs.size() && zs[zi].x() < key) ++zi;
        if (xi >= xs.size() || yi >= ys.size() || zi >= zs.size()) break;
        if (xs[xi].x() != ys[yi].x() || xs[xi].x() != zs[zi].x()) continue;
        if (std::isfinite(xs[xi].y()) && std::isfinite(ys[yi].y())
            && std::isfinite(zs[zi].y())) {
            points.append({xs[xi].x(), xs[xi].y(), ys[yi].y(), zs[zi].y()});
        }
        ++xi;
        ++yi;
        ++zi;
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

QVector<TrajectoryPoint3D> DecimateTrajectory3D(
    const QVector<TrajectoryPoint3D>& points, int maxPoints) {
    maxPoints = std::max(8, maxPoints);
    if (points.size() <= maxPoints) return points;

    QVector<TrajectoryPoint3D> reduced;
    reduced.reserve(maxPoints);
    reduced.append(points.first());
    const qsizetype interior = points.size() - 2;
    const qsizetype bucketCount = std::max<qsizetype>(1, (maxPoints - 2) / 6);
    const qsizetype bucketSize = (interior + bucketCount - 1) / bucketCount;
    for (qsizetype begin = 1; begin < points.size() - 1; begin += bucketSize) {
        const qsizetype end = std::min(begin + bucketSize, points.size() - 1);
        qsizetype minX = begin, maxX = begin, minY = begin, maxY = begin;
        qsizetype minZ = begin, maxZ = begin;
        for (qsizetype i = begin + 1; i < end; ++i) {
            if (points[i].x < points[minX].x) minX = i;
            if (points[i].x > points[maxX].x) maxX = i;
            if (points[i].y < points[minY].y) minY = i;
            if (points[i].y > points[maxY].y) maxY = i;
            if (points[i].z < points[minZ].z) minZ = i;
            if (points[i].z > points[maxZ].z) maxZ = i;
        }
        std::array<qsizetype, 6> indices = {minX, maxX, minY, maxY, minZ, maxZ};
        std::sort(indices.begin(), indices.end());
        const auto uniqueEnd = std::unique(indices.begin(), indices.end());
        for (auto it = indices.begin(); it != uniqueEnd; ++it) reduced.append(points[*it]);
    }
    reduced.append(points.last());
    return reduced;
}

}  // namespace pat
