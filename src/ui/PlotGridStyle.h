#pragma once

#include "ui/AppTheme.h"
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>
#include <QPen>
#include <QFontMetricsF>
#include <QVariant>
#include <algorithm>

inline void ApplyPlotGridStyle(QChart* chart, bool dense, const AppTheme& theme) {
    if (!chart || chart->property("applyingDenseTicks").toBool()) return;
    chart->setProperty("applyingDenseTicks", true);
    for (auto* abstractAxis : chart->axes()) {
        auto* axis = qobject_cast<QValueAxis*>(abstractAxis);
        if (!axis) continue;
        // Use labeled major ticks, not unlabeled minor/background grid lines.
        axis->setMinorTickCount(0);
        axis->setMinorGridLineVisible(false);
        axis->setGridLinePen(QPen(theme.gridLine, 0.8));
        if (axis->orientation() != Qt::Vertical) continue;
        const auto saved = axis->property("normalTickCount");
        if (dense) {
            if (!saved.isValid()) axis->setProperty("normalTickCount", axis->tickCount());
            const int normal = std::clamp(axis->property("normalTickCount").toInt(), 2, 10);
            const qreal spacing = std::max(40.0, QFontMetricsF(axis->labelsFont()).height() * 2.5);
            // CompactValueAxis supports up to ten distinct labeled ticks.
            const int count = std::clamp(int(chart->plotArea().height() / spacing) + 1, normal, 10);
            if (axis->tickCount() != count) axis->setTickCount(count);
        } else if (saved.isValid()) {
            axis->setTickCount(saved.toInt());
            axis->setProperty("normalTickCount", QVariant());
        }
    }
    chart->setProperty("applyingDenseTicks", false);
}
