#pragma once

#include "core/DataSession.h"
#include "ui/DisplayGroupManager.h"
#include "ui/SignalChartView.h"

#include <QWidget>

class QScrollArea;
class QSplitter;
class QResizeEvent;

class ChartArea : public QWidget {
    Q_OBJECT

public:
    explicit ChartArea(QWidget* parent = nullptr);

    void SetSeries(const QVector<pat::Series>* series);
    void SetDisplayGroups(const QVector<DisplayGroup>& groups);
    void SetStatistics(const pat::SeriesStatistics& stats);
    void SetTimeUnit(const QString& unit);
    void SetMaxVisiblePoints(int maxPoints);
    void ResetXRange();
    void RefreshCharts();

signals:
    void SignalsDropped(const QVector<int>& indices);
    void MergeRequested(const QVector<int>& indices);
    void ReorderRequested(int fromIndex, int toIndex);
    void HideSignalsRequested(const QVector<int>& indices);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void HandleCursorMoved(double cursorX);
    void HandleCursorLeft();
    void HandleXRangeRequested(double minX, double maxX);
    void HandleResetXRange();
    void HandleMergeDropped(const QVector<int>& indices, int targetIndex);
    void HandleChartMergeRequested(int fromIndex, int toIndex);
    void HandleReorderRequested(int fromIndex, int toIndex);
    void HandleHideSignalsRequested(const QVector<int>& indices);

private:
    void BuildCharts();
    void ClearCharts();
    void ApplyXRange(double minX, double maxX);
    void RefreshVisibleSeries(double minX, double maxX);
    QVector<QPointF> DecimateSamples(const QVector<QPointF>& samples,
                                     double minX,
                                     double maxX,
                                     int maxPoints) const;
    bool ComputeGroupRange(const QVector<int>& indices, double& outMinY, double& outMaxY) const;
    void UpdateChartHeights();
    void UpdateRangeContext();

    QScrollArea* scrollArea_ = nullptr;
    QWidget* container_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QVector<SignalChartView*> charts_;

    const QVector<pat::Series>* series_ = nullptr;
    QVector<DisplayGroup> groups_;
    pat::SeriesStatistics stats_;
    bool hasStats_ = false;
    QString timeUnit_ = QStringLiteral("s");
    double currentMinX_ = 0.0;
    double currentMaxX_ = 0.0;
    bool hasCurrentRange_ = false;
    double minXSpan_ = 1e-3;
    int maxVisiblePoints_ = 5000;

    bool cursorActive_ = false;
    double sharedCursorX_ = 0.0;

    int chartHeight_ = 240;
    int minChartHeight_ = 120;
    int maxChartHeight_ = 480;
};
