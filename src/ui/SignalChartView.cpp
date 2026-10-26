#include "ui/SignalChartView.h"

#include "ui/SignalTreeWidget.h"
#include "ui/CompactValueAxis.h"

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
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QFont>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
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
#include <QScreen>
#include <QStringList>

#include <QtCharts/QChart>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <QSignalBlocker>
#include <QGraphicsLayout>
#include <QTimer>
#include <cmath>
#include <limits>

namespace {

// Paint a backing plate, not a glyph outline: outlines obscure small text.
class CursorValueLabel final : public QGraphicsSimpleTextItem {
public:
    CursorValueLabel(const QColor& signalColor, QGraphicsItem* parent)
        : QGraphicsSimpleTextItem(parent), signalColor_(signalColor) {
        setBrush(QColor("#ffff00"));
        setPen(QPen(Qt::NoPen));
    }

    QRectF boundingRect() const override {
        return QGraphicsSimpleTextItem::boundingRect().adjusted(-4, -2, 4, 2);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override {
        painter->save();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#171c22"));
        painter->drawRoundedRect(boundingRect(), 3, 3);
        // Keep the association with the curve without coloring the numbers.
        const QRectF rect = boundingRect();
        painter->fillRect(QRectF(rect.left(), rect.top() + 2, 2, rect.height() - 4),
                          signalColor_);
        painter->restore();
        QGraphicsSimpleTextItem::paint(painter, option, widget);
    }

private:
    QColor signalColor_;
};

}  // namespace

SignalChartView::SignalChartView(QWidget* parent) : QChartView(parent) {
    mousePositionLabel_ = new QLabel(viewport());
    mousePositionLabel_->setObjectName("chartMousePosition");
    mousePositionLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
    mousePositionLabel_->setStyleSheet("color: #ffff00; background: #171c22; padding: 2px; font-size: 10px;");
    mousePositionLabel_->hide();
    // A plain tooltip window has no QToolTip timeout: it lives until hover ends.
    guidePopup_ = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint
                                  | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    guidePopup_->setObjectName(QStringLiteral("guideHoverPopup"));
    guidePopup_->setTextFormat(Qt::PlainText);
    guidePopup_->setAttribute(Qt::WA_TransparentForMouseEvents);
    guidePopup_->setAttribute(Qt::WA_ShowWithoutActivating);
    guidePopup_->setStyleSheet(QStringLiteral(
        "QLabel { color: #ffff00; background: #171c22; border: 1px solid #85909c;"
        " border-radius: 4px; padding: 6px; }"));
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) HideGuideTooltip();
    });
    setRenderHint(QPainter::Antialiasing);
    // Moving dashed overlays and child readouts must repaint the old pixels too.
    // Coalesce repaint requests at the viewport level rather than relying on
    // narrow scene dirty rectangles (which can leave trails on some backends).
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setAcceptDrops(true);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setMouseTracking(true);
    if (viewport()) {
        viewport()->setAcceptDrops(true);
        viewport()->setMouseTracking(true);
    }
}

