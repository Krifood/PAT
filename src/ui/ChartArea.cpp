#include "ui/ChartArea.h"

#include "ui/SignalTreeWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSplitter>
#include <QVBoxLayout>

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {

QVector<QColor> SeriesPalette() {
    return {
        QColor(90, 200, 255),
        QColor(255, 140, 0),
        QColor(120, 220, 120),
        QColor(220, 120, 220),
        QColor(255, 90, 90),
        QColor(120, 160, 255)
    };
}

}  // namespace

ChartArea::ChartArea(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    container_ = new QWidget(scrollArea_);
    auto* containerLayout = new QVBoxLayout(container_);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    splitter_ = new QSplitter(Qt::Vertical, container_);
    splitter_->setChildrenCollapsible(false);
    splitter_->setHandleWidth(3);
    containerLayout->addWidget(splitter_);
    container_->setLayout(containerLayout);
    scrollArea_->setWidget(container_);
    layout->addWidget(scrollArea_, /*stretch=*/1);

    scrollArea_->viewport()->setAcceptDrops(true);
    scrollArea_->viewport()->installEventFilter(this);
    container_->setAcceptDrops(true);
    container_->installEventFilter(this);

    connect(splitter_, &QSplitter::splitterMoved, this, [this](int, int index) {
        const QList<int> sizes = splitter_ ? splitter_->sizes() : QList<int>{};
        if (sizes.isEmpty()) return;
        const int widgetIndex = std::clamp(index, 0, static_cast<int>(sizes.size() - 1));
        chartHeight_ = sizes.value(widgetIndex);
        UpdateChartHeights();
    });
}

void ChartArea::SetSeries(const QVector<pat::Series>* series) {
    series_ = series;
}

void ChartArea::SetDisplayGroups(const QVector<DisplayGroup>& groups) {
    groups_ = groups;
}

void ChartArea::SetStatistics(const pat::SeriesStatistics& stats) {
    stats_ = stats;
    hasStats_ = true;
    minXSpan_ = stats_.minStep;
    if (!hasCurrentRange_) {
        currentMinX_ = 0.0;
        currentMaxX_ = stats_.maxX;
        hasCurrentRange_ = true;
    }
}

void ChartArea::SetTimeUnit(const QString& unit) {
    timeUnit_ = unit.trimmed().isEmpty() ? QStringLiteral("s") : unit.trimmed();
}

void ChartArea::SetMaxVisiblePoints(int maxPoints) {
    if (maxPoints <= 0) return;
    maxVisiblePoints_ = maxPoints;
    if (hasCurrentRange_) {
        RefreshVisibleSeries(currentMinX_, currentMaxX_);
    }
}

void ChartArea::ResetXRange() {
    if (!hasStats_) return;
    ApplyXRange(0.0, stats_.maxX);
}

void ChartArea::RefreshCharts() {
    BuildCharts();
}

