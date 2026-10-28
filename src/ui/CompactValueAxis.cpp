#include "ui/CompactValueAxis.h"

#include <QFontMetricsF>
#include <QFontDatabase>
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

void CompactValueAxis::SetStableLabelWidth() {
    stableLabelWidth_ = true;
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(9.0);
    font.setWeight(QFont::Normal);
    setLabelsFont(font);
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
        labels.append(QString::number(value, 'g', 4));
    }
    annotation_.clear();
    const QSet<QString> unique(labels.begin(), labels.end());
    const bool needsOffset = unique.size() != labels.size();
    const bool tooWide = std::any_of(labels.begin(), labels.end(), [](const QString& label) { return label.size() > 6; });
    if (needsOffset || tooWide) {
        // Large baseline with small deltas: retain distinguishable ticks without
        // repeating a dozen common digits at every tick.
        const double offset = needsOffset ? low : 0.0;
        const double magnitude = needsOffset ? high - low : std::max(std::abs(low), std::abs(high));
        const double scale = std::pow(10.0, std::clamp(std::floor(std::log10(magnitude)), -308.0, 308.0));
        labels.clear();
        for (double value : values) {
            QString label;
            for (int precision = 4; precision >= 1; --precision) {
                label = QString::number((value - offset) / scale, 'g', precision);
                if (label.size() <= 6) break;
            }
            labels.append(label);
        }
        annotation_ = tr("原值 = %1 + 刻度 × %2")
            .arg(QString::number(offset, 'g', 17), QString::number(scale, 'g', 3));
    }
    if (stableLabelWidth_) {
        // Figure spaces reserve a constant numeric-label envelope without
        // modifying values. Keep Qt's automatic axis/series layout in charge.
        for (auto& label : labels) {
            const int padding = std::max(0, 6 - int(label.size()));
            label = QString(padding / 2, QChar(0x2007)) + label + QString(padding - padding / 2, QChar(0x2007));
        }
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
