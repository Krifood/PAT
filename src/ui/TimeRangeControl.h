#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class TimeRangeBar;

// Shared offline time window for time-series and spatial plots.
class TimeRangeControl : public QWidget {
    Q_OBJECT
public:
    explicit TimeRangeControl(QWidget* parent = nullptr);
    void SetExtent(double maximum, const QString& unit, bool available);
    void SetSelection(double start, double end);

signals:
    void RangeSelected(double start, double end);

private:
    void Commit(double start, double end);
    double maximum_ = 0.0;
    QDoubleSpinBox* start_ = nullptr;
    QDoubleSpinBox* end_ = nullptr;
    QLabel* caption_ = nullptr;
    TimeRangeBar* bar_ = nullptr;
};