bool ChartArea::eventFilter(QObject* obj, QEvent* event) {
    if (obj == scrollArea_->viewport() || obj == container_) {
        auto* widget = qobject_cast<QWidget*>(obj);
        if (!widget) return QWidget::eventFilter(obj, event);

        auto chartAt = [widget](const QPoint& localPos) -> SignalChartView* {
            const QPoint globalPos = widget->mapToGlobal(localPos);
            QWidget* hovered = QApplication::widgetAt(globalPos);
            while (hovered) {
                if (auto* chart = qobject_cast<SignalChartView*>(hovered)) return chart;
                hovered = hovered->parentWidget();
            }
            return nullptr;
        };

        if (event->type() == QEvent::DragEnter) {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            const auto* mime = dragEvent->mimeData();
            if (!mime) return false;
            const bool isSignalDrag = mime->hasFormat(SignalTreeWidget::SIGNAL_INDICES_MIME);
            const bool isChartDrag = mime->hasFormat(SignalChartView::CHART_REORDER_MIME);
            if (!isSignalDrag && !isChartDrag) return false;

            const QPoint pos = dragEvent->position().toPoint();
            if (auto* target = chartAt(pos)) {
                const QPoint chartPos = target->mapFromGlobal(widget->mapToGlobal(pos));
                QDragEnterEvent forwarded(chartPos,
                                          dragEvent->possibleActions(),
                                          mime,
                                          dragEvent->buttons(),
                                          dragEvent->modifiers());
                forwarded.setDropAction(dragEvent->dropAction());
                QCoreApplication::sendEvent(target, &forwarded);
                if (forwarded.isAccepted()) {
                    dragEvent->acceptProposedAction();
                } else {
                    dragEvent->ignore();
                }
                return true;
            }

            if (isSignalDrag) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            const auto* mime = dragEvent->mimeData();
            if (!mime) return false;
            const bool isSignalDrag = mime->hasFormat(SignalTreeWidget::SIGNAL_INDICES_MIME);
            const bool isChartDrag = mime->hasFormat(SignalChartView::CHART_REORDER_MIME);
            if (!isSignalDrag && !isChartDrag) return false;

            const QPoint pos = dragEvent->position().toPoint();
            if (auto* target = chartAt(pos)) {
                const QPoint chartPos = target->mapFromGlobal(widget->mapToGlobal(pos));
                QDragMoveEvent forwarded(chartPos,
                                         dragEvent->possibleActions(),
                                         mime,
                                         dragEvent->buttons(),
                                         dragEvent->modifiers());
                forwarded.setDropAction(dragEvent->dropAction());
                QCoreApplication::sendEvent(target, &forwarded);
                if (forwarded.isAccepted()) {
                    dragEvent->acceptProposedAction();
                } else {
                    dragEvent->ignore();
                }
                return true;
            }

            if (isSignalDrag) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const auto* mime = dropEvent->mimeData();
            if (!mime) return false;
            const bool isSignalDrag = mime->hasFormat(SignalTreeWidget::SIGNAL_INDICES_MIME);
            const bool isChartDrag = mime->hasFormat(SignalChartView::CHART_REORDER_MIME);
            if (!isSignalDrag && !isChartDrag) return false;

            const QPoint pos = dropEvent->position().toPoint();
            if (auto* target = chartAt(pos)) {
                const QPoint chartPos = target->mapFromGlobal(widget->mapToGlobal(pos));
                QDropEvent forwarded(QPointF(chartPos),
                                     dropEvent->possibleActions(),
                                     mime,
                                     dropEvent->buttons(),
                                     dropEvent->modifiers());
                forwarded.setDropAction(dropEvent->dropAction());
                QCoreApplication::sendEvent(target, &forwarded);
                if (forwarded.isAccepted()) {
                    dropEvent->acceptProposedAction();
                } else {
                    dropEvent->ignore();
                }
                return true;
            }

            if (isSignalDrag) {
                QByteArray payload = mime->data(SignalTreeWidget::SIGNAL_INDICES_MIME);
                QDataStream stream(&payload, QIODevice::ReadOnly);
                QVector<int> indices;
                stream >> indices;
                if (!indices.isEmpty()) {
                    emit SignalsDropped(indices);
                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        }
    }

    return QWidget::eventFilter(obj, event);
}

void ChartArea::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    UpdateChartHeights();
}

void ChartArea::HandleCursorMoved(double cursorX) {
    sharedCursorX_ = cursorX;
    cursorActive_ = true;
    for (auto* chart : charts_) {
        if (chart) chart->SetCursorX(cursorX);
    }
}

void ChartArea::HandleCursorLeft() {
    cursorActive_ = false;
    for (auto* chart : charts_) {
        if (chart) chart->HideCursor();
    }
}

void ChartArea::HandleXRangeRequested(double minX, double maxX) {
    ApplyXRange(minX, maxX);
}

void ChartArea::HandleResetXRange() {
    ResetXRange();
}

void ChartArea::HandleMergeDropped(const QVector<int>& indices, int targetIndex) {
    Q_UNUSED(targetIndex)
    emit MergeRequested(indices);
}

void ChartArea::HandleChartMergeRequested(int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= groups_.size() || toIndex >= groups_.size()) return;
    if (fromIndex == toIndex) return;

    QVector<int> merged = groups_.value(toIndex).signalIndices;
    const auto& source = groups_.value(fromIndex).signalIndices;
    for (int idx : source) {
        if (idx >= 0 && !merged.contains(idx)) merged.append(idx);
    }
    if (merged.size() < 2) return;
    emit MergeRequested(merged);
}

void ChartArea::HandleReorderRequested(int fromIndex, int toIndex) {
    if (fromIndex < 0 || toIndex < 0 || fromIndex >= groups_.size() || toIndex >= groups_.size()) return;
    if (fromIndex == toIndex) return;

    DisplayGroup moved = groups_.takeAt(fromIndex);
    groups_.insert(toIndex, moved);
    BuildCharts();
    emit ReorderRequested(fromIndex, toIndex);
}

void ChartArea::HandleHideSignalsRequested(const QVector<int>& indices) {
    emit HideSignalsRequested(indices);
}

