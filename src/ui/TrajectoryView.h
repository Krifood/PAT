#pragma once

#include "core/FormatDefinition.h"
#include "core/SeriesProcessing.h"
#include "core/TrajectoryData.h"
#include "ui/AppTheme.h"

#include <QFrame>
#include <QHash>
#include <QWidget>
#include <limits>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineSeries;
class QScatterSeries;
class QSplitter;
class QTimer;
class QShowEvent;
class QChartView;
class QValueAxis;
class QMimeData;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class Trajectory3DCanvas;

enum class TrajectoryDimension { TwoD, ThreeD };

class TrajectoryPlotWidget : public QFrame {
    Q_OBJECT

public:
    struct TrackSnapshot {
        int xSignal = -1;
        int ySignal = -1;
        int zSignal = -1;
        QVector<pat::TrajectoryPoint2D> history2D;
        QVector<pat::TrajectoryPoint3D> history3D;
        double lastSampleKey = -1.0;
        bool visible = true;
    };

    TrajectoryPlotWidget(int plotId, TrajectoryDimension dimension, QWidget* parent = nullptr);

    int PlotId() const { return plotId_; }
    TrajectoryDimension Dimension() const { return dimension_; }
    int TrackCount() const { return tracks_.size(); }
    qsizetype HistoryPointCount(int trackIndex = 0) const;

    void ConfigureSignals(const pat::FormatDefinition& format);
    void SetDataSource(const QVector<pat::Series>* series, pat::TrajectoryPairing pairing);
    void SetDisplayTransforms(const QHash<int, pat::DisplayTransform>& transforms);
    void SetTheme(const AppTheme& theme);
    void SetTimeWindow(double start, double end);
    void CaptureData();
    void Render();
    void ResetHistory();
    QVector<TrackSnapshot> Snapshots() const;
    QVector<int> SelectedSignalIndices() const;
    QVector<QVector<int>> SelectedSignalGroups() const;
    void AppendSnapshots(const QVector<TrackSnapshot>& snapshots);
    void SetRemoveEnabled(bool enabled);

signals:
    void FocusRequested(int plotId);
    void MergeRequested(int sourcePlotId, int targetPlotId);
    void RemoveRequested(int plotId);
    void SignalsChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    struct TrackState {
        int id = 0;
        QWidget* row = nullptr;
        QLabel* label = nullptr;
        QCheckBox* visible = nullptr;
        QComboBox* groupCombo = nullptr;
        QComboBox* xCombo = nullptr;
        QComboBox* yCombo = nullptr;
        QComboBox* zCombo = nullptr;
        QLineSeries* line = nullptr;
        QScatterSeries* current = nullptr;
        QVector<pat::TrajectoryPoint2D> history2D;
        QVector<pat::TrajectoryPoint3D> history3D;
        double lastSampleKey = -1.0;
    };

    void AddTrack(const TrackSnapshot* snapshot = nullptr);
    void RemoveTrack(int trackId);
    void PopulateTrackCombos(TrackState& track, const TrackSnapshot* snapshot = nullptr);
    void PopulateCoordinateCombos(TrackState& track, int xSignal, int ySignal, int zSignal);
    void HandleTrackGroupChanged(int trackId);
    void ResetTrackHistory(int trackId);
    void RebuildTrackRows();
    void Ensure2DSeries();
    void Render2D();
    void ScheduleEqualAspect();
    void ApplyEqualAspect();
    void UpdateExtrema();
    void UpdateAxisTitles();
    void Render3D();
    QString TrackName(const TrackState& track) const;
    int BestSignal(bool longitude, bool latitude, bool altitude) const;
    bool InTimeWindow(double sampleKey, int xSignal) const;
    bool DecodeDrag(const QMimeData* mime, int& sourceId, TrajectoryDimension& dimension) const;
    bool HandleDragEnter(QDragEnterEvent* event);
    bool HandleDragMove(QDragMoveEvent* event);
    bool HandleDrop(QDropEvent* event);

    int plotId_ = 0;
    int nextTrackId_ = 0;
    TrajectoryDimension dimension_ = TrajectoryDimension::TwoD;
    pat::FormatDefinition format_;
    const QVector<pat::Series>* source_ = nullptr;
    pat::TrajectoryPairing pairing_ = pat::TrajectoryPairing::RecordOrder;
    QHash<int, pat::DisplayTransform> transforms_;
    AppTheme theme_ = ThemeFor(AppThemeId::IndoorDark);

    QLabel* headerTitle_ = nullptr;
    QLabel* dimensionLabel_ = nullptr;
    QWidget* trackRows_ = nullptr;
    QCheckBox* autoRange_ = nullptr;
    QCheckBox* equalAspect_ = nullptr;
    bool aspectPending_ = false;
    bool applyingAspect_ = false;
    double baseXMin_ = -1, baseXMax_ = 1, baseYMin_ = -1, baseYMax_ = 1;
    QLabel* extrema_ = nullptr;
    QLabel* pointerReadout_ = nullptr;
    QLabel* status_ = nullptr;
    QChartView* chartView_ = nullptr;
    Trajectory3DCanvas* canvas3D_ = nullptr;
    QValueAxis* xAxis_ = nullptr;
    QValueAxis* yAxis_ = nullptr;
    QVector<TrackState> tracks_;
    QPoint dragStart_;
    double windowStart_ = 0.0;
    double windowEnd_ = std::numeric_limits<double>::infinity();
};

class TrajectoryView : public QWidget {
    Q_OBJECT

public:
    explicit TrajectoryView(QWidget* parent = nullptr);

    void SetTheme(const AppTheme& theme);
    void SetTimeWindow(double start, double end);
    void ConfigureSignals(const pat::FormatDefinition& format);
    void SetDataSource(const QVector<pat::Series>* series, pat::TrajectoryPairing pairing);
    void SetDisplayTransforms(const QHash<int, pat::DisplayTransform>& transforms);
    void SetPaused(bool paused);
    void RequestRefresh();
    void RefreshNow();
    void ResetSession();
    QVector<int> SelectedSignalIndices() const;
    QVector<QVector<int>> SelectedSignalGroups() const;

    TrajectoryPlotWidget* AddPlot(TrajectoryDimension dimension);
    bool MergePlots(int sourcePlotId, int targetPlotId, QString& errorMessage);
    int PlotCount() const { return plots_.size(); }

signals:
    void SignalsChanged();

protected:
    void showEvent(QShowEvent* event) override;

private:
    void RemovePlot(int plotId);
    void ClearPlots();
    void UpdateRemoveButtons();
    void CaptureAllPlots();

    QSplitter* splitter_ = nullptr;
    QLabel* emptyState_ = nullptr;
    QTimer* refreshTimer_ = nullptr;
    QVector<TrajectoryPlotWidget*> plots_;
    int nextPlotId_ = 0;
    int focusedPlotId_ = -1;
    pat::FormatDefinition format_;
    const QVector<pat::Series>* source_ = nullptr;
    pat::TrajectoryPairing pairing_ = pat::TrajectoryPairing::RecordOrder;
    QHash<int, pat::DisplayTransform> transforms_;
    AppTheme theme_ = ThemeFor(AppThemeId::IndoorDark);
    bool paused_ = false;
    bool dirty_ = true;
    double windowStart_ = 0.0;
    double windowEnd_ = std::numeric_limits<double>::infinity();
};
