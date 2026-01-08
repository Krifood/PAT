#pragma once

#include <QMainWindow>

#include "core/FormatDefinition.h"
#include "core/RecordParser.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QMenu;
class QSplitter;
class QScrollArea;
class QVBoxLayout;
class QMouseEvent;
class QWheelEvent;
class QGraphicsLineItem;
class QGraphicsSimpleTextItem;
class QRubberBand;
class QMimeData;
QT_END_NAMESPACE

class SignalTreeWidget;

#ifdef PAT_ENABLE_QT_CHARTS
class QChartView;
class QLineSeries;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void OpenFormatFile();
    void OpenDataFile();
    void NewFormatFile();
    void EditFormatFile();
    void SaveFormatFile();
    void SaveFormatFileAs();
    void SetMaxVisiblePoints();

private:
    void SetupUi();
    void UpdateCharts();
    void UpdateStatus(const QString& text);
    void ClearCharts();
    bool eventFilter(QObject* obj, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    struct ChartItem;
    ChartItem* FindChart(QChartView* view);
    void HandleMouseMove(ChartItem& item, QMouseEvent* event);
    void HandleWheel(ChartItem& item, QWheelEvent* event);
    void HandleRubberBand(ChartItem& item, QMouseEvent* event);
    void UpdateSharedCursor(double cursorX);
    void HideCrosshairs();
    void UpdateGlobalRanges();
    void ApplyXRange(double minX, double maxX);
    void BuildSignalTree();
    void UpdateDisplayGroups();
    void ShowSignalTreeMenu(const QPoint& pos);
    void MergeSignals(const QVector<int>& indices);
    void MergeSelectedSignals();
    void UnmergeSelectedSignals();
    QVector<int> CollectCheckedSignalIndices() const;
    QVector<int> CollectSelectedSignalIndices() const;
    bool AreUnitsCompatible(const QVector<int>& indices, QString& outUnit) const;
    bool ComputeGroupRange(const QVector<int>& indices, double& outMinY, double& outMaxY) const;
    void UpdateChartHeights();
    void ResetXRange();
    int ChartIndex(QChartView* view) const;
    void ReorderCharts(int fromIndex, int toIndex);
    bool RunFormatEditor(QString initialText, QString& outText, const QString& title);
    bool ApplyFormatJsonText(const QString& jsonText, const QString& sourceLabel);
    void SetSignalsChecked(const QVector<int>& indices, bool checked);
    void RemoveFromMergedGroups(const QVector<int>& indices);
    bool ReadSignalIndicesMime(const QMimeData* mime, QVector<int>& outIndices) const;
    void RefreshVisibleSeries(double minX, double maxX);
    QVector<QPointF> DecimateSamples(const QVector<QPointF>& samples, double minX, double maxX, int maxPoints) const;

    struct ChartItem {
        QChartView* view = nullptr;
        QVector<QLineSeries*> series;
        QVector<int> seriesIndices;
        QGraphicsLineItem* crosshair = nullptr;
        QVector<QGraphicsSimpleTextItem*> valueLabels;
        QGraphicsLineItem* zeroLine = nullptr;
        QRubberBand* rubberBand = nullptr;
        QPoint rubberOrigin;
        bool rubberActive = false;
        bool rubberShown = false;
        bool panning = false;
        QPoint panLastPos;
        bool reorderArmed = false;
        QPoint reorderStartPos;
    };

    pat::FormatDefinition format_;
    bool hasFormat_ = false;
    QString loadedFormatPath_;
    QString formatJsonText_;
    QString loadedDataPath_;
    QVector<pat::Series> series_;

    struct DisplayGroup {
        QVector<int> signalIndices;
        QString title;
        QString unit;
        bool merged = false;
    };
    QVector<DisplayGroup> displayGroups_;
    QVector<QVector<int>> mergedGroups_;
    double globalMinY_ = -1.0;
    double globalMaxY_ = 1.0;
    double globalMaxX_ = 0.0;
    bool hasGlobalRange_ = false;
    double currentMinX_ = 0.0;
    double currentMaxX_ = 0.0;
    bool hasCurrentXRange_ = false;
    double minXSpan_ = 1e-3;
    bool cursorActive_ = false;
    int maxVisiblePoints_ = 5000;

    SignalTreeWidget* signalTree_ = nullptr;
#ifdef PAT_ENABLE_QT_CHARTS
    QWidget* chartsContainer_ = nullptr;
    QSplitter* chartsSplitter_ = nullptr;
    QScrollArea* chartsScrollArea_ = nullptr;
    QVector<ChartItem> charts_;
    double sharedCursorX_ = 0.0;
#endif
    QLabel* statusLabel_ = nullptr;

    int chartHeight_ = 240;
    int minChartHeight_ = 120;
    int maxChartHeight_ = 480;
};