void ChartArea::BuildCharts() {
    ClearCharts();

    if (!series_ || groups_.isEmpty() || !splitter_) return;
    if (!hasStats_) return;

    const double viewMinX = hasCurrentRange_ ? currentMinX_ : 0.0;
    const double viewMaxX = hasCurrentRange_ ? currentMaxX_ : stats_.maxX;
    const auto palette = SeriesPalette();

    for (int groupIndex = 0; groupIndex < groups_.size(); ++groupIndex) {
        const auto& group = groups_[groupIndex];
        double groupMinY = -1.0;
        double groupMaxY = 1.0;
        if (!ComputeGroupRange(group.signalIndices, groupMinY, groupMaxY)) {
            groupMinY = -1.0;
            groupMaxY = 1.0;
        }

        QVector<QVector<QPointF>> seriesSamples;
        seriesSamples.reserve(group.signalIndices.size());
        for (int idx : group.signalIndices) {
            if (!series_ || idx < 0 || idx >= series_->size()) {
                seriesSamples.append(QVector<QPointF>{});
                continue;
            }
            seriesSamples.append(DecimateSamples(series_->at(idx).samples, viewMinX, viewMaxX, maxVisiblePoints_));
        }

        auto* view = new SignalChartView(splitter_);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        view->Configure(group.title,
                        group.unit,
                        timeUnit_,
                        group.merged,
                        palette,
                        group.signalIndices,
                        seriesSamples,
                        series_,
                        groupMinY,
                        groupMaxY,
                        viewMinX,
                        viewMaxX,
                        groupIndex);

        connect(view, &SignalChartView::CursorMoved, this, &ChartArea::HandleCursorMoved);
        connect(view, &SignalChartView::CursorLeft, this, &ChartArea::HandleCursorLeft);
        connect(view, &SignalChartView::XRangeRequested, this, &ChartArea::HandleXRangeRequested);
        connect(view, &SignalChartView::ResetXRangeRequested, this, &ChartArea::HandleResetXRange);
        connect(view, &SignalChartView::MergeDropped, this, &ChartArea::HandleMergeDropped);
        connect(view, &SignalChartView::ChartMergeRequested, this, &ChartArea::HandleChartMergeRequested);
        connect(view, &SignalChartView::ReorderRequested, this, &ChartArea::HandleReorderRequested);
        connect(view, &SignalChartView::HideSignalsRequested, this, &ChartArea::HandleHideSignalsRequested);

        splitter_->addWidget(view);
        splitter_->setStretchFactor(splitter_->indexOf(view), 1);
        charts_.append(view);
    }

    UpdateChartHeights();
    ApplyXRange(viewMinX, viewMaxX);
}

void ChartArea::ClearCharts() {
    charts_.clear();
    cursorActive_ = false;
    if (!splitter_) return;
    while (splitter_->count() > 0) {
        auto* widget = splitter_->widget(0);
        if (!widget) break;
        widget->setParent(nullptr);
        widget->deleteLater();
    }
}

void ChartArea::ApplyXRange(double minX, double maxX) {
    if (!hasStats_) return;
    const double boundMin = 0.0;
    const double boundMax = stats_.maxX;
    minX = std::max(boundMin, minX);
    maxX = std::min(boundMax, maxX);
    if (maxX <= minX) return;

    if (maxX - minX < minXSpan_) {
        const double center = (minX + maxX) * 0.5;
        minX = center - minXSpan_ * 0.5;
        maxX = center + minXSpan_ * 0.5;
        if (minX < boundMin) {
            minX = boundMin;
            maxX = std::min(boundMax, boundMin + minXSpan_);
        }
        if (maxX > boundMax) {
            maxX = boundMax;
            minX = std::max(boundMin, boundMax - minXSpan_);
        }
        if (maxX <= minX) return;
    }

    currentMinX_ = minX;
    currentMaxX_ = maxX;
    hasCurrentRange_ = true;

    for (auto* chart : charts_) {
        if (chart) chart->SetXAxisRange(minX, maxX);
    }

    RefreshVisibleSeries(minX, maxX);
    UpdateRangeContext();

    if (cursorActive_) {
        if (sharedCursorX_ < currentMinX_) sharedCursorX_ = currentMinX_;
        if (sharedCursorX_ > currentMaxX_) sharedCursorX_ = currentMaxX_;
        for (auto* chart : charts_) {
            if (chart) chart->SetCursorX(sharedCursorX_);
        }
    }
}

void ChartArea::RefreshVisibleSeries(double minX, double maxX) {
    if (!series_) return;

    for (int i = 0; i < charts_.size(); ++i) {
        auto* chart = charts_[i];
        if (!chart) continue;
        QVector<QVector<QPointF>> samples;
        const auto& indices = chart->SeriesIndices();
        samples.reserve(indices.size());
        for (int idx : indices) {
            if (idx < 0 || idx >= series_->size()) {
                samples.append(QVector<QPointF>{});
                continue;
            }
            samples.append(DecimateSamples(series_->at(idx).samples, minX, maxX, maxVisiblePoints_));
        }
        chart->SetSeriesSamples(samples);
    }
}

