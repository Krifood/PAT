#include "ui/SignalChartView.h"

#include "ui/SignalTreeWidget.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QGraphicsLineItem>
#include <QGraphicsLayout>
#include <QGraphicsSimpleTextItem>
#include <QFont>
#include <QFontMetricsF>
#include <QMenu>
#include <QEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPen>
#include <QRubberBand>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QSizeF>

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>

#include <algorithm>
#include <cmath>

namespace {

const QColor CHART_BACKGROUND(20, 20, 22);
const QColor PLOT_BACKGROUND(28, 28, 30);
const QColor AXIS_TEXT_COLOR(210, 210, 210);
const QColor AXIS_LINE_COLOR(140, 140, 140);
const QColor GRID_LINE_COLOR(60, 60, 60);
const QColor CURSOR_LINE_COLOR(255, 48, 48);
const QColor CURSOR_VALUE_COLOR(255, 215, 0);
const QColor ASSIST_LINE_COLOR(90, 90, 90);

}  // namespace

SignalChartView::SignalChartView(QWidget* parent) : QChartView(parent) {
    setRenderHint(QPainter::Antialiasing);
    setAcceptDrops(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setMouseTracking(true);
    if (viewport()) {
        viewport()->setAcceptDrops(true);
        viewport()->setMouseTracking(true);
    }
}

void SignalChartView::Configure(const QString& title,
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
                               int viewIndex) {
    seriesIndices_ = seriesIndices;
    sourceSeries_ = sourceSeries;
    viewIndex_ = viewIndex;

    auto* chart = new QChart();
    chart->setTitle(QString());
    chart->setBackgroundBrush(CHART_BACKGROUND);
    chart->setPlotAreaBackgroundVisible(true);
    chart->setPlotAreaBackgroundBrush(PLOT_BACKGROUND);
    chart->setTitleBrush(QBrush(AXIS_TEXT_COLOR));
    auto* legend = chart->legend();
    legend->setLabelColor(AXIS_TEXT_COLOR);
    legend->setVisible(showLegend);
    if (showLegend) {
        legend->detachFromChart();
        legend->setBackgroundVisible(true);
        legend->setBrush(QBrush(QColor(20, 20, 22, 180)));
        legend->setPen(QPen(QColor(90, 90, 90, 200)));
    }
    QFont titleFont = chart->titleFont();
    if (titleFont.pointSize() > 0) {
        titleFont.setPointSize(std::max(9, titleFont.pointSize() - 1));
        chart->setTitleFont(titleFont);
    }
    chart->setMargins(QMargins(4, 0, 4, 2));

    series_.clear();
    for (int i = 0; i < seriesIndices.size(); ++i) {
        auto* line = new QLineSeries(chart);
        const int colorIndex = palette.isEmpty() ? 0 : (i % palette.size());
        QPen pen(palette.isEmpty() ? Qt::white : palette[colorIndex]);
        pen.setWidthF(1.5);
        line->setPen(pen);
        const int dataIndex = seriesIndices[i];
        if (sourceSeries_ && dataIndex >= 0 && dataIndex < sourceSeries_->size()) {
            const auto& data = sourceSeries_->at(dataIndex);
            line->setName(data.unit.isEmpty() ? data.name : QStringLiteral("%1 (%2)").arg(data.name, data.unit));
        } else {
            line->setName(title);
        }
        if (i < seriesSamples.size()) {
            line->replace(seriesSamples[i]);
        }
        chart->addSeries(line);
        series_.append(line);
    }

    auto* axisX = new QValueAxis(chart);
    const QString timeTitle = timeUnit.trimmed().isEmpty() ? QObject::tr("时间") : QObject::tr("时间 (%1)").arg(timeUnit.trimmed());
    axisX->setTitleText(timeTitle);
    axisX->setRange(minX, maxX);
    axisX->setTitleBrush(QBrush(AXIS_TEXT_COLOR));
    axisX->setLabelsColor(AXIS_TEXT_COLOR);
    axisX->setLinePenColor(AXIS_LINE_COLOR);
    axisX->setGridLineColor(GRID_LINE_COLOR);
    chart->addAxis(axisX, Qt::AlignBottom);
    for (auto* line : series_) {
        line->attachAxis(axisX);
    }

    auto* axisY = new QValueAxis(chart);
    axisY->setRange(minY, maxY);
    axisY->setTitleBrush(QBrush(AXIS_TEXT_COLOR));
    axisY->setLabelsColor(AXIS_TEXT_COLOR);
    axisY->setLinePenColor(AXIS_LINE_COLOR);
    axisY->setGridLineColor(GRID_LINE_COLOR);
    chart->addAxis(axisY, Qt::AlignLeft);
    for (auto* line : series_) {
        line->attachAxis(axisY);
    }

    setChart(chart);

    headerTitleText_ = title.trimmed();
    headerDescText_ = unit.trimmed();
    headerTitle_ = new QGraphicsSimpleTextItem(chart);
    headerDesc_ = new QGraphicsSimpleTextItem(chart);
    QFont headerFont = chart->titleFont();
    if (headerFont.pointSize() > 0) {
        const int boosted = std::max(14, headerFont.pointSize() + 3);
        const int reduced = std::max(8, static_cast<int>(std::round(boosted * 0.5)));
        headerFont.setPointSize(reduced);
    } else if (headerFont.pixelSize() > 0) {
        const int boosted = std::max(16, headerFont.pixelSize() + 3);
        const int reduced = std::max(10, static_cast<int>(std::round(boosted * 0.5)));
        headerFont.setPixelSize(reduced);
    }
    headerTitle_->setFont(headerFont);
    headerDesc_->setFont(headerFont);
    headerTitle_->setBrush(QBrush(AXIS_TEXT_COLOR));
    headerDesc_->setBrush(QBrush(AXIS_TEXT_COLOR));
    headerTitle_->setZValue(900);
    headerDesc_->setZValue(900);
    if (legend && legend->isVisible()) {
        legend->setFont(headerFont);
    }
    UpdateHeaderLayout();

    if (!crosshair_) {
        crosshair_ = new QGraphicsLineItem();
        QPen cursorPen(CURSOR_LINE_COLOR);
        cursorPen.setStyle(Qt::DashLine);
        crosshair_->setPen(cursorPen);
        crosshair_->setZValue(1000);
        crosshair_->setVisible(false);
        chart->scene()->addItem(crosshair_);
    }

    if (!zeroLine_) {
        zeroLine_ = new QGraphicsLineItem();
        QPen assistPen(ASSIST_LINE_COLOR);
        assistPen.setStyle(Qt::DashLine);
        zeroLine_->setPen(assistPen);
        zeroLine_->setZValue(500);
        zeroLine_->setVisible(true);
        chart->scene()->addItem(zeroLine_);
    }

    valueLabels_.clear();
    for (int i = 0; i < series_.size(); ++i) {
        auto* label = new QGraphicsSimpleTextItem(chart);
        label->setBrush(CURSOR_VALUE_COLOR);
        label->setZValue(1001 + i);
        label->setVisible(false);
        valueLabels_.append(label);
    }

    if (!rubberBand_) {
        rubberBand_ = new QRubberBand(QRubberBand::Rectangle, viewport());
        rubberBand_->hide();
    }

    UpdateZeroLine();
}

void SignalChartView::SetSeriesSamples(const QVector<QVector<QPointF>>& seriesSamples) {
    for (int i = 0; i < series_.size() && i < seriesSamples.size(); ++i) {
        if (series_[i]) {
            series_[i]->replace(seriesSamples[i]);
        }
    }
}

void SignalChartView::SetRangeContext(const ChartRangeContext& context) {
    rangeContext_ = context;
}

void SignalChartView::SetXAxisRange(double minX, double maxX) {
    if (!chart() || series_.isEmpty()) return;
    auto* axisX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal, series_.first()).value(0));
    if (!axisX) return;
    axisX->setRange(minX, maxX);
    UpdateZeroLine();
    UpdateHeaderLayout();
}