void SignalChartView::SetTheme(const AppTheme& theme) {
    theme_ = theme;
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
    HideGuideTooltip();
    seriesIndices_ = seriesIndices;
    infoPositions_.clear();
    draggingInfo_ = nullptr;
    cursorBlocksDetached_ = false;
    extremaLabel_ = nullptr;
    extremaLines_.clear();
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
    autoMinY_ = minY; autoMaxY_ = maxY; yZoom_ = 1.0;

    auto* chart = new QChart();
    chart->setCacheMode(QGraphicsItem::NoCache);
    chart->setTitle(QString());
    chart->setBackgroundBrush(viewIndex % 2 == 0 ? theme_.chartBackgroundEven
                                                  : theme_.chartBackgroundOdd);
    chart->setBackgroundPen(QPen(viewIndex % 2 == 0 ? theme_.chartBorderEven
                                                     : theme_.chartBorderOdd, 1.5));
    chart->setPlotAreaBackgroundVisible(true);
    chart->setPlotAreaBackgroundBrush(viewIndex % 2 == 0 ? theme_.plotBackgroundEven
                                                          : theme_.plotBackgroundOdd);
    chart->setTitleBrush(QBrush(theme_.axisText));
    auto* legend = chart->legend();
    legend->hide();
    QFont titleFont = chart->titleFont();
    if (titleFont.pointSize() > 0) {
        titleFont.setPointSize(std::max(9, titleFont.pointSize() - 1));
        chart->setTitleFont(titleFont);
    }
    chart->setMargins(QMargins(4, 0, 4, 2));

    series_.clear();
    segmentSeries_.clear();
    for (int i = 0; i < seriesIndices.size(); ++i) {
        auto* line = new QLineSeries(chart);
        const int colorIndex = palette.isEmpty() ? 0 : (i % palette.size());
        QPen pen(palette.isEmpty() ? Qt::white : palette[colorIndex]);
        pen.setWidthF(theme_.seriesLineWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        line->setPen(pen);
        const int dataIndex = seriesIndices[i];
        if (sourceSeries_ && dataIndex >= 0 && dataIndex < sourceSeries_->size()) {
            const auto& data = sourceSeries_->at(dataIndex);
            line->setName(data.unit.isEmpty() ? data.name : QStringLiteral("%1 (%2)").arg(data.name, data.unit));
        } else {
            line->setName(title);
        }
        chart->addSeries(line);
        series_.append(line);
        segmentSeries_.append(QVector<QLineSeries*>{line});
    }

    auto* axisX = new QValueAxis(chart);
    axisX->setTitleText(timeUnit);
    axisX->setRange(minX, maxX);
    axisX->setTitleBrush(QBrush(theme_.axisText));
    axisX->setLabelsColor(theme_.axisText);
    axisX->setLinePenColor(theme_.axisLine);
    axisX->setGridLineColor(theme_.gridLine);
    chart->addAxis(axisX, Qt::AlignBottom);
    for (auto* line : series_) {
        line->attachAxis(axisX);
    }

    auto* axisY = new CompactValueAxis(chart);
    axisY->setRange(minY, maxY);
    axisY->setTickCount(3);
    axisY->setTitleBrush(QBrush(theme_.axisText));
    axisY->setLabelsColor(theme_.axisText);
    axisY->setLinePenColor(theme_.axisLine);
    axisY->setGridLineColor(theme_.gridLine);
    chart->addAxis(axisY, Qt::AlignLeft);
    for (auto* line : series_) {
        line->attachAxis(axisY);
    }

    setChart(chart);
    connect(chart, &QGraphicsWidget::geometryChanged, this, &SignalChartView::SchedulePlotLayout);
    connect(chart, &QChart::plotAreaChanged, this, [this](const QRectF&) {
        UpdateHeaderLayout();
        UpdateZeroLine();
        UpdateGuides();
    });

    headerTitleText_ = title.trimmed();
    headerDescText_ = unit.trimmed();
    headerTitle_ = new QGraphicsSimpleTextItem(chart);
    headerDesc_ = new QGraphicsSimpleTextItem(chart);
    emptyMessage_ = new QGraphicsSimpleTextItem(
        tr("当前区间无有效差值/和值\n请检查时间重叠，或使用线性插值模式"), chart);
    emptyMessage_->setBrush(QBrush(theme_.axisText));
    emptyMessage_->setZValue(900);
    emptyMessage_->hide();
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
    headerTitle_->setBrush(QBrush(theme_.axisText));
    headerDesc_->setBrush(QBrush(theme_.axisText));
    headerTitle_->setZValue(900);
    headerDesc_->setZValue(900);
    legendPanel_ = nullptr;
    legendLabels_.clear();
    legendSwatches_.clear();
    if (showLegend) {
        legendPanel_ = new QGraphicsRectItem(chart);
        legendPanel_->setData(0, QStringLiteral("signalLegend"));
        legendPanel_->setBrush(theme_.legendBackground);
        legendPanel_->setPen(QPen(theme_.legendBorder));
        legendPanel_->setZValue(999);
        // Include one extra row for an overflow summary on short canvases.
        for (int i = 0; i <= series_.size(); ++i) {
            auto* label = new QGraphicsSimpleTextItem(legendPanel_);
            label->setData(0, QStringLiteral("legendLabel"));
            label->setBrush(theme_.axisText);
            legendLabels_.append(label);
            legendSwatches_.append(new QGraphicsLineItem(legendPanel_));
        }
    }
    UpdateHeaderLayout();

    if (!crosshair_) {
        crosshair_ = new QGraphicsLineItem();
        crosshair_->setData(0, QStringLiteral("cursorCrosshair"));
        QPen cursorPen(theme_.cursorLine);
        cursorPen.setStyle(Qt::DashLine);
        crosshair_->setPen(cursorPen);
        crosshair_->setZValue(990);
        crosshair_->setVisible(false);
        chart->scene()->addItem(crosshair_);
    }
    crosshair_->setParentItem(chart);

    if (!zeroLine_) {
        zeroLine_ = new QGraphicsLineItem();
        QPen assistPen(theme_.zeroLine);
        assistPen.setStyle(Qt::DashLine);
        zeroLine_->setPen(assistPen);
        zeroLine_->setZValue(500);
        zeroLine_->setVisible(true);
        chart->scene()->addItem(zeroLine_);
    }
    zeroLine_->setParentItem(chart);

    valueLabels_.clear();
    for (int i = 0; i < series_.size(); ++i) {
        const QColor labelColor = series_.value(i)
            ? series_[i]->pen().color() : theme_.cursorValue;
        auto* label = new CursorValueLabel(labelColor, chart);
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
    SetSeriesSamples(seriesSamples);
}

void SignalChartView::SetSeriesSamples(const QVector<QVector<QPointF>>& seriesSamples) {
    bool hasFinite = false;
    for (int i = 0; i < series_.size() && i < seriesSamples.size(); ++i) {
        if (series_[i]) {
            // Qt Charts cannot reliably render NaN vertices. Preserve gaps using
            // separate finite paths instead of submitting invalid coordinates.
            QVector<QVector<QPointF>> runs;
            QVector<QPointF> run;
            for (const auto& point : seriesSamples[i]) {
                if (std::isfinite(point.x()) && std::isfinite(point.y())) run.append(point);
                else if (!run.isEmpty()) { runs.append(run); run.clear(); }
            }
            if (!run.isEmpty()) runs.append(run);
            auto& paths = segmentSeries_[i];
            while (paths.size() < runs.size()) {
                auto* path = new QLineSeries(chart());
                path->setPen(series_[i]->pen());
                chart()->addSeries(path);
                for (auto* axis : series_[i]->attachedAxes()) path->attachAxis(axis);
                paths.append(path);
            }
            for (int j = 0; j < paths.size(); ++j) {
                paths[j]->replace(j < runs.size() ? runs[j] : QVector<QPointF>{});
                paths[j]->setVisible(j == 0 || j < runs.size());
                paths[j]->setPointsVisible(j < runs.size() && runs[j].size() == 1);
            }
            const auto finiteCount = std::count_if(seriesSamples[i].begin(), seriesSamples[i].end(),
                [](const QPointF& point) { return std::isfinite(point.x()) && std::isfinite(point.y()); });
            hasFinite = hasFinite || finiteCount > 0;
        }
    }
    if (emptyMessage_) {
        emptyMessage_->setVisible(!hasFinite && std::any_of(seriesIndices_.begin(), seriesIndices_.end(),
            [](int index) { return index < 0; }));
        UpdateHeaderLayout();
    }
}

void SignalChartView::SetDisplaySources(const QVector<const pat::Series*>& sources,
                                        const QVector<pat::DisplayTransform>& transforms) {
    displaySources_ = sources;
    displayTransforms_ = transforms;
    while (displayTransforms_.size() < displaySources_.size()) {
        displayTransforms_.append(pat::DisplayTransform{});
    }
    QStringList compactNames;
    compactNames.reserve(displaySources_.size());
    const bool compactLegendLabels = displaySources_.size() > 1;
    QString commonUnit;
    bool unitInitialized = false;
    bool sameUnit = true;
    for (const auto* source : displaySources_) {
        if (!source) {
            compactNames.append(QString());
            continue;
        }
        const int separator = compactLegendLabels
            ? source->name.lastIndexOf(QStringLiteral(" - ")) : -1;
        compactNames.append(separator >= 0 ? source->name.mid(separator + 3) : source->name);
        if (!unitInitialized) {
            commonUnit = source->unit;
            unitInitialized = true;
        } else if (source->unit != commonUnit) {
            sameUnit = false;
        }
    }

    for (int i = 0; i < series_.size() && i < displaySources_.size(); ++i) {
        const auto* source = displaySources_[i];
        if (!series_[i] || !source) continue;
        QString displayName = compactNames.value(i);
        if (compactNames.count(displayName) > 1) displayName = source->name;
        QString label = (compactLegendLabels && sameUnit) || source->unit.isEmpty()
            ? displayName : tr("%1 (%2)").arg(displayName, source->unit);
        const auto transform = displayTransforms_.value(i);
        if (!transform.IsIdentity()) {
            label += tr(" [×%1, %2%3]")
                         .arg(transform.scale, 0, 'g', 8)
                         .arg(transform.offset >= 0.0 ? QStringLiteral("+") : QString())
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
    }
    UpdateHeaderLayout();
}

QVector<GuideLine> SignalChartView::GuideLines() const {
    QVector<GuideLine> result;
    result.reserve(guides_.size());
    for (const auto& visual : guides_) result.append(visual.guide);
    return result;
}

void SignalChartView::SetGuideLines(const QVector<GuideLine>& guides) {
    const QSignalBlocker blocker(this);
    HideGuideTooltip();
    draggingGuide_ = -1;
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
    HideGuideTooltip();
    if (!chart() || series_.isEmpty()) return;
    auto* axisX = qobject_cast<QValueAxis*>(chart()->axes(Qt::Horizontal, series_.first()).value(0));
    if (!axisX) return;
    axisX->setRange(minX, maxX);
    UpdateZeroLine();
    UpdateHeaderLayout();
    UpdateGuides();
}

void SignalChartView::SetYAxisRange(double minY, double maxY) {
    HideGuideTooltip();
    if (!chart() || series_.isEmpty()) return;
    auto* axisY = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    if (!axisY) return;
    autoMinY_ = minY; autoMaxY_ = maxY;
    const double center = minY * .5 + maxY * .5;
    const double half = (maxY * .5 - minY * .5) * yZoom_;
    if (std::isfinite(center - half) && std::isfinite(center + half) && half > 0)
        axisY->setRange(center - half, center + half);
    UpdateAlignedPlotArea();
    UpdateZeroLine();
    UpdateHeaderLayout();
    UpdateGuides();
}

void SignalChartView::ZoomY(double factor) {
    yZoom_ = std::clamp(yZoom_ * factor, 0.001, 1000.0);
    SetYAxisRange(autoMinY_, autoMaxY_);
    emit AxisLayoutChanged();
}

void SignalChartView::ResetYZoom() {
    yZoom_ = 1.0;
    SetYAxisRange(autoMinY_, autoMaxY_);
    emit AxisLayoutChanged();
}

void SignalChartView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (InfoBlockAt(event->position().toPoint())) { event->accept(); return; }
    if (event->button() == Qt::LeftButton) {
        rubberActive_ = false; reorderArmed_ = false;
        if (rubberBand_) rubberBand_->hide();
        emit FocusRequested(); event->accept();
    }
    else QChartView::mouseDoubleClickEvent(event);
}

void SignalChartView::SetCursorX(double cursorX) {
    cursorActive_ = true;
    UpdateCursor(cursorX);
    viewport()->update();
}

void SignalChartView::HideCursor() {
    if (crosshair_) crosshair_->setVisible(false);
    for (auto* label : valueLabels_) {
        if (label) label->setVisible(false);
    }
    cursorActive_ = false;
    mousePositionLabel_->hide();
    viewport()->update();
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
    if (auto* compact = dynamic_cast<CompactValueAxis*>(axis)) return compact->LabelWidth();
    const QFontMetricsF metrics(axis->labelsFont());
    const QByteArray labelFormat = axis->labelFormat().toLatin1();
    return std::max(metrics.horizontalAdvance(QString::asprintf(labelFormat.constData(), axis->min())),
                    metrics.horizontalAdvance(QString::asprintf(labelFormat.constData(), axis->max())));
}

void SignalChartView::mouseMoveEvent(QMouseEvent* event) {
    if (chart() && !series_.isEmpty()) {
        const QPointF point = chart()->mapFromScene(mapToScene(event->position().toPoint()));
        const bool inside = chart()->plotArea().contains(point);
        mousePositionLabel_->setVisible(inside);
        if (inside) {
            const QPointF value = MapToValue(event->position().toPoint());
            mousePositionLabel_->setText(tr("游标位置 X: %1  Y: %2").arg(QString::number(value.x(), 'g', 10), QString::number(value.y(), 'g', 10)));
            mousePositionLabel_->adjustSize();
            const QPoint anchor = mapFromScene(chart()->mapToScene(chart()->plotArea().bottomRight()));
            mousePositionLabel_->move(std::max(0, anchor.x() - mousePositionLabel_->width() - 4), std::max(0, anchor.y() - mousePositionLabel_->height() - 4));
            mousePositionLabel_->raise();
        }
    }
    if (draggingInfo_ && (event->buttons() & Qt::LeftButton)) {
        const QRectF area = chart()->plotArea().adjusted(4, 4, -4, -4);
        const QPointF target = chart()->mapFromScene(mapToScene(event->position().toPoint())) - infoDragOffset_;
        const double width = std::max(1.0, area.width());
        const double height = std::max(1.0, area.height());
        infoPositions_[draggingInfo_] = QPointF(std::clamp((target.x() - area.left()) / width, 0.0, 1.0),
                                               std::clamp((target.y() - area.top()) / height, 0.0, 1.0));
        PlaceInfoBlocks();
        event->accept();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton) && InfoBlockAt(event->position().toPoint())) {
        setCursor(Qt::OpenHandCursor);
        HideGuideTooltip();
        return; // Freeze the hovered reading so it remains easy to grab.
    }
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
        emit GuidesEdited();
        emit CursorMoved(MapToValue(event->position().toPoint()).x());
        HideGuideTooltip();
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
        if (!chart()->plotArea().contains(chart()->mapFromScene(mapToScene(viewPos)))) {
            HideCursor();
            HideGuideTooltip();
            unsetCursor();
            emit CursorLeft();
            return;
        }
        const QPointF plotPoint = MapToValue(viewPos);
        const double minX = rangeContext_.hasCurrentRange ? rangeContext_.currentMinX : rangeContext_.globalMinX;
        const double maxX = rangeContext_.hasCurrentRange ? rangeContext_.currentMaxX : rangeContext_.globalMaxX;
        const double cursorX = std::clamp(plotPoint.x(), minX, maxX);
        emit CursorMoved(cursorX);
        const int guideIndex = GuideAt(viewPos);
        if (guideIndex >= 0) {
            setCursor(Qt::SizeAllCursor);
            ShowGuideTooltip(guideIndex, event->globalPosition().toPoint());
            return;
        }
        unsetCursor();
        HideGuideTooltip();
    }
}

