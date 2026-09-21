#include "ui/SignalChartView.h"

#include "ui/SignalTreeWidget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
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
#include <QToolTip>
#include <QStringList>

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const QColor AXIS_TEXT_COLOR(210, 210, 210);
const QColor AXIS_LINE_COLOR(140, 140, 140);
const QColor GRID_LINE_COLOR(60, 60, 60);
const QColor CURSOR_LINE_COLOR(255, 48, 48);
const QColor CURSOR_VALUE_COLOR(255, 215, 0);
const QColor ASSIST_LINE_COLOR(90, 90, 90);

QString YAxisLabelFormat(double minY, double maxY) {
    const double magnitude = std::max(std::abs(minY), std::abs(maxY));
    const double step = (maxY - minY) / 2.0;
    if ((magnitude >= 1e6 || (step > 0.0 && step < 1e-6)) && magnitude > 0.0 && step > 0.0) {
        const int exponent = static_cast<int>(std::floor(std::log10(magnitude)));
        const int precision = std::clamp(static_cast<int>(std::ceil(exponent - std::log10(step))), 1, 12);
        return QStringLiteral("%.") + QString::number(precision) + QLatin1Char('e');
    }
    if (step <= 0.0 || step >= 0.1) return QStringLiteral("%.1f");
    const int decimals = std::max(1, static_cast<int>(std::ceil(-std::log10(step))) + 1);
    return QStringLiteral("%.") + QString::number(decimals) + QLatin1Char('f');
}

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
    displaySources_.clear();
    displayTransforms_.clear();
    for (int dataIndex : seriesIndices) {
        displaySources_.append(sourceSeries_ && dataIndex >= 0 && dataIndex < sourceSeries_->size()
                                   ? &sourceSeries_->at(dataIndex)
                                   : nullptr);
        displayTransforms_.append(pat::DisplayTransform{});
    }
    viewIndex_ = viewIndex;

    auto* chart = new QChart();
    chart->setTitle(QString());
    chart->setBackgroundBrush(viewIndex % 2 == 0 ? QColor(25, 37, 46) : QColor(30, 43, 50));
    chart->setBackgroundPen(QPen(viewIndex % 2 == 0 ? QColor(77, 109, 126)
                                                : QColor(92, 117, 123), 1.5));
    chart->setPlotAreaBackgroundVisible(true);
    chart->setPlotAreaBackgroundBrush(viewIndex % 2 == 0 ? QColor(20, 29, 36)
                                                      : QColor(24, 33, 39));
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
    axisX->setTitleText(timeUnit);
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
    axisY->setTickCount(3);
    axisY->setLabelFormat(YAxisLabelFormat(minY, maxY));
    axisY->setTitleBrush(QBrush(AXIS_TEXT_COLOR));
    axisY->setLabelsColor(AXIS_TEXT_COLOR);
    axisY->setLinePenColor(AXIS_LINE_COLOR);
    axisY->setGridLineColor(GRID_LINE_COLOR);
    chart->addAxis(axisY, Qt::AlignLeft);
    for (auto* line : series_) {
        line->attachAxis(axisY);
    }

    setChart(chart);
    connect(chart, &QChart::plotAreaChanged, this, [this](const QRectF&) {
        UpdateHeaderLayout();
        UpdateZeroLine();
        UpdateGuides();
    });

    headerTitleText_ = title.trimmed();
    headerDescText_ = unit.trimmed();
    headerTitle_ = new QGraphicsSimpleTextItem(chart);
    headerDesc_ = new QGraphicsSimpleTextItem(chart);
    QFont headerFont = chart->titleFont();
    if (headerFont.pointSize() > 0) {
        const int boosted = std::max(14, headerFont.pointSize() + 3);
        const int reduced = std::max(8, static_cast<int>(std::round(boosted * 0.5)));
        headerFont.setPointSize(std::max(11, static_cast<int>(std::round(reduced * 1.3))));
    } else if (headerFont.pixelSize() > 0) {
        const int boosted = std::max(16, headerFont.pixelSize() + 3);
        const int reduced = std::max(10, static_cast<int>(std::round(boosted * 0.5)));
        headerFont.setPixelSize(std::max(13, static_cast<int>(std::round(reduced * 1.3))));
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
    UpdateAlignedPlotArea();
}

void SignalChartView::SetSeriesSamples(const QVector<QVector<QPointF>>& seriesSamples) {
    for (int i = 0; i < series_.size() && i < seriesSamples.size(); ++i) {
        if (series_[i]) {
            series_[i]->replace(seriesSamples[i]);
        }
    }
}