void SignalChartView::SetYAxisRange(double minY, double maxY) {
    if (!chart() || series_.isEmpty()) return;
    auto* axisY = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    if (!axisY) return;
    axisY->setRange(minY, maxY);
    UpdateZeroLine();
    UpdateHeaderLayout();
}

void SignalChartView::SetCursorX(double cursorX) {
    cursorActive_ = true;
    UpdateCursor(cursorX);
}

void SignalChartView::HideCursor() {
    if (crosshair_) crosshair_->setVisible(false);
    for (auto* label : valueLabels_) {
        if (label) label->setVisible(false);
    }
    cursorActive_ = false;
}

void SignalChartView::SetViewIndex(int index) {
    viewIndex_ = index;
}

void SignalChartView::mouseMoveEvent(QMouseEvent* event) {
    if (reorderArmed_ && (event->buttons() & Qt::LeftButton)) {
        const QPoint viewPos = event->position().toPoint();
        if ((viewPos - reorderStartPos_).manhattanLength() >= QApplication::startDragDistance()) {
            StartReorderDrag();
            reorderArmed_ = false;
            return;
        }
    }

    if (panning_ && (event->buttons() & Qt::LeftButton)) {
        const QPoint viewPos = event->position().toPoint();
        const double span = rangeContext_.hasCurrentRange ? (rangeContext_.currentMaxX - rangeContext_.currentMinX)
                                                          : (rangeContext_.globalMaxX - rangeContext_.globalMinX);
        const QRectF plotArea = chart() ? chart()->plotArea() : QRectF();
        if (!plotArea.isEmpty() && span > 0.0) {
            const double dx = static_cast<double>(viewPos.x() - panLastPos_.x());
            const double deltaValue = dx / plotArea.width() * span;
            const double minX = (rangeContext_.hasCurrentRange ? rangeContext_.currentMinX : rangeContext_.globalMinX) - deltaValue;
            const double maxX = (rangeContext_.hasCurrentRange ? rangeContext_.currentMaxX : rangeContext_.globalMaxX) - deltaValue;
            emit XRangeRequested(minX, maxX);
            panLastPos_ = viewPos;
        }
        return;
    }

    if (rubberActive_ && (event->buttons() & Qt::LeftButton) && rubberBand_) {
        const QPoint currentPos = event->position().toPoint();
        const QRect rect(rubberOrigin_, currentPos);
        const QRect normalized = rect.normalized();
        if (!rubberShown_ && (normalized.width() + normalized.height()) >= QApplication::startDragDistance()) {
            rubberShown_ = true;
            rubberBand_->show();
        }
        if (rubberShown_) rubberBand_->setGeometry(normalized);
        return;
    }

    if (!series_.isEmpty() && chart()) {
        const QPoint viewPos = event->position().toPoint();
        const QPointF plotPoint = MapToValue(viewPos);
        double minX = rangeContext_.hasCurrentRange ? rangeContext_.currentMinX : rangeContext_.globalMinX;
        double maxX = rangeContext_.hasCurrentRange ? rangeContext_.currentMaxX : rangeContext_.globalMaxX;
        double cursorX = plotPoint.x();
        if (cursorX < minX) cursorX = minX;
        if (cursorX > maxX) cursorX = maxX;
        emit CursorMoved(cursorX);
    }
}