void SignalChartView::mousePressEvent(QMouseEvent* event) {
    HideGuideTooltip();
    if (event->button() != Qt::LeftButton || !chart()) return;

    const QPoint viewPos = event->position().toPoint();
    if (auto* block = InfoBlockAt(viewPos)) {
        draggingInfo_ = block;
        infoDragOffset_ = chart()->mapFromScene(mapToScene(viewPos)) - block->mapRectToParent(block->boundingRect()).topLeft();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
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
        if (chart() && !chart()->plotArea().contains(chart()->mapFromScene(mapToScene(event->position().toPoint())))) {
            HideCursor();
            emit CursorLeft();
        }
        if (draggingInfo_) {
            draggingInfo_ = nullptr;
            setCursor(Qt::OpenHandCursor);
            event->accept();
            return;
        }
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
    if (event->modifiers() & Qt::ControlModifier) {
        ZoomY(zoomFactor);
        event->accept();
        return;
    }

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
    mousePositionLabel_->hide();
    if (draggingInfo_) { event->accept(); return; }
    Q_UNUSED(event)
    HideGuideTooltip();
    HideCursor();
    unsetCursor();
    emit CursorLeft();
}

void SignalChartView::hideEvent(QHideEvent* event) {
    HideGuideTooltip();
    HideCursor();
    QChartView::hideEvent(event);
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
    SchedulePlotLayout();
}

void SignalChartView::SchedulePlotLayout() {
    if (plotLayoutPending_ || updatingPlotLayout_) return;
    plotLayoutPending_ = true;
    QTimer::singleShot(0, this, [this] {
        plotLayoutPending_ = false;
        UpdateAlignedPlotArea();
        viewport()->update();
    });
}

void SignalChartView::UpdateAlignedPlotArea() {
    if (!chart() || series_.isEmpty() || updatingPlotLayout_) return;
    updatingPlotLayout_ = true;
    // Explicit common plot coordinates avoid Qt's per-chart label/endpoint layout drift.
    const qreal left = std::min(plotLeftMargin_, chart()->size().width() * .45);
    auto* axis = chart()->axes(Qt::Horizontal).value(0);
    const qreal bottom = axis && axis->isVisible() ? 34.0 : 12.0;
    const QRectF target(left, 28.0, std::max(1.0, chart()->size().width() - left - 28.0),
                        std::max(1.0, chart()->size().height() - 28.0 - bottom));
    // A fixed plot rectangle can leave Qt's background and axis layout cached at
    // the old size even while the series domain has already changed. Reset the
    // layout first, then commit the shared rectangle for every chart component.
    chart()->setPlotArea(QRectF());
    chart()->layout()->invalidate();
    chart()->layout()->activate();
    chart()->setPlotArea(target);
    chart()->layout()->invalidate();
    chart()->layout()->activate();
    UpdateGuides();
    UpdateHeaderLayout();
    UpdateZeroLine();
    updatingPlotLayout_ = false;
}

void SignalChartView::InputGuidePosition(GuideOrientation orientation, double initial, int index) {
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("设置辅助线位置"),
        orientation == GuideOrientation::Horizontal
            ? tr("Y 坐标（当前纵轴单位，支持科学计数法；超出视野仍保留）：")
            : tr("X 坐标（当前横轴单位，支持科学计数法；垂线同步所有子图）："),
        QLineEdit::Normal, QString::number(initial, 'g', 17), &ok);
    if (!ok) return;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        QMessageBox::warning(this, tr("无效位置"), tr("请输入有限数值，例如 12.5 或 1.25e3。"));
        return;
    }
    if (index < 0) AddGuide(orientation, value);
    else if (index < guides_.size()) {
        guides_[index].guide.value = value;
        UpdateGuides(); emit GuidesEdited();
    }
}

