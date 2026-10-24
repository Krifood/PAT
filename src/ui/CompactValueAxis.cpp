#include "ui/CompactValueAxis.h"

#include <QFontMetricsF>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

CompactValueAxis::CompactValueAxis(QObject* parent) : QCategoryAxis(parent) {
    setLabelsPosition(QCategoryAxis::AxisLabelsPositionOnValue);
    connect(this, &QValueAxis::rangeChanged, this, [this] { RefreshLabels(); });
    connect(this, &QValueAxis::tickCountChanged, this, [this] { RefreshLabels(); });
    RefreshLabels();
}

void CompactValueAxis::RefreshLabels() {
    const double low = min(), high = max();
    if (!std::isfinite(low) || !std::isfinite(high) || high <= low) return;
    QVector<double> values;
    QStringList labels;
    const int count = std::clamp(tickCount(), 2, 10);
    for (int i = 0; i < count; ++i) {
        const double fraction = double(i) / (count - 1);
        const double value = i == 0 ? low : (i == count - 1 ? high
            : (1.0 - fraction) * low + fraction * high);
        if (!values.isEmpty() && value <= values.last()) continue;
        values.append(value);
        labels.append(QString::number(value, 'g', 6));
    }
    annotation_.clear();
    const QSet<QString> unique(labels.begin(), labels.end());
    if (unique.size() != labels.size()) {
        // Large baseline with small deltas: retain distinguishable ticks without
        // repeating a dozen common digits at every tick.
        const double span = high - low;
        const double scale = std::pow(10.0, std::clamp(std::floor(std::log10(span)), -300.0, 300.0));
        labels.clear();
        for (double value : values) labels.append(QStringLiteral("Δ") + QString::number((value - low) / scale, 'g', 6));
        annotation_ = tr("原值 = %1 + 刻度 × %2")
            .arg(QString::number(low, 'g', 17), QString::number(scale, 'g', 3));
    }
    for (const auto& label : categoriesLabels()) remove(label);
    setStartValue(std::nextafter(low, -std::numeric_limits<double>::infinity()));
    for (int i = 0; i < values.size(); ++i) append(labels[i], values[i]);
}

qreal CompactValueAxis::LabelWidth() {
    const QFontMetricsF metrics(labelsFont());
    qreal width = 0;
    for (const auto& label : categoriesLabels()) width = std::max(width, metrics.horizontalAdvance(label));
    return width;
}