void SignalChartView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !chart()) return;

    const QPoint viewPos = event->position().toPoint();
    if (IsHeaderDragZone(viewPos)) {
        reorderArmed_ = true;
        reorderStartPos_ = viewPos;
        return;
    }

    if (event->modifiers() & Qt::AltModifier) {
        panning_ = true;
        panLastPos_ = viewPos;
        return;
    }

    if (rubberBand_) {
        rubberActive_ = true;
        rubberShown_ = false;
        rubberOrigin_ = viewPos;
        rubberBand_->setGeometry(QRect(rubberOrigin_, QSize()));
        rubberBand_->hide();
        emit CursorLeft();
    }
}

void SignalChartView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        reorderArmed_ = false;
        panning_ = false;

        if (rubberActive_ && rubberBand_) {
            rubberBand_->hide();
            const bool didShow = rubberShown_;
            rubberActive_ = false;
            rubberShown_ = false;

            if (didShow && chart() && !series_.isEmpty()) {
                const QPoint currentPos = event->position().toPoint();
                QRect rect(rubberOrigin_, currentPos);
                rect = rect.normalized();
                if (rect.width() >= 6) {
                    const QPointF topLeft = mapToScene(rect.topLeft());
                    const QPointF bottomRight = mapToScene(rect.bottomRight());
                    const QPointF chartTopLeft = chart()->mapFromScene(topLeft);
                    const QPointF chartBottomRight = chart()->mapFromScene(bottomRight);
                    const QPointF p1 = chart()->mapToValue(chartTopLeft, series_.first());
                    const QPointF p2 = chart()->mapToValue(chartBottomRight, series_.first());
                    const double minX = std::min(p1.x(), p2.x());
                    const double maxX = std::max(p1.x(), p2.x());
                    emit XRangeRequested(minX, maxX);
                }
            }
        }
    }
}