void SignalChartView::SetDisplaySources(const QVector<const pat::Series*>& sources,
                                        const QVector<pat::DisplayTransform>& transforms) {
    displaySources_ = sources;
    displayTransforms_ = transforms;
    while (displayTransforms_.size() < displaySources_.size()) {
        displayTransforms_.append(pat::DisplayTransform{});
    }
    for (int i = 0; i < series_.size() && i < displaySources_.size(); ++i) {
        const auto* source = displaySources_[i];
        if (!series_[i] || !source) continue;
        QString label = source->unit.isEmpty() ? source->name
            : tr("%1 (%2)").arg(source->name, source->unit);
        const auto transform = displayTransforms_.value(i);
        if (!transform.IsIdentity()) {
            label += tr(" [×%1, 平移 %2]")
                         .arg(transform.scale, 0, 'g', 8)
                         .arg(transform.offset, 0, 'g', 8);
        }
        series_[i]->setName(label);
    }
    if (displaySources_.size() == 1 && displaySources_.first()
        && !displayTransforms_.value(0).IsIdentity()) {
        const auto transform = displayTransforms_.value(0);
        headerDescText_ = tr("%1  显示值=解析值×%2%3%4")
                              .arg(displaySources_.first()->unit)
                              .arg(transform.scale, 0, 'g', 8)
                              .arg(transform.offset >= 0.0 ? QStringLiteral("+") : QString())
                              .arg(transform.offset, 0, 'g', 8);
        UpdateHeaderLayout();
    }
}

QVector<GuideLine> SignalChartView::GuideLines() const {
    QVector<GuideLine> result;
    result.reserve(guides_.size());
    for (const auto& visual : guides_) result.append(visual.guide);
    return result;
}

void SignalChartView::SetGuideLines(const QVector<GuideLine>& guides) {
    for (const auto& visual : guides_) {
        delete visual.item;
        delete visual.outline;
    }
    guides_.clear();
    for (const auto& guide : guides) AddGuide(guide.orientation, guide.value);
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
    UpdateGuides();
}

void SignalChartView::SetYAxisRange(double minY, double maxY) {
    if (!chart() || series_.isEmpty()) return;
    auto* axisY = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    if (!axisY) return;
    axisY->setRange(minY, maxY);
    axisY->setLabelFormat(YAxisLabelFormat(minY, maxY));
    UpdateAlignedPlotArea();
    UpdateZeroLine();
    UpdateHeaderLayout();
    UpdateGuides();
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

void SignalChartView::SetXAxisVisible(bool visible) {
    if (!chart() || series_.isEmpty()) return;
    auto* axis = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal, series_.first()).value(0));
    if (!axis) return;
    axis->setVisible(visible);
    axis->setTitleVisible(false);
    UpdateAlignedPlotArea();
}

void SignalChartView::SetPlotLeftMargin(qreal margin) {
    if (qFuzzyCompare(plotLeftMargin_, margin)) return;
    plotLeftMargin_ = margin;
    UpdateAlignedPlotArea();
}

qreal SignalChartView::YLabelWidth() const {
    if (!chart() || series_.isEmpty()) return 0.0;
    auto* axis = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    if (!axis) return 0.0;
    const QFontMetricsF metrics(axis->labelsFont());
    const QByteArray labelFormat = axis->labelFormat().toLatin1();
    return std::max(metrics.horizontalAdvance(QString::asprintf(labelFormat.constData(), axis->min())),
                    metrics.horizontalAdvance(QString::asprintf(labelFormat.constData(), axis->max())));
}

void SignalChartView::mouseMoveEvent(QMouseEvent* event) {
    if (draggingGuide_ >= 0 && draggingGuide_ < guides_.size()
        && (event->buttons() & Qt::LeftButton)) {
        const QPointF point = MapToValue(event->position().toPoint());
        auto& guide = guides_[draggingGuide_].guide;
        if (guide.orientation == GuideOrientation::Horizontal) {
            auto* axis = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
            if (axis) guide.value = std::clamp(point.y(), axis->min(), axis->max());
        } else {
            auto* axis = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal, series_.first()).value(0));
            if (axis) guide.value = std::clamp(point.x(), axis->min(), axis->max());
        }
        UpdateGuides();
        emit CursorMoved(MapToValue(event->position().toPoint()).x());
        QToolTip::hideText();
        return;
    }

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
        const double minX = rangeContext_.hasCurrentRange ? rangeContext_.currentMinX : rangeContext_.globalMinX;
        const double maxX = rangeContext_.hasCurrentRange ? rangeContext_.currentMaxX : rangeContext_.globalMaxX;
        const double cursorX = std::clamp(plotPoint.x(), minX, maxX);
        emit CursorMoved(cursorX);
        const int guideIndex = GuideAt(viewPos);
        if (guideIndex >= 0) {
            setCursor(Qt::SizeAllCursor);
            QToolTip::showText(event->globalPosition().toPoint(),
                               GuideTooltip(guideIndex), this);
            return;
        }
        unsetCursor();
        QToolTip::hideText();
    }
}

