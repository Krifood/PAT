#include "ui/TrajectoryView.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <cmath>

namespace {

int CoordinateScore(const QString& name, bool longitude) {
    const QString normalized = name.trimmed().toLower();
    const QString chinese = longitude ? QStringLiteral("经度") : QStringLiteral("纬度");
    const QString english = longitude ? QStringLiteral("longitude") : QStringLiteral("latitude");
    const QString shortName = longitude ? QStringLiteral("lon") : QStringLiteral("lat");
    if (normalized == QStringLiteral("组合") + chinese) return 100;
    if (normalized.contains(QStringLiteral("组合")) && normalized.contains(chinese)) return 90;
    if (normalized.contains(QStringLiteral("gnss")) && normalized.contains(chinese)) return 80;
    if (normalized.contains(chinese)) return 70;
    if (normalized.contains(english)) return 60;
    if (normalized == shortName || normalized.endsWith(QStringLiteral("_") + shortName)) return 50;
    return -1;
}

void SetPaddedRange(QValueAxis* axis, double minimum, double maximum) {
    const double span = maximum - minimum;
    const double center = minimum + span / 2.0;
    const double padding = span > 0.0 && std::isfinite(span)
                               ? span * 0.05
                               : std::max(1e-6, std::abs(center) * 1e-7);
    const double low = minimum - padding;
    const double high = maximum + padding;
    if (!std::isfinite(low) || !std::isfinite(high) || high <= low) {
        axis->setRange(-1.0, 1.0);
        axis->setLabelFormat(QStringLiteral("%.3e"));
        return;
    }
    axis->setRange(low, high);
    const double largest = std::max(std::abs(low), std::abs(high));
    if (largest >= 1e6 || (largest > 0.0 && largest < 1e-4)) {
        axis->setLabelFormat(QStringLiteral("%.3e"));
    } else {
        const int decimals = std::clamp(int(std::ceil(-std::log10(high - low))) + 2, 1, 9);
        axis->setLabelFormat(QString::asprintf("%%.%df", decimals));
    }
}

}  // namespace

TrajectoryView::TrajectoryView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("trajectoryView"));
    setStyleSheet(QStringLiteral(R"(
        QWidget#trajectoryView { background: #1a2128; }
        QWidget#trajectoryView QLabel, QWidget#trajectoryView QCheckBox { color: #dbe5eb; }
        QWidget#trajectoryView QComboBox {
            background: #203139; color: #dbe5eb; border: 1px solid #48606a;
            min-width: 130px; padding: 3px;
        }
    )"));

    auto* layout = new QVBoxLayout(this);
    auto* controls = new QHBoxLayout;
    xCombo_ = new QComboBox(this);
    xCombo_->setObjectName(QStringLiteral("trajectoryXCombo"));
    yCombo_ = new QComboBox(this);
    yCombo_->setObjectName(QStringLiteral("trajectoryYCombo"));
    autoRange_ = new QCheckBox(tr("自动范围"), this);
    autoRange_->setChecked(true);
    autoRange_->setToolTip(tr("关闭后可在轨迹图中框选缩放；再次开启可恢复全轨迹范围"));
    controls->addWidget(new QLabel(tr("横轴 X"), this));
    controls->addWidget(xCombo_);
    controls->addWidget(new QLabel(tr("纵轴 Y"), this));
    controls->addWidget(yCombo_);
    controls->addWidget(autoRange_);
    controls->addStretch();
    layout->addLayout(controls);

    auto* chart = new QChart;
    chart->setBackgroundBrush(QColor(23, 35, 46));
    chart->setPlotAreaBackgroundBrush(QColor(26, 33, 40));
    chart->setPlotAreaBackgroundVisible(true);
    chart->legend()->hide();
    chart->setMargins(QMargins(8, 8, 8, 8));

    pathSeries_ = new QLineSeries(chart);
    pathSeries_->setName(tr("轨迹"));
    pathSeries_->setColor(QColor(90, 200, 255));
    chart->addSeries(pathSeries_);
    currentSeries_ = new QScatterSeries(chart);
    currentSeries_->setName(tr("当前位置"));
    currentSeries_->setColor(QColor(255, 170, 55));
    currentSeries_->setMarkerSize(11.0);
    chart->addSeries(currentSeries_);

    xAxis_ = new QValueAxis(chart);
    yAxis_ = new QValueAxis(chart);
    xAxis_->setLabelFormat(QStringLiteral("%.7f"));
    yAxis_->setLabelFormat(QStringLiteral("%.7f"));
    xAxis_->setRange(-1.0, 1.0);
    yAxis_->setRange(-1.0, 1.0);
    xAxis_->setTickCount(5);
    yAxis_->setTickCount(5);
    chart->addAxis(xAxis_, Qt::AlignBottom);
    chart->addAxis(yAxis_, Qt::AlignLeft);
    pathSeries_->attachAxis(xAxis_);
    pathSeries_->attachAxis(yAxis_);
    currentSeries_->attachAxis(xAxis_);
    currentSeries_->attachAxis(yAxis_);

    chartView_ = new QChartView(chart, this);
    chartView_->setObjectName(QStringLiteral("trajectoryChart"));
    chartView_->setRenderHint(QPainter::Antialiasing, false);
    chartView_->setRubberBand(QChartView::RectangleRubberBand);
    layout->addWidget(chartView_, 1);

    status_ = new QLabel(tr("请先加载格式并选择 X、Y 数据列"), this);
    status_->setObjectName(QStringLiteral("trajectoryStatus"));
    layout->addWidget(status_);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(100);
    connect(refreshTimer_, &QTimer::timeout, this, &TrajectoryView::RefreshNow);
    connect(xCombo_, &QComboBox::currentIndexChanged, this, [this]() { RefreshNow(); });
    connect(yCombo_, &QComboBox::currentIndexChanged, this, [this]() { RefreshNow(); });
    connect(autoRange_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) RefreshNow();
    });
}

