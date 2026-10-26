#pragma once

#include "core/RecordParser.h"
#include "core/SeriesProcessing.h"
#include "ui/AppTheme.h"

#include <QtCharts/QChartView>

#include <QColor>
#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QVector>

class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QHideEvent;
class QLabel;
class QMouseEvent;
class QWheelEvent;
class QResizeEvent;
class QGraphicsLineItem;
class QGraphicsRectItem;
class QGraphicsSimpleTextItem;
class QLineSeries;
class QRubberBand;

struct ChartRangeContext {
    double globalMinX = 0.0;
    double globalMaxX = 0.0;
    double currentMinX = 0.0;
    double currentMaxX = 0.0;
    double minSpan = 1e-3;
    bool hasCurrentRange = false;
};

enum class GuideOrientation { Horizontal, Vertical };
struct GuideLine {
    GuideOrientation orientation = GuideOrientation::Horizontal;
    double value = 0.0;
};

class SignalChartView : public QChartView {
    Q_OBJECT

public:
    static constexpr const char* CHART_REORDER_MIME = "application/x-pat-chart-reorder";

    explicit SignalChartView(QWidget* parent = nullptr);

    void SetTheme(const AppTheme& theme);
    void Configure(const QString& title,
                   const QString& unit,
                   const QString& timeUnit,
                   bool showLegend,
                   const QVector<QColor>& palette,
                   const QVector<int>& seriesIndices,
                   const QVector<QVector<QPointF>>& seriesSamples,
                   const QVector<pat::Series>* sourceSeries,
                   double minY,
                   double maxY,
                   double minX,
                   double maxX,
                   int viewIndex);

    void SetSeriesSamples(const QVector<QVector<QPointF>>& seriesSamples);
    void SetExtremaText(const QStringList& lines);
    void SetDisplaySources(const QVector<const pat::Series*>& sources,
                           const QVector<pat::DisplayTransform>& transforms);
    void SetRangeContext(const ChartRangeContext& context);
    void SetXAxisRange(double minX, double maxX);
    void SetYAxisRange(double minY, double maxY);
    void ZoomY(double factor);
    void ResetYZoom();
    void SetCursorX(double cursorX);
    void HideCursor();
    void SetViewIndex(int index);
    void SetXAxisVisible(bool visible);
    void SetPlotLeftMargin(qreal margin);
    qreal YLabelWidth() const;
    int ViewIndex() const { return viewIndex_; }
    const QVector<int>& SeriesIndices() const { return seriesIndices_; }
    QVector<GuideLine> GuideLines() const;
    void SetGuideLines(const QVector<GuideLine>& guides);

signals:
    void FocusRequested();
    void GuidesEdited();
    void AxisLayoutChanged();
    void CursorMoved(double cursorX);
    void CursorLeft();
    void XRangeRequested(double minX, double maxX);
    void ResetXRangeRequested();
    void MergeDropped(const QVector<int>& indices, int targetIndex);
    void ChartMergeRequested(int fromIndex, int toIndex);
    void ReorderRequested(int fromIndex, int toIndex);
    void HideSignalsRequested(const QVector<int>& indices);
    void SplitRequested(const QVector<int>& indices);
    void TransformRequested(int signalIndex);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    QPointF MapToValue(const QPoint& viewPos) const;
    bool IsHeaderDragZone(const QPoint& viewPos) const;
    void UpdateZeroLine();
    void UpdateHeaderLayout();
    QGraphicsItem* InfoBlockAt(const QPoint& viewPos) const;
    void PlaceInfoBlocks();
    void RememberInfoBlock(QGraphicsItem* block);
    bool cursorBlocksDetached_ = false;
    QHash<QGraphicsItem*, QPointF> infoPositions_;
    QGraphicsItem* draggingInfo_ = nullptr;
    QPointF infoDragOffset_;
    double lastCursorX_ = 0.0;
    void StartReorderDrag();
    void UpdateCursor(double cursorX);
    void UpdateAlignedPlotArea();
    void SchedulePlotLayout();
    bool plotLayoutPending_ = false;
    bool updatingPlotLayout_ = false;
    void UpdateGuides();
    int GuideAt(const QPoint& viewPos) const;
    void AddGuide(GuideOrientation orientation, double value);
    void InputGuidePosition(GuideOrientation orientation, double initial, int index = -1);
    QLabel* mousePositionLabel_ = nullptr;
    QString GuideTooltip(int index, int maxEntries = 40) const;
    void ShowGuideTooltip(int index, const QPoint& globalPos);
    void HideGuideTooltip();

    struct GuideVisual {
        GuideLine guide;
        QGraphicsLineItem* item = nullptr;
        QGraphicsLineItem* outline = nullptr;
    };
    QVector<GuideVisual> guides_;
    int draggingGuide_ = -1;
    QLabel* guidePopup_ = nullptr;

    QVector<QLineSeries*> series_;
    AppTheme theme_ = ThemeFor(AppThemeId::IndoorDark);
    QVector<int> seriesIndices_;
    QVector<QVector<QLineSeries*>> segmentSeries_;
    const QVector<pat::Series>* sourceSeries_ = nullptr;
    QVector<const pat::Series*> displaySources_;
    QVector<pat::DisplayTransform> displayTransforms_;

    QGraphicsLineItem* crosshair_ = nullptr;
    QVector<QGraphicsSimpleTextItem*> valueLabels_;
    QGraphicsRectItem* legendPanel_ = nullptr;
    QVector<QGraphicsSimpleTextItem*> legendLabels_;
    QVector<QGraphicsLineItem*> legendSwatches_;
    QGraphicsLineItem* zeroLine_ = nullptr;
    QGraphicsSimpleTextItem* headerTitle_ = nullptr;
    QGraphicsSimpleTextItem* headerDesc_ = nullptr;
    QGraphicsSimpleTextItem* extremaLabel_ = nullptr;
    QStringList extremaLines_;
    QGraphicsSimpleTextItem* emptyMessage_ = nullptr;
    QString headerTitleText_;
    QString headerDescText_;
    QRubberBand* rubberBand_ = nullptr;

    ChartRangeContext rangeContext_;
    bool cursorActive_ = false;
    bool panning_ = false;
    QPoint panLastPos_;
    bool rubberActive_ = false;
    bool rubberShown_ = false;
    QPoint rubberOrigin_;
    bool reorderArmed_ = false;
    QPoint reorderStartPos_;
    int viewIndex_ = -1;
    qreal plotLeftMargin_ = 80.0;
    double yZoom_ = 1.0;
    double autoMinY_ = -1.0, autoMaxY_ = 1.0;
};