void SignalChartView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !chart()) return;

    const QPoint viewPos = event->position().toPoint();
    const int guideIndex = GuideAt(viewPos);
    if (guideIndex >= 0) {
        draggingGuide_ = guideIndex;
        return;
    }
    const QPointF chartPos = chart()->mapFromScene(mapToScene(viewPos));
    const QRectF plot = chart()->plotArea();
    if (!series_.isEmpty() && chartPos.x() >= plot.left() && chartPos.x() <= plot.right()
        && chartPos.y() > plot.bottom() && chartPos.y() <= chart()->size().height()) {
        auto* axis = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
        if (axis) {
            AddGuide(GuideOrientation::Horizontal, axis->min());
            draggingGuide_ = guides_.size() - 1;
            return;
        }
    }
    if (!series_.isEmpty() && chartPos.y() >= plot.top() && chartPos.y() <= plot.bottom()
        && chartPos.x() < plot.left() && chartPos.x() >= 0) {
        auto* axis = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal, series_.first()).value(0));
        if (axis) {
            AddGuide(GuideOrientation::Vertical, axis->min());
            draggingGuide_ = guides_.size() - 1;
            return;
        }
    }
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
        if (draggingGuide_ >= 0) {
            draggingGuide_ = -1;
            return;
        }
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
    QToolTip::hideText();
    unsetCursor();
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
    UpdateAlignedPlotArea();
    UpdateHeaderLayout();
    UpdateZeroLine();
    UpdateGuides();
}

void SignalChartView::UpdateAlignedPlotArea() {
    if (!chart() || series_.isEmpty()) return;
    const qreal ownWidth = YLabelWidth();
    const qreal extra = std::max(0.0, plotLeftMargin_ - ownWidth - 48.0);
    chart()->setMargins(QMargins(static_cast<int>(std::ceil(4.0 + extra)), 0, 4, 2));
    UpdateGuides();
}

void SignalChartView::AddGuide(GuideOrientation orientation, double value) {
    if (!chart() || series_.isEmpty()) return;
    auto* outline = new QGraphicsLineItem(chart());
    QPen outlinePen(QColor(9, 16, 22));
    outlinePen.setWidthF(5.0);
    outline->setPen(outlinePen);
    outline->setZValue(959);
    auto* item = new QGraphicsLineItem(chart());
    QPen pen(QColor(255, 227, 90));
    pen.setWidthF(2.5);
    item->setPen(pen);
    item->setZValue(960);
    guides_.append({{orientation, value}, item, outline});
    UpdateGuides();
}

void SignalChartView::UpdateGuides() {
    if (!chart() || series_.isEmpty()) return;
    const QRectF plot = chart()->plotArea();
    auto* axisX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal, series_.first()).value(0));
    auto* axisY = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    if (!axisX || !axisY || plot.isEmpty()) return;
    for (auto& visual : guides_) {
        if (!visual.item) continue;
        if (visual.guide.orientation == GuideOrientation::Horizontal) {
            const double y = visual.guide.value;
            const bool visible = y >= axisY->min() && y <= axisY->max();
            visual.item->setVisible(visible);
            visual.outline->setVisible(visible);
            if (visible) {
                const qreal py = chart()->mapToPosition(QPointF(axisX->min(), y), series_.first()).y();
                visual.item->setLine(QLineF(plot.left(), py, plot.right(), py));
                visual.outline->setLine(visual.item->line());
            }
        } else {
            const double x = visual.guide.value;
            const bool visible = x >= axisX->min() && x <= axisX->max();
            visual.item->setVisible(visible);
            visual.outline->setVisible(visible);
            if (visible) {
                const qreal px = chart()->mapToPosition(QPointF(x, axisY->min()), series_.first()).x();
                visual.item->setLine(QLineF(px, plot.top(), px, plot.bottom()));
                visual.outline->setLine(visual.item->line());
            }
        }
    }
}