QVector<QPointF> ChartArea::DecimateSamples(const QVector<QPointF>& samples,
                                            double minX,
                                            double maxX,
                                            int maxPoints) const {
    if (samples.isEmpty()) return {};
    if (maxPoints <= 0) return {};
    if (maxX < minX) std::swap(minX, maxX);

    auto begin = samples.begin();
    auto end = samples.end();
    auto startIt = std::lower_bound(begin, end, minX, [](const QPointF& p, double x) { return p.x() < x; });
    auto endIt = std::upper_bound(startIt, end, maxX, [](double x, const QPointF& p) { return x < p.x(); });
    const int count = static_cast<int>(endIt - startIt);
    if (count <= 0) return {};
    if (count <= maxPoints) {
        return QVector<QPointF>(startIt, endIt);
    }

    const int bucketCount = std::max(1, maxPoints / 2);
    const double span = maxX - minX;
    const double bucketSize = span > 0.0 ? span / bucketCount : 1.0;

    QVector<QPointF> out;
    out.reserve(maxPoints);
    out.append(*startIt);
    for (int b = 0; b < bucketCount; ++b) {
        const double bx0 = minX + bucketSize * b;
        const double bx1 = (b == bucketCount - 1) ? maxX : (bx0 + bucketSize);
        auto b0 = std::lower_bound(startIt, endIt, bx0, [](const QPointF& p, double x) { return p.x() < x; });
        auto b1 = std::lower_bound(b0, endIt, bx1, [](const QPointF& p, double x) { return p.x() < x; });
        if (b0 == b1) continue;
        auto minIt = b0;
        auto maxIt = b0;
        for (auto it = b0; it != b1; ++it) {
            if (it->y() < minIt->y()) minIt = it;
            if (it->y() > maxIt->y()) maxIt = it;
        }
        if (minIt->x() <= maxIt->x()) {
            out.append(*minIt);
            if (maxIt != minIt) out.append(*maxIt);
        } else {
            out.append(*maxIt);
            if (maxIt != minIt) out.append(*minIt);
        }
        if (out.size() >= maxPoints) break;
    }
    out.append(*(endIt - 1));
    return out;
}

bool ChartArea::ComputeGroupRange(const QVector<int>& indices, double& outMinY, double& outMaxY) const {
    if (!series_) return false;
    bool hasRange = false;
    outMinY = -1.0;
    outMaxY = 1.0;

    for (int idx : indices) {
        if (idx < 0 || idx >= series_->size()) continue;
        const auto& series = series_->at(idx);
        for (const auto& pt : series.samples) {
            if (!hasRange) {
                outMinY = outMaxY = pt.y();
                hasRange = true;
            } else {
                outMinY = std::min(outMinY, pt.y());
                outMaxY = std::max(outMaxY, pt.y());
            }
        }
    }

    if (!hasRange) return false;
    if (qFuzzyCompare(outMinY, outMaxY)) {
        const double delta = std::abs(outMinY) > 1.0 ? std::abs(outMinY) * 0.1 : 1.0;
        outMinY -= delta;
        outMaxY += delta;
    }
    return true;
}

void ChartArea::UpdateChartHeights() {
    if (!splitter_ || !scrollArea_) return;
    const int viewportHeight = scrollArea_->viewport()->height();
    minChartHeight_ = std::max(80, viewportHeight / 6);
    maxChartHeight_ = std::max(minChartHeight_, viewportHeight);
    chartHeight_ = std::clamp(chartHeight_, minChartHeight_, maxChartHeight_);

    for (auto* chart : charts_) {
        if (!chart) continue;
        chart->setMinimumHeight(minChartHeight_);
        chart->setMaximumHeight(maxChartHeight_);
    }

    QList<int> sizes;
    for (int i = 0; i < splitter_->count(); ++i) sizes.append(chartHeight_);
    if (!sizes.isEmpty()) {
        splitter_->setSizes(sizes);
    }
}

void ChartArea::UpdateRangeContext() {
    ChartRangeContext context;
    context.globalMinX = 0.0;
    context.globalMaxX = stats_.maxX;
    context.currentMinX = currentMinX_;
    context.currentMaxX = currentMaxX_;
    context.minSpan = minXSpan_;
    context.hasCurrentRange = hasCurrentRange_;

    for (auto* chart : charts_) {
        if (chart) chart->SetRangeContext(context);
    }
}