void TrajectoryView::ConfigureSignals(const pat::FormatDefinition& format) {
    source_ = nullptr;
    refreshTimer_->stop();
    QSignalBlocker blockX(xCombo_);
    QSignalBlocker blockY(yCombo_);
    xCombo_->clear();
    yCombo_->clear();
    xCombo_->addItem(tr("请选择..."), -1);
    yCombo_->addItem(tr("请选择..."), -1);
    int bestXScore = -1;
    int bestYScore = -1;
    int bestX = -1;
    int bestY = -1;
    for (size_t i = 0; i < format.signalFormats.size(); ++i) {
        const auto& signal = format.signalFormats[i];
        const QString label = signal.unit.isEmpty() ? signal.name
            : tr("%1 (%2)").arg(signal.name, signal.unit);
        xCombo_->addItem(label, static_cast<int>(i));
        yCombo_->addItem(label, static_cast<int>(i));
        const int xScore = CoordinateScore(signal.name, true);
        const int yScore = CoordinateScore(signal.name, false);
        if (xScore > bestXScore) { bestXScore = xScore; bestX = static_cast<int>(i) + 1; }
        if (yScore > bestYScore) { bestYScore = yScore; bestY = static_cast<int>(i) + 1; }
    }
    xCombo_->setCurrentIndex(bestX > 0 ? bestX : 0);
    yCombo_->setCurrentIndex(bestY > 0 ? bestY : 0);
    ClearPlot(tr("请加载数据；可在上方调整 X、Y 数据列"));
    dirty_ = true;
}

void TrajectoryView::SetDataSource(const QVector<pat::Series>* series,
                                   pat::TrajectoryPairing pairing) {
    source_ = series;
    pairing_ = pairing;
    dirty_ = true;
    if (isVisible() && !paused_) RefreshNow();
}

void TrajectoryView::SetPaused(bool paused) {
    paused_ = paused;
    if (paused_) refreshTimer_->stop();
    else if (dirty_) RequestRefresh();
}

void TrajectoryView::RequestRefresh() {
    dirty_ = true;
    if (!paused_ && isVisible() && !refreshTimer_->isActive()) refreshTimer_->start();
}

void TrajectoryView::RefreshNow() {
    if (paused_) { dirty_ = true; return; }
    refreshTimer_->stop();
    if (!isVisible()) { dirty_ = true; return; }
    dirty_ = false;
    const int xIndex = xCombo_->currentData().toInt();
    const int yIndex = yCombo_->currentData().toInt();
    if (xIndex < 0 || yIndex < 0 || xIndex == yIndex) {
        ClearPlot(tr("请选择两个不同的 X、Y 数据列"));
        return;
    }
    if (!source_ || xIndex >= source_->size() || yIndex >= source_->size()) {
        ClearPlot(tr("等待数据；可在上方调整 X、Y 数据列"));
        return;
    }

    const auto& xSeries = source_->at(xIndex);
    const auto& ySeries = source_->at(yIndex);
    xAxis_->setTitleText(xSeries.unit.isEmpty() ? xSeries.name
        : tr("%1 (%2)").arg(xSeries.name, xSeries.unit));
    yAxis_->setTitleText(ySeries.unit.isEmpty() ? ySeries.name
        : tr("%1 (%2)").arg(ySeries.name, ySeries.unit));
    const QVector<QPointF> points = pat::PairTrajectorySamples(xSeries, ySeries, pairing_);
    if (points.isEmpty()) {
        ClearPlot(tr("等待两个数据列在同一采样点上出现有效数值"));
        return;
    }

    constexpr int kMaxDrawPoints = 4000;
    const QVector<QPointF> drawn = pat::DecimateTrajectory(points, kMaxDrawPoints);
    pathSeries_->replace(drawn);
    currentSeries_->replace(QVector<QPointF>{points.last()});
    if (autoRange_->isChecked()) {
        double minX = points.first().x(), maxX = minX;
        double minY = points.first().y(), maxY = minY;
        for (const auto& point : points) {
            minX = std::min(minX, point.x());
            maxX = std::max(maxX, point.x());
            minY = std::min(minY, point.y());
            maxY = std::max(maxY, point.y());
        }
        SetPaddedRange(xAxis_, minX, maxX);
        SetPaddedRange(yAxis_, minY, maxY);
    }
    status_->setText(tr("有效点 %1；绘制点 %2；仅为数值平面，不含地图底图")
                         .arg(points.size()).arg(drawn.size()));
}

void TrajectoryView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (dirty_ && !paused_) QTimer::singleShot(0, this, &TrajectoryView::RefreshNow);
}

void TrajectoryView::ClearPlot(const QString& message) {
    pathSeries_->clear();
    currentSeries_->clear();
    status_->setText(message);
}