int SignalChartView::GuideAt(const QPoint& viewPos) const {
    if (!chart()) return -1;
    const QPointF chartPos = chart()->mapFromScene(mapToScene(viewPos));
    const QRectF plot = chart()->plotArea().adjusted(-6, -6, 6, 6);
    if (!plot.contains(chartPos)) return -1;
    for (int i = guides_.size() - 1; i >= 0; --i) {
        const auto& visual = guides_[i];
        if (!visual.item || !visual.item->isVisible()) continue;
        const QLineF line = visual.item->line();
        const qreal distance = visual.guide.orientation == GuideOrientation::Horizontal
                                   ? std::abs(chartPos.y() - line.y1())
                                   : std::abs(chartPos.x() - line.x1());
        if (distance <= 6.0) return i;
    }
    return -1;
}

QString SignalChartView::GuideTooltip(int index, int maxEntries) const {
    if (index < 0 || index >= guides_.size()) return {};
    const GuideLine guide = guides_[index].guide;
    QStringList rows;
    rows.append(guide.orientation == GuideOrientation::Horizontal
                    ? tr("横向辅助线 val=%1").arg(guide.value, 0, 'g', 12)
                    : tr("纵向辅助线 key=%1").arg(guide.value, 0, 'g', 12));
    const double visibleMinX = rangeContext_.hasCurrentRange ? rangeContext_.currentMinX : rangeContext_.globalMinX;
    const double visibleMaxX = rangeContext_.hasCurrentRange ? rangeContext_.currentMaxX : rangeContext_.globalMaxX;
    int intersectionCount = 0;
    const auto appendCrossing = [&](const QString& name, double x, double y, double endX = std::numeric_limits<double>::quiet_NaN()) {
        ++intersectionCount;
        if (intersectionCount > maxEntries) return;
        const QString xText = std::isnan(endX)
                                  ? QString::number(x, 'g', 12)
                                  : tr("%1～%2").arg(QString::number(x, 'g', 12), QString::number(endX, 'g', 12));
        rows.append(tr("%1：key=%2  val=%3").arg(name, xText, QString::number(y, 'g', 12)));
    };
    for (int seriesIndex = 0; seriesIndex < displaySources_.size(); ++seriesIndex) {
        const auto* dataPtr = displaySources_[seriesIndex];
        if (!dataPtr) continue;
        const auto& data = *dataPtr;
        const auto transform = displayTransforms_.value(seriesIndex);
        const QString label = data.unit.isEmpty() ? data.name : tr("%1 (%2)").arg(data.name, data.unit);
        const auto& samples = data.samples;
        if (samples.isEmpty()) continue;
        if (guide.orientation == GuideOrientation::Vertical) {
            const double x = guide.value;
            if (x >= visibleMinX && x <= visibleMaxX
                && x >= samples.first().x() && x <= samples.last().x()) {
                auto it = std::lower_bound(samples.begin(), samples.end(), x,
                                           [](const QPointF& point, double key) { return point.x() < key; });
                if (it != samples.end()) {
                    double y = it->y();
                    if (it != samples.begin()) {
                        const QPointF p0 = *(it - 1);
                        const QPointF p1 = *it;
                        const double dx = p1.x() - p0.x();
                        y = dx == 0.0 ? p1.y() : p0.y() + (p1.y() - p0.y()) * (x - p0.x()) / dx;
                    }
                    appendCrossing(label, x, transform.Apply(y));
                }
            }
        } else {
            const double y = guide.value;
            if (samples.size() == 1 && samples.first().x() >= visibleMinX && samples.first().x() <= visibleMaxX
                && transform.Apply(samples.first().y()) == y) {
                appendCrossing(label, samples.first().x(), y);
            }
            const auto firstVisible = std::lower_bound(samples.begin(), samples.end(), visibleMinX,
                                                       [](const QPointF& point, double key) {
                                                           return point.x() < key;
                                                       });
            const auto afterVisible = std::upper_bound(firstVisible, samples.end(), visibleMaxX,
                                                       [](double key, const QPointF& point) {
                                                           return key < point.x();
                                                       });
            const qsizetype start = std::max<qsizetype>(1, firstVisible - samples.begin());
            const qsizetype end = std::min<qsizetype>(samples.size() - 1, afterVisible - samples.begin());
            double lastX = std::numeric_limits<double>::quiet_NaN();
            for (qsizetype i = start; i <= end; ++i) {
                QPointF p0 = samples[i - 1];
                QPointF p1 = samples[i];
                p0.setY(transform.Apply(p0.y()));
                p1.setY(transform.Apply(p1.y()));
                if (p1.x() < visibleMinX || p0.x() > visibleMaxX) continue;
                if (y < std::min(p0.y(), p1.y()) || y > std::max(p0.y(), p1.y())) continue;
                const double dy = p1.y() - p0.y();
                if (dy == 0.0) {
                    const double from = std::max(visibleMinX, p0.x());
                    const double to = std::min(visibleMaxX, p1.x());
                    appendCrossing(label, from, y, to);
                    lastX = to;
                    continue;
                }
                const double x = p0.x() + (y - p0.y()) * (p1.x() - p0.x()) / dy;
                if (x < visibleMinX || x > visibleMaxX) continue;
                if (std::isfinite(lastX) && std::abs(x - lastX) <= 1e-9 * std::max(1.0, std::abs(x))) continue;
                appendCrossing(label, x, y);
                lastX = x;
            }
        }
    }
    if (intersectionCount == 0) rows.append(tr("当前可见范围内无交点"));
    if (intersectionCount > maxEntries) {
        rows.append(tr("另有 %1 个交点；右键辅助线可复制全部").arg(intersectionCount - maxEntries));
    }
    return rows.join('\n');
}