void SignalChartView::wheelEvent(QWheelEvent* event) {
    if (!chart() || series_.isEmpty()) return;

    const double currentMin = rangeContext_.hasCurrentRange ? rangeContext_.currentMinX : rangeContext_.globalMinX;
    const double currentMax = rangeContext_.hasCurrentRange ? rangeContext_.currentMaxX : rangeContext_.globalMaxX;
    if (currentMax <= currentMin) return;

    const double delta = static_cast<double>(event->angleDelta().y());
    if (delta == 0.0) return;
    const double zoomFactor = delta > 0.0 ? 0.8 : 1.25;

    const QPoint viewPos = event->position().toPoint();
    const QPointF plotPoint = MapToValue(viewPos);
    const double centerX = plotPoint.x();
    const double leftSpan = centerX - currentMin;
    const double rightSpan = currentMax - centerX;
    const double newMin = centerX - leftSpan * zoomFactor;
    const double newMax = centerX + rightSpan * zoomFactor;

    emit XRangeRequested(newMin, newMax);
    event->accept();
}

void SignalChartView::leaveEvent(QEvent* event) {
    Q_UNUSED(event)
    emit CursorLeft();
}

bool SignalChartView::viewportEvent(QEvent* event) {
    if (event && event->type() == QEvent::ContextMenu) {
        auto* ctx = static_cast<QContextMenuEvent*>(event);
        contextMenuEvent(ctx);
        return true;
    }
    return QChartView::viewportEvent(event);
}

void SignalChartView::resizeEvent(QResizeEvent* event) {
    QChartView::resizeEvent(event);
    UpdateHeaderLayout();
    UpdateZeroLine();
}

void SignalChartView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    auto* resetAction = menu.addAction(tr("还原时间轴"));
    connect(resetAction, &QAction::triggered, this, &SignalChartView::ResetXRangeRequested);
    if (!seriesIndices_.isEmpty()) {
        menu.addAction(tr("取消显示本图信号"), this, [this]() {
            emit HideSignalsRequested(seriesIndices_);
        });
    }
    menu.exec(event->globalPos());
    event->accept();
}

void SignalChartView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() &&
        (event->mimeData()->hasFormat(SignalTreeWidget::SIGNAL_INDICES_MIME) ||
         event->mimeData()->hasFormat(CHART_REORDER_MIME))) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void SignalChartView::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData() &&
        (event->mimeData()->hasFormat(SignalTreeWidget::SIGNAL_INDICES_MIME) ||
         event->mimeData()->hasFormat(CHART_REORDER_MIME))) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void SignalChartView::dropEvent(QDropEvent* event) {
    if (!event->mimeData()) {
        event->ignore();
        return;
    }

    if (event->mimeData()->hasFormat(CHART_REORDER_MIME)) {
        QByteArray payload = event->mimeData()->data(CHART_REORDER_MIME);
        QDataStream stream(&payload, QIODevice::ReadOnly);
        int fromIndex = -1;
        stream >> fromIndex;
        if (fromIndex < 0 || fromIndex == viewIndex_) {
            event->ignore();
            return;
        }
        const QPoint viewPos = event->position().toPoint();
        if (IsHeaderDragZone(viewPos)) {
            emit ReorderRequested(fromIndex, viewIndex_);
        } else {
            emit ChartMergeRequested(fromIndex, viewIndex_);
        }
        event->acceptProposedAction();
        return;
    }

    if (event->mimeData()->hasFormat(SignalTreeWidget::SIGNAL_INDICES_MIME)) {
        QByteArray payload = event->mimeData()->data(SignalTreeWidget::SIGNAL_INDICES_MIME);
        QDataStream stream(&payload, QIODevice::ReadOnly);
        QVector<int> dropped;
        stream >> dropped;

        QVector<int> merged = seriesIndices_;
        for (int idx : dropped) {
            if (idx >= 0 && !merged.contains(idx)) merged.append(idx);
        }
        if (merged.size() >= 2) {
            emit MergeDropped(merged, viewIndex_);
            event->acceptProposedAction();
            return;
        }
    }

    event->ignore();
}

