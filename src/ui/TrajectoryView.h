#pragma once

#include "core/FormatDefinition.h"
#include "core/TrajectoryData.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class QShowEvent;
class QChartView;
class QLineSeries;
class QScatterSeries;
class QValueAxis;

class TrajectoryView : public QWidget {
    Q_OBJECT

public:
    explicit TrajectoryView(QWidget* parent = nullptr);

    void ConfigureSignals(const pat::FormatDefinition& format);
    void SetDataSource(const QVector<pat::Series>* series, pat::TrajectoryPairing pairing);
    void SetPaused(bool paused);
    void RequestRefresh();
    void RefreshNow();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void ClearPlot(const QString& message);

    QComboBox* xCombo_ = nullptr;
    QComboBox* yCombo_ = nullptr;
    QCheckBox* autoRange_ = nullptr;
    QLabel* status_ = nullptr;
    QChartView* chartView_ = nullptr;
    QLineSeries* pathSeries_ = nullptr;
    QScatterSeries* currentSeries_ = nullptr;
    QValueAxis* xAxis_ = nullptr;
    QValueAxis* yAxis_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    const QVector<pat::Series>* source_ = nullptr;
    pat::TrajectoryPairing pairing_ = pat::TrajectoryPairing::RecordOrder;
    bool paused_ = false;
    bool dirty_ = true;
};