void SignalChartView::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    const int guideIndex = GuideAt(mapFromGlobal(event->globalPos()));
    if (guideIndex >= 0) {
        menu.addAction(tr("删除辅助线"), this, [this, guideIndex]() {
            delete guides_[guideIndex].item;
            delete guides_[guideIndex].outline;
            guides_.removeAt(guideIndex);
            QToolTip::hideText();
        });
        menu.addAction(tr("复制本线全部交点"), this, [this, guideIndex]() {
            QApplication::clipboard()->setText(
                GuideTooltip(guideIndex, std::numeric_limits<int>::max()));
        });
        menu.addSeparator();
    }
    const QPointF value = MapToValue(mapFromGlobal(event->globalPos()));
    menu.addAction(tr("添加横向辅助线"), this, [this, value]() {
        AddGuide(GuideOrientation::Horizontal, value.y());
    });
    menu.addAction(tr("添加纵向辅助线"), this, [this, value]() {
        AddGuide(GuideOrientation::Vertical, value.x());
    });
    if (!guides_.isEmpty()) {
        menu.addAction(tr("清除本图辅助线"), this, [this]() { SetGuideLines({}); });
    }
    menu.addSeparator();
    auto* resetAction = menu.addAction(tr("还原横轴"));
    connect(resetAction, &QAction::triggered, this, &SignalChartView::ResetXRangeRequested);
    if (seriesIndices_.size() > 1) {
        menu.addAction(tr("打散组合画布"), this, [this]() {
            emit SplitRequested(seriesIndices_);
        });
    }
    QVector<int> transformable;
    for (int signalIndex : seriesIndices_) {
        if (signalIndex >= 0) transformable.append(signalIndex);
    }
    if (!transformable.isEmpty()) {
        auto* transformMenu = menu.addMenu(tr("显示比例与平移"));
        for (int signalIndex : transformable) {
            QString label = tr("信号 %1").arg(signalIndex);
            const int position = seriesIndices_.indexOf(signalIndex);
            if (position >= 0 && position < displaySources_.size() && displaySources_[position]) {
                label = displaySources_[position]->name;
            }
            transformMenu->addAction(label, this, [this, signalIndex]() {
                emit TransformRequested(signalIndex);
            });
        }
    }
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
    if (!chart() || series_.isEmpty()) return;

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

    for (int i = 0; i < valueLabels_.size() && i < displaySources_.size(); ++i) {
        if (!displaySources_[i]) continue;
        const auto& seriesData = *displaySources_[i];
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

        value = displayTransforms_.value(i).Apply(value);
        auto* label = valueLabels_[i];
        if (!label) continue;
        label->setText(QStringLiteral("key=%1\nval=%2").arg(cursorX, 0, 'f', 2).arg(value, 0, 'f', 4));
        auto* seriesRef = series_.value(i);
        if (!seriesRef) seriesRef = series_.first();
        const QPointF valuePoint = chart()->mapToPosition(QPointF(cursorX, value), seriesRef);
        const QPointF labelPos = valuePoint + QPointF(6, -12 - (12 * i));
        label->setPos(labelPos);
        label->setVisible(true);
    }
}
