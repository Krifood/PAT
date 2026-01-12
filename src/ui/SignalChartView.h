#pragma once

#include "core/RecordParser.h"

#include <QtCharts/QChartView>

#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QVector>

class QContextMenuEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QMouseEvent;
class QWheelEvent;
class QResizeEvent;
class QGraphicsLineItem;
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

class SignalChartView : public QChartView {
    Q_OBJECT

public:
    static constexpr const char* CHART_REORDER_MIME = "application/x-pat-chart-reorder";

    explicit SignalChartView(QWidget* parent = nullptr);

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
    void SetRangeContext(const ChartRangeContext& context);
    void SetXAxisRange(double minX, double maxX);
    void SetYAxisRange(double minY, double maxY);
    void SetCursorX(double cursorX);
    void HideCursor();
    void SetViewIndex(int index);
    int ViewIndex() const { return viewIndex_; }
    const QVector<int>& SeriesIndices() const { return seriesIndices_; }

signals:
    void CursorMoved(double cursorX);
    void CursorLeft();
    void XRangeRequested(double minX, double maxX);
    void ResetXRangeRequested();
    void MergeDropped(const QVector<int>& indices, int targetIndex);
    void ChartMergeRequested(int fromIndex, int toIndex);
    void ReorderRequested(int fromIndex, int toIndex);
    void HideSignalsRequested(const QVector<int>& indices);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
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
    void StartReorderDrag();
    void UpdateCursor(double cursorX);

    QVector<QLineSeries*> series_;
    QVector<int> seriesIndices_;
    const QVector<pat::Series>* sourceSeries_ = nullptr;

    QGraphicsLineItem* crosshair_ = nullptr;
    QVector<QGraphicsSimpleTextItem*> valueLabels_;
    QGraphicsLineItem* zeroLine_ = nullptr;
    QGraphicsSimpleTextItem* headerTitle_ = nullptr;
    QGraphicsSimpleTextItem* headerDesc_ = nullptr;
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
};