QPointF SignalChartView::MapToValue(const QPoint& viewPos) const {
    if (!chart() || series_.isEmpty()) return {};
    const QPointF scenePos = mapToScene(viewPos);
    const QPointF chartPos = chart()->mapFromScene(scenePos);
    return chart()->mapToValue(chartPos, series_.first());
}

bool SignalChartView::IsHeaderDragZone(const QPoint& viewPos) const {
    if (!chart()) return false;
    const QRectF plotArea = chart()->plotArea();
    if (plotArea.isEmpty()) return false;

    const QPointF chartPos = chart()->mapFromScene(mapToScene(viewPos));
    qreal headerHeight = 0.0;
    if (headerTitle_) {
        headerHeight = QFontMetricsF(headerTitle_->font()).height();
    }
    const qreal dragBand = std::max(6.0, headerHeight + 4.0);
    return chartPos.y() < plotArea.top() + dragBand;
}

void SignalChartView::UpdateZeroLine() {
    if (!zeroLine_ || !chart() || series_.isEmpty()) return;
    auto* axisY = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    auto* axisX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal, series_.first()).value(0));
    if (!axisY || !axisX) return;

    const double yMin = axisY->min();
    const double yMax = axisY->max();
    if (0.0 < yMin || 0.0 > yMax) {
        zeroLine_->setVisible(false);
        return;
    }

    const double minX = axisX->min();
    const double maxX = axisX->max();
    const QPointF left = chart()->mapToPosition(QPointF(minX, 0.0), series_.first());
    const QPointF right = chart()->mapToPosition(QPointF(maxX, 0.0), series_.first());
    zeroLine_->setLine(QLineF(chart()->mapToScene(left), chart()->mapToScene(right)));
    zeroLine_->setVisible(true);
}

void SignalChartView::UpdateHeaderLayout() {
    if (!chart() || !headerTitle_ || !headerDesc_) return;
    const QRectF plotArea = chart()->plotArea();
    if (plotArea.isEmpty()) return;

    const qreal padding = 4.0;
    const qreal gap = 10.0;
    const qreal y = plotArea.top() + 2.0;

    const QFontMetricsF titleMetrics(headerTitle_->font());
    const QFontMetricsF descMetrics(headerDesc_->font());

    QString titleText = headerTitleText_;
    QString descText = headerDescText_;
    const qreal centerX = plotArea.center().x();
    const qreal leftRegionLeft = plotArea.left() + padding;
    const qreal leftRegionRight = centerX - gap * 0.5;
    const qreal rightRegionLeft = centerX + gap * 0.5;
    const qreal rightRegionRight = plotArea.right() - padding;
    const qreal leftWidth = std::max(0.0, leftRegionRight - leftRegionLeft);
    const qreal rightWidth = std::max(0.0, rightRegionRight - rightRegionLeft);

    const QString elidedTitle = titleMetrics.elidedText(titleText, Qt::ElideRight, static_cast<int>(leftWidth));
    const qreal titleWidth = titleMetrics.horizontalAdvance(elidedTitle);
    qreal titleX = leftRegionLeft + (leftWidth - titleWidth) * 0.5;
    if (titleX < leftRegionLeft) titleX = leftRegionLeft;
    headerTitle_->setText(elidedTitle);
    headerTitle_->setPos(titleX, y);

    if (descText.isEmpty()) {
        headerDesc_->setVisible(false);
    } else {
        const QString elidedDesc = descMetrics.elidedText(descText, Qt::ElideLeft, static_cast<int>(rightWidth));
        const qreal descWidth = descMetrics.horizontalAdvance(elidedDesc);
        qreal descX = rightRegionLeft + (rightWidth - descWidth) * 0.5;
        if (descX < rightRegionLeft) descX = rightRegionLeft;
        headerDesc_->setText(elidedDesc);
        headerDesc_->setPos(descX, y);
        headerDesc_->setVisible(true);
    }

    auto* legend = chart()->legend();
    if (legend && legend->isVisible()) {
        legend->setFont(headerTitle_->font());
        const QFontMetricsF legendMetrics(legend->font());
        qreal maxLabelWidth = 0.0;
        const auto markers = legend->markers();
        for (const auto* marker : markers) {
            if (!marker) continue;
            maxLabelWidth = std::max(maxLabelWidth, legendMetrics.horizontalAdvance(marker->label()));
        }
        const qreal markerPadding = std::max(18.0, legendMetrics.height() * 1.6);
        qreal legendWidth = maxLabelWidth + markerPadding + 8.0;
        const qreal maxWidth = plotArea.width() * 0.45;
        legendWidth = std::clamp(legendWidth, 90.0, std::max(120.0, maxWidth));

        const qreal headerHeight = std::max(titleMetrics.height(), descMetrics.height());
        const qreal legendX = plotArea.right() - padding - legendWidth;
        const qreal legendY = plotArea.top() + headerHeight + 6.0;
        const qreal maxHeight = std::max(30.0, plotArea.bottom() - padding - legendY);
        QSizeF legendSize;
        if (auto* layout = legend->layout()) {
            layout->invalidate();
            legendSize = layout->effectiveSizeHint(Qt::PreferredSize, QSizeF(legendWidth, maxHeight));
        }
        if (legendSize.isEmpty()) {
            legendSize = legend->boundingRect().size();
        }
        if (legendSize.isEmpty()) {
            legendSize = QSizeF(legendWidth, std::min(60.0, maxHeight));
        }
        legendSize.setWidth(legendWidth);
        if (legendSize.height() > maxHeight) legendSize.setHeight(maxHeight);
        legend->setGeometry(QRectF(QPointF(legendX, legendY), legendSize));
    }
}