void SignalChartView::AddGuide(GuideOrientation orientation, double value) {
    if (!chart() || series_.isEmpty()) return;
    auto* outline = new QGraphicsLineItem(chart());
    QPen outlinePen(theme_.guideOutline);
    outlinePen.setWidthF(theme_.guideLineWidth + 1.0);
    outlinePen.setCosmetic(true);
    outline->setPen(outlinePen);
    outline->setZValue(959);
    auto* item = new QGraphicsLineItem(chart());
    QPen pen(theme_.guideLine);
    pen.setWidthF(theme_.guideLineWidth);
    pen.setCosmetic(true);
    item->setPen(pen);
    item->setZValue(960);
    guides_.append({{orientation, value}, item, outline});
    UpdateGuides();
    emit GuidesEdited();
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

void SignalChartView::ShowGuideTooltip(int index, const QPoint& globalPos) {
    const auto* screen = QGuiApplication::screenAt(globalPos);
    if (!screen) screen = this->screen();
    const QRect bounds = screen->availableGeometry().adjusted(4, 4, -4, -4);
    const int maxRows = std::max(1, bounds.height() / guidePopup_->fontMetrics().lineSpacing() - 4);
    guidePopup_->setText(GuideTooltip(index, std::min(40, maxRows)));
    guidePopup_->adjustSize();
    QPoint position = globalPos + QPoint(16, 20);
    if (position.y() + guidePopup_->height() > bounds.bottom())
        position.setY(globalPos.y() - guidePopup_->height() - 12);
    position.setX(std::clamp(position.x(), bounds.left(),
                           std::max(bounds.left(), bounds.right() - guidePopup_->width())));
    position.setY(std::clamp(position.y(), bounds.top(),
                           std::max(bounds.top(), bounds.bottom() - guidePopup_->height())));
    guidePopup_->move(position);
    guidePopup_->show();
}

void SignalChartView::HideGuideTooltip() {
    if (guidePopup_) guidePopup_->hide();
}

void SignalChartView::contextMenuEvent(QContextMenuEvent* event) {
    HideGuideTooltip();
    QMenu menu(this);
    const int guideIndex = GuideAt(mapFromGlobal(event->globalPos()));
    if (guideIndex >= 0) {
        menu.addAction(tr("设置辅助线位置…"), this, [this, guideIndex] {
            const auto guide = guides_[guideIndex].guide;
            InputGuidePosition(guide.orientation, guide.value, guideIndex);
        });
        menu.addAction(tr("删除辅助线"), this, [this, guideIndex]() {
            delete guides_[guideIndex].item;
            delete guides_[guideIndex].outline;
            guides_.removeAt(guideIndex);
            HideGuideTooltip();
            emit GuidesEdited();
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
    menu.addAction(tr("输入横向辅助线位置…"), this, [this, value] { InputGuidePosition(GuideOrientation::Horizontal, value.y()); });
    menu.addAction(tr("输入垂向辅助线位置…"), this, [this, value] { InputGuidePosition(GuideOrientation::Vertical, value.x()); });
    if (!guides_.isEmpty()) {
        menu.addAction(tr("清除辅助线（垂线同步所有图）"), this, [this]() { SetGuideLines({}); emit GuidesEdited(); });
    }
    menu.addSeparator();
    auto* resetAction = menu.addAction(tr("还原横轴"));
    menu.addAction(tr("放大本图 / 返回多图（双击）"), this, [this] { emit FocusRequested(); });
    menu.addAction(tr("纵向放大（Ctrl + 滚轮上）"), this, [this] { ZoomY(.8); });
    menu.addAction(tr("纵向缩小（Ctrl + 滚轮下）"), this, [this] { ZoomY(1.25); });
    menu.addAction(tr("还原纵轴自动范围"), this, &SignalChartView::ResetYZoom);
    auto* detach = menu.addAction(tr("打散数据块（游标数据自由拖动）"));
    detach->setObjectName("detachCursorBlocks");
    detach->setCheckable(true);
    detach->setChecked(cursorBlocksDetached_);
    connect(detach, &QAction::toggled, this, [this](bool detached) {
        cursorBlocksDetached_ = detached;
        draggingInfo_ = nullptr;
        for (auto* label : valueLabels_) {
            if (detached && label->isVisible()) RememberInfoBlock(label);
            else infoPositions_.remove(label);
        }
        if (cursorActive_) UpdateCursor(lastCursorX_);
    });
    menu.addAction(tr("还原信息块位置"), this, [this] {
        infoPositions_.clear();
        draggingInfo_ = nullptr;
        cursorBlocksDetached_ = false;
        UpdateHeaderLayout();
        if (cursorActive_) UpdateCursor(lastCursorX_);
    });
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
        auto* transformMenu = menu.addMenu(tr("信号属性（比例/整体平移）"));
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
        if (event->modifiers() & Qt::ShiftModifier) {
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
    zeroLine_->setLine(QLineF(left, right));
    zeroLine_->setVisible(true);
}

QGraphicsItem* SignalChartView::InfoBlockAt(const QPoint& viewPos) const {
    const QPointF scenePos = mapToScene(viewPos);
    for (auto it = valueLabels_.crbegin(); it != valueLabels_.crend(); ++it)
        if ((*it)->isVisible() && (*it)->sceneBoundingRect().contains(scenePos))
            return cursorBlocksDetached_ ? *it : nullptr;
    if (extremaLabel_ && extremaLabel_->isVisible() && extremaLabel_->sceneBoundingRect().contains(scenePos))
        return extremaLabel_;
    if (legendPanel_ && legendPanel_->isVisible() && legendPanel_->sceneBoundingRect().contains(scenePos))
        return legendPanel_;
    return nullptr;
}

void SignalChartView::RememberInfoBlock(QGraphicsItem* block) {
    const QRectF area = chart()->plotArea().adjusted(4, 4, -4, -4);
    const QRectF bounds = block->mapRectToParent(block->boundingRect());
    infoPositions_[block] = QPointF(
        std::clamp((bounds.left() - area.left()) / std::max(1.0, area.width()), 0.0, 1.0),
        std::clamp((bounds.top() - area.top()) / std::max(1.0, area.height()), 0.0, 1.0));
}

void SignalChartView::PlaceInfoBlocks() {
    if (!chart()) return;
    const QRectF area = chart()->plotArea().adjusted(4, 4, -4, -4);
    for (auto it = infoPositions_.cbegin(); it != infoPositions_.cend(); ++it) {
        auto* block = it.key();
        const QRectF bounds = block->boundingRect();
        block->setPos(area.topLeft() + QPointF(std::clamp(it.value().x() * area.width(), 0.0, std::max(0.0, area.width() - bounds.width())),
                                               std::clamp(it.value().y() * area.height(), 0.0, std::max(0.0, area.height() - bounds.height())))
                      - bounds.topLeft());
    }
}

void SignalChartView::SetExtremaText(const QStringList& lines) {
    extremaLines_ = lines;
    if (!extremaLabel_) {
        extremaLabel_ = new CursorValueLabel(QColor(Qt::yellow), chart());
        QFont font = extremaLabel_->font();
        font.setPointSizeF(std::max(8.0, font.pointSizeF() - 2.0));
        extremaLabel_->setFont(font);
        extremaLabel_->setZValue(1000);
    }
    UpdateHeaderLayout();
}

void SignalChartView::UpdateHeaderLayout() {
    if (!chart() || !headerTitle_ || !headerDesc_) return;
    if (emptyMessage_) {
        const QRectF area = chart()->plotArea();
        const QRectF bounds = emptyMessage_->boundingRect();
        emptyMessage_->setPos(area.center().x() - bounds.width() / 2,
                             area.center().y() - bounds.height() / 2);
    }
    const QRectF plotArea = chart()->plotArea();
    if (plotArea.isEmpty()) return;
    if (extremaLabel_) {
        QStringList lines;
        for (int i = 0; i < extremaLines_.size(); ++i) {
            if (i >= series_.size() || !series_[i]->isVisible()) continue;
            for (const auto& line : extremaLines_[i].split('\n'))
                lines.append((series_.size() > 1 ? QStringLiteral("#%1 ").arg(i + 1) : QString{}) + line);
        }
        const QString full = lines.join('\n');
        const QFontMetricsF metrics(extremaLabel_->font());
        const int limit = std::max(3, int(plotArea.height() * 0.3 / metrics.height()));
        if (lines.size() > limit) {
            lines = lines.mid(0, ((limit - 1) / 2) * 2);
            lines.append(tr("更多极值…（悬停查看全部）"));
        }
        for (auto& line : lines)
            line = metrics.elidedText(line, Qt::ElideRight, std::max(0, int(plotArea.width() - 16)));
        extremaLabel_->setText(lines.join('\n'));
        extremaLabel_->setToolTip(full);
        extremaLabel_->setVisible(!lines.isEmpty());
        extremaLabel_->setPos(plotArea.left() + 6,
                             plotArea.top() + QFontMetricsF(headerTitle_->font()).height() + 8);
    }

    const qreal padding = 4.0;
    const qreal gap = 10.0;
    const qreal y = plotArea.top() + 2.0;

    const QFontMetricsF titleMetrics(headerTitle_->font());
    const QFontMetricsF descMetrics(headerDesc_->font());

    QString titleText = headerTitleText_;
    QString descText = headerDescText_;
    if (auto* axis = dynamic_cast<CompactValueAxis*>(chart()->axes(Qt::Vertical).value(0))) {
        if (!axis->Annotation().isEmpty()) descText += QStringLiteral("  ") + axis->Annotation();
    }
    headerDesc_->setToolTip(descText);
    headerTitle_->setToolTip(tr("拖动到另一图合并全部信号；按住 Shift 拖动排序\n%1").arg(titleText));
    const qreal contentLeft = plotArea.left() + padding;
    const qreal contentRight = plotArea.right() - padding;
    qreal titleRight = contentRight;

    if (descText.isEmpty()) {
        headerDesc_->setVisible(false);
    } else {
        const qreal descMaxWidth = std::max(0.0, plotArea.width() * 0.5);
        const QString elidedDesc = descMetrics.elidedText(
            descText, Qt::ElideLeft, static_cast<int>(descMaxWidth));
        const qreal descWidth = descMetrics.horizontalAdvance(elidedDesc);
        const qreal descX = std::max(contentLeft, contentRight - descWidth);
        headerDesc_->setText(elidedDesc);
        headerDesc_->setPos(descX, y);
        headerDesc_->setVisible(true);
        titleRight = std::max(contentLeft, descX - gap);
    }

    const qreal titleWidth = std::max(0.0, titleRight - contentLeft);
    const QString elidedTitle = titleMetrics.elidedText(
        titleText, Qt::ElideRight, static_cast<int>(titleWidth));
    headerTitle_->setText(elidedTitle);
    headerTitle_->setPos(contentLeft, y);

    if (legendPanel_) {
        QFont legendFont = headerTitle_->font();
        if (legendFont.pointSizeF() > 0.0) {
            legendFont.setPointSizeF(std::max(8.0, legendFont.pointSizeF() - 2.0));
        } else if (legendFont.pixelSize() > 0) {
            legendFont.setPixelSize(std::max(10, legendFont.pixelSize() - 3));
        }
        const QFontMetricsF legendMetrics(legendFont);
        const qreal rowHeight = legendMetrics.height();
        constexpr qreal inset = 6.0;
        constexpr qreal rowGap = 3.0;
        constexpr qreal textLeft = 22.0;
        qreal maxLabelWidth = 0.0;
        QStringList fullNames;
        for (int i = 0; i < series_.size(); ++i) {
            maxLabelWidth = std::max(maxLabelWidth,
                                    legendMetrics.horizontalAdvance(series_[i]->name()));
            const auto* source = displaySources_.value(i);
            fullNames.append(source ? source->name : series_[i]->name());
        }
        const qreal headerHeight = std::max(titleMetrics.height(), descMetrics.height());
        const qreal legendY = plotArea.top() + headerHeight + 6.0;
        const qreal maxHeight = plotArea.bottom() - padding - legendY;
        const int capacity = std::max(0, static_cast<int>(
            (maxHeight - 2 * inset + rowGap) / (rowHeight + rowGap)));
        const int rows = std::min(capacity, static_cast<int>(series_.size()));
        const bool overflow = rows < series_.size();
        const int signalRows = overflow ? std::max(0, rows - 1) : rows;
        const QString overflowText = tr("另 %1 个信号（悬停查看）")
                                         .arg(series_.size() - signalRows);
        if (overflow) {
            maxLabelWidth = std::max(maxLabelWidth, legendMetrics.horizontalAdvance(overflowText));
        }
        const qreal maxWidth = std::max(0.0, std::min<qreal>(240.0, plotArea.width() * 0.3));
        const qreal legendWidth = std::min(maxWidth, std::ceil(maxLabelWidth) + textLeft + inset);
        const qreal legendHeight = 2 * inset + rows * rowHeight + std::max(0, rows - 1) * rowGap;
        legendPanel_->setVisible(rows > 0 && legendWidth > textLeft + inset);
        legendPanel_->setRect(0, 0, legendWidth, legendHeight);
        legendPanel_->setPos(plotArea.right() - padding - legendWidth, legendY);
        legendPanel_->setToolTip(fullNames.join(QLatin1Char('\n')));
        for (int i = 0; i < legendLabels_.size(); ++i) {
            auto* label = legendLabels_[i];
            auto* swatch = legendSwatches_[i];
            label->setVisible(i < rows);
            swatch->setVisible(i < signalRows);
            if (i >= rows) continue;
            label->setFont(legendFont);
            const QString name = i < signalRows ? series_[i]->name() : overflowText;
            label->setText(legendMetrics.elidedText(name, Qt::ElideRight,
                std::max(0, static_cast<int>(legendWidth - textLeft - inset))));
            label->setPos(textLeft, inset + i * (rowHeight + rowGap));
            if (i < signalRows) {
                const qreal yCenter = label->pos().y() + rowHeight * 0.5;
                swatch->setPen(QPen(series_[i]->pen().color(), 2));
                swatch->setLine(inset, yCenter, textLeft - 5, yCenter);
            }
        }
    }
    if (extremaLabel_ && extremaLabel_->isVisible() && !infoPositions_.contains(extremaLabel_) && legendPanel_ && legendPanel_->isVisible() && !infoPositions_.contains(legendPanel_)) {
        const QFontMetricsF metrics(extremaLabel_->font());
        QStringList lines = extremaLabel_->text().split('\n');
        const int width = std::max(0, int(legendPanel_->pos().x() - extremaLabel_->pos().x() - 10));
        for (auto& line : lines) line = metrics.elidedText(line, Qt::ElideRight, width);
        extremaLabel_->setText(lines.join('\n'));
    }
    PlaceInfoBlocks();
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
    lastCursorX_ = cursorX;
    if (!chart() || series_.isEmpty()) return;

    auto* axisY = qobject_cast<QValueAxis*>(chart()->axes(Qt::Vertical, series_.first()).value(0));
    if (!axisY) return;

    const double yMin = axisY->min();
    const double yMax = axisY->max();
    const QPointF top = chart()->mapToPosition(QPointF(cursorX, yMax), series_.first());
    const QPointF bottom = chart()->mapToPosition(QPointF(cursorX, yMin), series_.first());
    if (crosshair_) {
        crosshair_->setLine(QLineF(top, bottom));
        crosshair_->setVisible(true);
    }

    for (auto* label : valueLabels_) {
        if (label) label->setVisible(false);
    }

    struct CursorLabelPlacement {
        QGraphicsSimpleTextItem* label = nullptr;
        qreal desiredTop = 0.0;
        qreal top = 0.0;
        qreal width = 0.0;
        qreal height = 0.0;
    };
    QVector<CursorLabelPlacement> placements;
    placements.reserve(valueLabels_.size());

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
        if (it != end && it->x() == clampedX) {
            // A valid point immediately after a gap must not interpolate with NaN.
            // At a step, prefer the value AFTER the update at that timestamp.
            while (it + 1 != end && (it + 1)->x() == clampedX) ++it;
            value = it->y();
        } else if (it == begin) {
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
        if (!std::isfinite(value)) continue;
        auto* label = valueLabels_[i];
        if (!label) continue;
        const QString keyText = QString::number(cursorX, 'f', 2);
        const QString valueText = QString::number(value, 'f', 4);
        label->setText(series_.size() > 1
                           ? QStringLiteral("k:%1  v:%2").arg(keyText, valueText)
                           : QStringLiteral("k:%1\nv:%2").arg(keyText, valueText));
        auto* seriesRef = series_.value(i);
        if (!seriesRef) seriesRef = series_.first();
        const QPointF valuePoint = chart()->mapToPosition(QPointF(cursorX, value), seriesRef);
        const QRectF bounds = label->boundingRect();
        Q_UNUSED(valuePoint)
        placements.append({label, chart()->plotArea().top() + 22.0, 0.0,
                           bounds.width(), bounds.height()});
    }

    if (placements.isEmpty()) return;
    std::stable_sort(placements.begin(), placements.end(),
                     [](const auto& left, const auto& right) {
        return left.desiredTop < right.desiredTop;
    });

    QRectF labelArea = chart()->plotArea().adjusted(2.0, 2.0, -2.0, -2.0);
    if (extremaLabel_ && extremaLabel_->isVisible() && !infoPositions_.contains(extremaLabel_))
        labelArea.setTop(std::max(labelArea.top(), extremaLabel_->mapRectToParent(extremaLabel_->boundingRect()).bottom() + 4));
    if (labelArea.width() <= 0.0 || labelArea.height() <= 0.0) return;
    const qreal gap = 3.0;
    qreal maxHeight = 0.0;
    for (const auto& placement : placements) {
        maxHeight = std::max(maxHeight, placement.height);
    }
    const int laneCapacity = std::max(
        1, static_cast<int>((labelArea.height() + gap) / std::max(1.0, maxHeight + gap)));
    const int laneCount = (placements.size() + laneCapacity - 1) / laneCapacity;

    struct LaneLayout {
        int begin = 0;
        int end = 0;
        qreal width = 0.0;
        bool placeRight = true;
        qreal x = 0.0;
    };
    QVector<LaneLayout> lanes;
    lanes.reserve(laneCount);
    for (int laneIndex = 0; laneIndex < laneCount; ++laneIndex) {
        LaneLayout lane;
        lane.begin = laneIndex * laneCapacity;
        lane.end = std::min(lane.begin + laneCapacity,
                            static_cast<int>(placements.size()));
        for (int i = lane.begin; i < lane.end; ++i) {
            lane.width = std::max(lane.width, placements[i].width);
        }

        qreal nextTop = labelArea.top();
        for (int i = lane.begin; i < lane.end; ++i) {
            auto& placement = placements[i];
            const qreal latestTop = std::max(
                labelArea.top(), labelArea.bottom() - placement.height);
            placement.top = std::clamp(placement.desiredTop, labelArea.top(), latestTop);
            placement.top = std::max(placement.top, nextTop);
            nextTop = placement.top + placement.height + gap;
        }
        for (int i = lane.end - 1; i >= lane.begin; --i) {
            auto& placement = placements[i];
            const qreal latestTop = i == lane.end - 1
                ? labelArea.bottom() - placement.height
                : placements[i + 1].top - gap - placement.height;
            placement.top = std::min(placement.top, latestTop);
        }
        if (placements[lane.begin].top < labelArea.top()) {
            placements[lane.begin].top = labelArea.top();
            for (int i = lane.begin + 1; i < lane.end; ++i) {
                placements[i].top = std::max(
                    placements[i].top,
                    placements[i - 1].top + placements[i - 1].height + gap);
            }
        }
        lanes.append(lane);
    }

    const qreal cursorPixelX = top.x();
    qreal rightBoundary = labelArea.right();
    if (legendPanel_ && legendPanel_->isVisible() && !infoPositions_.contains(legendPanel_)) {
        rightBoundary = std::min(rightBoundary, legendPanel_->pos().x() - gap);
    }
    qreal rightX = cursorPixelX + 6.0;
    qreal leftX = cursorPixelX - 6.0;
    for (auto& lane : lanes) {
        const qreal rightSpace = rightBoundary - rightX;
        const qreal leftSpace = leftX - labelArea.left();
        lane.placeRight = rightSpace >= lane.width
            && (leftSpace < lane.width || rightSpace >= leftSpace);
        if (lane.placeRight) {
            lane.x = std::clamp(rightX, labelArea.left(),
                                std::max(labelArea.left(), rightBoundary - lane.width));
            rightX = lane.x + lane.width + gap;
        } else {
            lane.x = std::clamp(leftX - lane.width, labelArea.left(),
                                std::max(labelArea.left(), labelArea.right() - lane.width));
            leftX = lane.x - gap;
        }
    }

    for (const auto& lane : lanes) {
        for (int i = lane.begin; i < lane.end; ++i) {
            auto& placement = placements[i];
            const qreal x = lane.placeRight ? lane.x
                                            : lane.x + lane.width - placement.width;
            placement.label->setPos(QPointF(x, placement.top)
                                    - placement.label->boundingRect().topLeft());
            placement.label->setVisible(true);
            if (cursorBlocksDetached_ && !infoPositions_.contains(placement.label)) RememberInfoBlock(placement.label);
        }
    }
    PlaceInfoBlocks();
}