void SignalChartView::StartReorderDrag() {
    if (viewIndex_ < 0) return;
    auto* drag = new QDrag(this);
    auto* mime = new QMimeData();
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << viewIndex_;
    mime->setData(CHART_REORDER_MIME, payload);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
}

void SignalChartView::UpdateCursor(double cursorX) {
    if (!chart() || series_.isEmpty() || !sourceSeries_) return;

    auto* axisY = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    if (!axisY) return;

    const double yMin = axisY->min();
    const double yMax = axisY->max();
    const QPointF top = chart()->mapToPosition(QPointF(cursorX, yMax), series_.first());
    const QPointF bottom = chart()->mapToPosition(QPointF(cursorX, yMin), series_.first());
    if (crosshair_) {
        crosshair_->setLine(QLineF(chart()->mapToScene(top), chart()->mapToScene(bottom)));
        crosshair_->setVisible(true);
    }

    for (int i = 0; i < valueLabels_.size() && i < seriesIndices_.size(); ++i) {
        const int idx = seriesIndices_[i];
        if (idx < 0 || idx >= sourceSeries_->size()) continue;
        const auto& seriesData = sourceSeries_->at(idx);
        if (seriesData.samples.isEmpty()) continue;

        const double seriesMinX = seriesData.samples.first().x();
        const double seriesMaxX = seriesData.samples.last().x();
        double clampedX = cursorX;
        if (clampedX < seriesMinX) clampedX = seriesMinX;
        if (clampedX > seriesMaxX) clampedX = seriesMaxX;

        auto begin = seriesData.samples.begin();
        auto end = seriesData.samples.end();
        auto it = std::lower_bound(begin, end, clampedX, [](const QPointF& p, double x) { return p.x() < x; });
        double value = 0.0;
        if (it == begin) {
            value = it->y();
        } else if (it == end) {
            value = (end - 1)->y();
        } else {
            const QPointF p0 = *(it - 1);
            const QPointF p1 = *it;
            const double dx = p1.x() - p0.x();
            value = dx == 0.0 ? p1.y() : (p0.y() + (p1.y() - p0.y()) * (clampedX - p0.x()) / dx);
        }

        auto* label = valueLabels_[i];
        if (!label) continue;
        label->setText(QStringLiteral("t=%1  val=%2").arg(cursorX, 0, 'f', 2).arg(value, 0, 'f', 4));
        auto* seriesRef = series_.value(i);
        if (!seriesRef) seriesRef = series_.first();
        const QPointF valuePoint = chart()->mapToPosition(QPointF(cursorX, value), seriesRef);
        const QPointF labelPos = valuePoint + QPointF(6, -12 - (12 * i));
        label->setPos(labelPos);
        label->setVisible(true);
    }
}
