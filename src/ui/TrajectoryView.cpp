#include "ui/TrajectoryView.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLegend>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>

namespace {

constexpr const char* kTrajectoryPlotMime = "application/x-pat-trajectory-plot";
constexpr int kMaxDrawPointsPerTrack = 4000;

int CoordinateScore(const QString& name, bool longitude, bool latitude, bool altitude) {
    const QString normalized = name.trimmed().toLower();
    QStringList terms;
    if (longitude) terms = {QStringLiteral("经度"), QStringLiteral("longitude"), QStringLiteral("lon")};
    if (latitude) terms = {QStringLiteral("纬度"), QStringLiteral("latitude"), QStringLiteral("lat")};
    if (altitude) terms = {QStringLiteral("高度"), QStringLiteral("海拔"), QStringLiteral("altitude"),
                           QStringLiteral("height"), QStringLiteral("alt")};
    int score = -1;
    for (const auto& term : terms) {
        if (normalized == term) score = std::max(score, 70);
        else if (normalized.contains(term)) score = std::max(score, 50);
    }
    if (score >= 0 && normalized.contains(QStringLiteral("组合"))) score += 40;
    else if (score >= 0 && normalized.contains(QStringLiteral("gnss"))) score += 20;
    return score;
}

void SetPaddedRange(QValueAxis* axis, double minimum, double maximum) {
    const double span = maximum - minimum;
    const double center = minimum + span / 2.0;
    const double padding = span > 0.0 && std::isfinite(span)
        ? span * 0.05 : std::max(1e-6, std::abs(center) * 1e-7);
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

class Trajectory3DCanvas final : public QWidget {
public:
    struct Track {
        QString name;
        QColor color;
        QVector<pat::TrajectoryPoint3D> points;
    };

    explicit Trajectory3DCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(230);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    void SetTheme(const AppTheme& theme) {
        theme_ = theme;
        update();
    }

    void SetTracks(QVector<Track> tracks) {
        tracks_ = std::move(tracks);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), theme_.plotBackgroundEven);
        const QRectF area = rect().adjusted(48, 28, -24, -42);
        painter.setPen(QPen(theme_.chartBorderEven, 1.5));
        painter.drawRect(area);

        bool hasPoints = false;
        double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0, minZ = 0.0, maxZ = 0.0;
        for (const auto& track : tracks_) {
            for (const auto& point : track.points) {
                if (!hasPoints) {
                    minX = maxX = point.x;
                    minY = maxY = point.y;
                    minZ = maxZ = point.z;
                    hasPoints = true;
                } else {
                    minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
                    minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
                    minZ = std::min(minZ, point.z); maxZ = std::max(maxZ, point.z);
                }
            }
        }
        if (!hasPoints) {
            painter.setPen(theme_.axisText);
            painter.drawText(area, Qt::AlignCenter, tr("等待三列有效数据"));
            return;
        }

        const double spanX = std::max(1e-12, maxX - minX);
        const double spanY = std::max(1e-12, maxY - minY);
        const double spanZ = std::max(1e-12, maxZ - minZ);
        const double yaw = yawDegrees_ * std::numbers::pi / 180.0;
        const double pitch = pitchDegrees_ * std::numbers::pi / 180.0;
        const double cy = std::cos(yaw), sy = std::sin(yaw);
        const double cp = std::cos(pitch), sp = std::sin(pitch);
        const double baseScale = std::min(area.width(), area.height()) * 0.34 * zoom_;
        const QPointF center = area.center();
        const auto projectNormalized = [&](double nx, double ny, double nz) {
            const double rx = nx * cy - nz * sy;
            const double rz = nx * sy + nz * cy;
            const double ry = ny * cp - rz * sp;
            const double depth = ny * sp + rz * cp;
            const double perspective = 1.0 / std::max(1.5, 3.4 + depth * 0.45);
            return QPointF(center.x() + rx * baseScale * perspective * 3.0,
                           center.y() - ry * baseScale * perspective * 3.0);
        };
        const auto projectPoint = [&](const pat::TrajectoryPoint3D& point) {
            return projectNormalized((point.x - minX) / spanX * 2.0 - 1.0,
                                     (point.y - minY) / spanY * 2.0 - 1.0,
                                     (point.z - minZ) / spanZ * 2.0 - 1.0);
        };

        static constexpr std::array<std::array<int, 2>, 12> edges = {{{0,1},{0,2},{0,4},
            {1,3},{1,5},{2,3},{2,6},{3,7},{4,5},{4,6},{5,7},{6,7}}};
        std::array<QPointF, 8> cube;
        for (int i = 0; i < 8; ++i) {
            cube[static_cast<size_t>(i)] = projectNormalized(
                (i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0, (i & 4) ? 1.0 : -1.0);
        }
        painter.setPen(QPen(theme_.gridLine, 1.0, Qt::DashLine));
        for (const auto& edge : edges) painter.drawLine(cube[edge[0]], cube[edge[1]]);

        for (const auto& track : tracks_) {
            if (track.points.isEmpty()) continue;
            QPainterPath path;
            path.moveTo(projectPoint(track.points.first()));
            for (qsizetype i = 1; i < track.points.size(); ++i) path.lineTo(projectPoint(track.points[i]));
            painter.setPen(QPen(track.color, theme_.seriesLineWidth));
            painter.drawPath(path);
            painter.setBrush(track.color);
            painter.setPen(QPen(theme_.guideOutline, 1.0));
            painter.drawEllipse(projectPoint(track.points.last()), 4.5, 4.5);
        }

        painter.setPen(theme_.axisText);
        painter.drawText(QRectF(area.left(), area.bottom() + 8, area.width(), 22),
                         Qt::AlignCenter, tr("拖动旋转 · 滚轮缩放 · 双击复位"));
        qreal legendX = area.left() + 8;
        const qreal legendY = area.top() + 8;
        for (const auto& track : tracks_) {
            painter.setPen(QPen(track.color, 3.0));
            painter.drawLine(QPointF(legendX, legendY + 6), QPointF(legendX + 18, legendY + 6));
            painter.setPen(theme_.axisText);
            painter.drawText(QPointF(legendX + 23, legendY + 11), track.name);
            legendX += 30 + painter.fontMetrics().horizontalAdvance(track.name);
            if (legendX > area.right() - 100) break;
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            rotating_ = true;
            lastMouse_ = event->position().toPoint();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (rotating_ && (event->buttons() & Qt::LeftButton)) {
            const QPoint delta = event->position().toPoint() - lastMouse_;
            lastMouse_ = event->position().toPoint();
            yawDegrees_ += delta.x() * 0.6;
            pitchDegrees_ = std::clamp(pitchDegrees_ + delta.y() * 0.6, -89.0, 89.0);
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) rotating_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override {
        yawDegrees_ = -35.0;
        pitchDegrees_ = 24.0;
        zoom_ = 1.0;
        update();
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override {
        zoom_ = std::clamp(zoom_ * (event->angleDelta().y() > 0 ? 1.12 : 0.89), 0.35, 4.0);
        update();
        event->accept();
    }

private:
    QVector<Track> tracks_;
    AppTheme theme_ = ThemeFor(AppThemeId::IndoorDark);
    bool rotating_ = false;
    QPoint lastMouse_;
    double yawDegrees_ = -35.0;
    double pitchDegrees_ = 24.0;
    double zoom_ = 1.0;
};

TrajectoryPlotWidget::TrajectoryPlotWidget(int plotId, TrajectoryDimension dimension, QWidget* parent)
    : QFrame(parent), plotId_(plotId), dimension_(dimension) {
    setObjectName(QStringLiteral("trajectoryPlot_%1").arg(plotId_));
    setFrameShape(QFrame::StyledPanel);
    setMinimumHeight(300);
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    auto* header = new QHBoxLayout;
    headerTitle_ = new QLabel(tr("☰  空间子图 %1").arg(plotId_ + 1), this);
    headerTitle_->setObjectName(QStringLiteral("trajectoryDragHandle_%1").arg(plotId_));
    headerTitle_->setCursor(Qt::OpenHandCursor);
    headerTitle_->installEventFilter(this);
    QFont headerFont = headerTitle_->font();
    headerFont.setBold(true);
    headerTitle_->setFont(headerFont);
    dimensionLabel_ = new QLabel(dimension_ == TrajectoryDimension::TwoD ? tr("二维") : tr("三维"), this);
    autoRange_ = new QCheckBox(tr("自动范围"), this);
    autoRange_->setChecked(true);
    autoRange_->setVisible(dimension_ == TrajectoryDimension::TwoD);
    auto* removeButton = new QToolButton(this);
    removeButton->setObjectName(QStringLiteral("removeTrajectoryPlot_%1").arg(plotId_));
    removeButton->setText(tr("删除子图"));
    connect(removeButton, &QToolButton::clicked, this, [this]() { emit RemoveRequested(plotId_); });
    header->addWidget(headerTitle_);
    header->addWidget(dimensionLabel_);
    header->addStretch();
    header->addWidget(autoRange_);
    header->addWidget(removeButton);
    layout->addLayout(header);

    trackRows_ = new QWidget(this);
    auto* trackLayout = new QVBoxLayout(trackRows_);
    trackLayout->setContentsMargins(0, 0, 0, 0);
    trackLayout->setSpacing(3);
    layout->addWidget(trackRows_);

    if (dimension_ == TrajectoryDimension::TwoD) {
        auto* chart = new QChart;
        chart->setPlotAreaBackgroundVisible(true);
        chart->setMargins(QMargins(8, 8, 8, 8));
        xAxis_ = new QValueAxis(chart);
        yAxis_ = new QValueAxis(chart);
        xAxis_->setRange(-1.0, 1.0);
        yAxis_->setRange(-1.0, 1.0);
        xAxis_->setTickCount(5);
        yAxis_->setTickCount(5);
        chart->addAxis(xAxis_, Qt::AlignBottom);
        chart->addAxis(yAxis_, Qt::AlignLeft);
        chartView_ = new QChartView(chart, this);
        chartView_->setObjectName(plotId_ == 0 ? QStringLiteral("trajectoryChart")
                                               : QStringLiteral("trajectoryChart_%1").arg(plotId_));
        chartView_->setRenderHint(QPainter::Antialiasing, false);
        chartView_->setRubberBand(QChartView::RectangleRubberBand);
        chartView_->setAcceptDrops(true);
        chartView_->viewport()->setAcceptDrops(true);
        chartView_->viewport()->installEventFilter(this);
        layout->addWidget(chartView_, 1);
    } else {
        canvas3D_ = new Trajectory3DCanvas(this);
        canvas3D_->setObjectName(QStringLiteral("trajectory3DCanvas_%1").arg(plotId_));
        canvas3D_->setAcceptDrops(true);
        canvas3D_->installEventFilter(this);
        layout->addWidget(canvas3D_, 1);
    }

    status_ = new QLabel(tr("请加载格式并选择坐标数据列"), this);
    status_->setObjectName(QStringLiteral("trajectoryStatus_%1").arg(plotId_));
    layout->addWidget(status_);
    connect(autoRange_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) Render();
    });
    AddTrack();
    SetTheme(theme_);
}

qsizetype TrajectoryPlotWidget::HistoryPointCount(int trackIndex) const {
    if (trackIndex < 0 || trackIndex >= tracks_.size()) return 0;
    return dimension_ == TrajectoryDimension::TwoD
        ? tracks_[trackIndex].history2D.size() : tracks_[trackIndex].history3D.size();
}

void TrajectoryPlotWidget::ConfigureSignals(const pat::FormatDefinition& format) {
    format_ = format;
    for (auto& track : tracks_) PopulateTrackCombos(track);
    ResetHistory();
}

void TrajectoryPlotWidget::SetDataSource(const QVector<pat::Series>* series,
                                         pat::TrajectoryPairing pairing) {
    source_ = series;
    pairing_ = pairing;
}

void TrajectoryPlotWidget::SetDisplayTransforms(
    const QHash<int, pat::DisplayTransform>& transforms) {
    transforms_ = transforms;
    Render();
}

void TrajectoryPlotWidget::SetTheme(const AppTheme& theme) {
    theme_ = theme;
    setStyleSheet(QStringLiteral("QFrame#%1 { background: %2; border: 1px solid %3; }")
                      .arg(objectName(), theme_.contentBackground.name(), theme_.border.name()));
    if (chartView_ && chartView_->chart()) {
        auto* chart = chartView_->chart();
        chart->setBackgroundBrush(theme_.chartBackgroundEven);
        chart->setBackgroundPen(QPen(theme_.chartBorderEven, 1.5));
        chart->setPlotAreaBackgroundBrush(theme_.plotBackgroundEven);
        chart->legend()->setLabelColor(theme_.axisText);
        for (auto* axis : {xAxis_, yAxis_}) {
            if (!axis) continue;
            axis->setTitleBrush(QBrush(theme_.axisText));
            axis->setLabelsColor(theme_.axisText);
            axis->setLinePenColor(theme_.axisLine);
            axis->setGridLineColor(theme_.gridLine);
        }
    }
    if (canvas3D_) canvas3D_->SetTheme(theme_);
    Ensure2DSeries();
    Render();
}

int TrajectoryPlotWidget::BestSignal(bool longitude, bool latitude, bool altitude) const {
    int best = -1;
    int bestScore = -1;
    for (int i = 0; i < static_cast<int>(format_.signalFormats.size()); ++i) {
        const int score = CoordinateScore(format_.signalFormats[static_cast<size_t>(i)].name,
                                          longitude, latitude, altitude);
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best >= 0) return best;
    const int fallback = longitude ? 0 : (latitude ? 1 : 2);
    return fallback < static_cast<int>(format_.signalFormats.size()) ? fallback : -1;
}

void TrajectoryPlotWidget::AddTrack(const TrackSnapshot* snapshot) {
    TrackState track;
    track.id = nextTrackId_++;
    track.row = new QWidget(trackRows_);
    auto* rowLayout = new QHBoxLayout(track.row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    track.label = new QLabel(tr("轨迹 %1").arg(tracks_.size() + 1), track.row);
    track.xCombo = new QComboBox(track.row);
    track.yCombo = new QComboBox(track.row);
    if (dimension_ == TrajectoryDimension::ThreeD) track.zCombo = new QComboBox(track.row);
    const bool primary = plotId_ == 0 && track.id == 0;
    track.xCombo->setObjectName(primary ? QStringLiteral("trajectoryXCombo")
                                        : QStringLiteral("trajectoryXCombo_%1_%2").arg(plotId_).arg(track.id));
    track.yCombo->setObjectName(primary ? QStringLiteral("trajectoryYCombo")
                                        : QStringLiteral("trajectoryYCombo_%1_%2").arg(plotId_).arg(track.id));
    if (track.zCombo) {
        track.zCombo->setObjectName(primary ? QStringLiteral("trajectoryZCombo")
                                            : QStringLiteral("trajectoryZCombo_%1_%2").arg(plotId_).arg(track.id));
    }
    rowLayout->addWidget(track.label);
    rowLayout->addWidget(new QLabel(tr("X"), track.row));
    rowLayout->addWidget(track.xCombo, 1);
    rowLayout->addWidget(new QLabel(tr("Y"), track.row));
    rowLayout->addWidget(track.yCombo, 1);
    if (track.zCombo) {
        rowLayout->addWidget(new QLabel(tr("Z"), track.row));
        rowLayout->addWidget(track.zCombo, 1);
    }
    auto* removeTrackButton = new QToolButton(track.row);
    removeTrackButton->setText(tr("移除轨迹"));
    removeTrackButton->setObjectName(QStringLiteral("removeTrajectoryTrack_%1_%2").arg(plotId_).arg(track.id));
    rowLayout->addWidget(removeTrackButton);
    trackRows_->layout()->addWidget(track.row);
    const int trackId = track.id;
    tracks_.append(track);
    auto changed = [this, trackId]() {
        ResetTrackHistory(trackId);
        CaptureData();
        Render();
    };
    connect(track.xCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(track.yCombo, &QComboBox::currentIndexChanged, this, changed);
    if (track.zCombo) connect(track.zCombo, &QComboBox::currentIndexChanged, this, changed);
    connect(removeTrackButton, &QToolButton::clicked, this, [this, trackId]() { RemoveTrack(trackId); });
    PopulateTrackCombos(tracks_.last(), snapshot);
    if (snapshot) {
        tracks_.last().history2D = snapshot->history2D;
        tracks_.last().history3D = snapshot->history3D;
        tracks_.last().lastSampleKey = snapshot->lastSampleKey;
    }
    RebuildTrackRows();
    Ensure2DSeries();
}

void TrajectoryPlotWidget::RemoveTrack(int trackId) {
    if (tracks_.size() <= 1) return;
    for (int i = 0; i < tracks_.size(); ++i) {
        if (tracks_[i].id != trackId) continue;
        if (chartView_ && chartView_->chart()) {
            if (tracks_[i].line) chartView_->chart()->removeSeries(tracks_[i].line);
            if (tracks_[i].current) chartView_->chart()->removeSeries(tracks_[i].current);
        }
        delete tracks_[i].line;
        delete tracks_[i].current;
        delete tracks_[i].row;
        tracks_.removeAt(i);
        break;
    }
    RebuildTrackRows();
    Ensure2DSeries();
    Render();
}

void TrajectoryPlotWidget::PopulateTrackCombos(TrackState& track, const TrackSnapshot* snapshot) {
    QSignalBlocker blockX(track.xCombo);
    QSignalBlocker blockY(track.yCombo);
    std::unique_ptr<QSignalBlocker> blockZ;
    if (track.zCombo) blockZ = std::make_unique<QSignalBlocker>(track.zCombo);
    const auto fill = [this](QComboBox* combo) {
        if (!combo) return;
        combo->clear();
        combo->addItem(tr("请选择..."), -1);
        for (int i = 0; i < static_cast<int>(format_.signalFormats.size()); ++i) {
            const auto& signal = format_.signalFormats[static_cast<size_t>(i)];
            combo->addItem(signal.unit.isEmpty() ? signal.name
                : tr("%1 (%2)").arg(signal.name, signal.unit), i);
        }
    };
    fill(track.xCombo);
    fill(track.yCombo);
    fill(track.zCombo);
    const int x = snapshot ? snapshot->xSignal : BestSignal(true, false, false);
    const int y = snapshot ? snapshot->ySignal : BestSignal(false, true, false);
    const int z = snapshot ? snapshot->zSignal : BestSignal(false, false, true);
    track.xCombo->setCurrentIndex(std::max(0, track.xCombo->findData(x)));
    track.yCombo->setCurrentIndex(std::max(0, track.yCombo->findData(y)));
    if (track.zCombo) track.zCombo->setCurrentIndex(std::max(0, track.zCombo->findData(z)));
}

void TrajectoryPlotWidget::ResetTrackHistory(int trackId) {
    for (auto& track : tracks_) {
        if (track.id != trackId) continue;
        track.history2D.clear();
        track.history3D.clear();
        track.lastSampleKey = -1.0;
        return;
    }
}

void TrajectoryPlotWidget::RebuildTrackRows() {
    for (int i = 0; i < tracks_.size(); ++i) {
        tracks_[i].label->setText(tr("轨迹 %1").arg(i + 1));
        if (auto* button = tracks_[i].row->findChild<QToolButton*>()) {
            button->setEnabled(tracks_.size() > 1);
        }
    }
    if (chartView_ && chartView_->chart()) chartView_->chart()->legend()->setVisible(tracks_.size() > 1);
}

void TrajectoryPlotWidget::Ensure2DSeries() {
    if (!chartView_ || !chartView_->chart()) return;
    auto* chart = chartView_->chart();
    for (int i = 0; i < tracks_.size(); ++i) {
        auto& track = tracks_[i];
        const qsizetype colorCount = std::max<qsizetype>(1, theme_.seriesColors.size());
        const QColor color = theme_.seriesColors.value(i % colorCount, theme_.axisText);
        if (!track.line) {
            track.line = new QLineSeries(chart);
            chart->addSeries(track.line);
            track.line->attachAxis(xAxis_);
            track.line->attachAxis(yAxis_);
        }
        QPen pen(color, theme_.seriesLineWidth);
        track.line->setPen(pen);
        if (!track.current) {
            track.current = new QScatterSeries(chart);
            track.current->setMarkerSize(10.0);
            chart->addSeries(track.current);
            track.current->attachAxis(xAxis_);
            track.current->attachAxis(yAxis_);
            for (auto* marker : chart->legend()->markers(track.current)) marker->setVisible(false);
        }
        track.current->setColor(color);
        track.current->setBorderColor(theme_.guideOutline);
    }
    chart->legend()->setVisible(tracks_.size() > 1);
}

QString TrajectoryPlotWidget::TrackName(const TrackState& track) const {
    const QString x = track.xCombo ? track.xCombo->currentText() : QString();
    const QString y = track.yCombo ? track.yCombo->currentText() : QString();
    const QString z = track.zCombo ? track.zCombo->currentText() : QString();
    return dimension_ == TrajectoryDimension::TwoD
        ? tr("%1 / %2").arg(x, y) : tr("%1 / %2 / %3").arg(x, y, z);
}

void TrajectoryPlotWidget::CaptureData() {
    if (!source_) return;
    for (auto& track : tracks_) {
        const int x = track.xCombo ? track.xCombo->currentData().toInt() : -1;
        const int y = track.yCombo ? track.yCombo->currentData().toInt() : -1;
        const int z = track.zCombo ? track.zCombo->currentData().toInt() : -1;
        if (x < 0 || y < 0 || x >= source_->size() || y >= source_->size() || x == y) continue;
        if (dimension_ == TrajectoryDimension::TwoD) {
            const auto paired = pat::PairTrajectorySamplesWithKeys(source_->at(x), source_->at(y), pairing_);
            if (pairing_ == pat::TrajectoryPairing::RecordOrder) {
                track.history2D = paired;
                track.lastSampleKey = paired.isEmpty() ? -1.0 : paired.last().sampleKey;
            } else {
                for (const auto& point : paired) {
                    if (point.sampleKey > track.lastSampleKey) track.history2D.append(point);
                }
                if (!paired.isEmpty()) track.lastSampleKey = std::max(track.lastSampleKey,
                                                                      paired.last().sampleKey);
            }
        } else {
            if (z < 0 || z >= source_->size() || z == x || z == y) continue;
            const auto paired = pat::PairTrajectorySamples3D(source_->at(x), source_->at(y),
                                                             source_->at(z), pairing_);
            if (pairing_ == pat::TrajectoryPairing::RecordOrder) {
                track.history3D = paired;
                track.lastSampleKey = paired.isEmpty() ? -1.0 : paired.last().sampleKey;
            } else {
                for (const auto& point : paired) {
                    if (point.sampleKey > track.lastSampleKey) track.history3D.append(point);
                }
                if (!paired.isEmpty()) track.lastSampleKey = std::max(track.lastSampleKey,
                                                                      paired.last().sampleKey);
            }
        }
    }
}

void TrajectoryPlotWidget::Render() {
    if (dimension_ == TrajectoryDimension::TwoD) Render2D();
    else Render3D();
}

void TrajectoryPlotWidget::Render2D() {
    if (!chartView_ || !xAxis_ || !yAxis_) return;
    Ensure2DSeries();
    bool hasRange = false;
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    qsizetype historyCount = 0;
    qsizetype drawnCount = 0;
    for (int i = 0; i < tracks_.size(); ++i) {
        auto& track = tracks_[i];
        const int xSignal = track.xCombo->currentData().toInt();
        const int ySignal = track.yCombo->currentData().toInt();
        const auto xTransform = transforms_.value(xSignal, pat::DisplayTransform{});
        const auto yTransform = transforms_.value(ySignal, pat::DisplayTransform{});
        QVector<QPointF> displayed;
        displayed.reserve(track.history2D.size());
        for (const auto& sample : track.history2D) {
            const QPointF point(xTransform.Apply(sample.value.x()),
                                yTransform.Apply(sample.value.y()));
            displayed.append(point);
            if (!hasRange) {
                minX = maxX = point.x(); minY = maxY = point.y(); hasRange = true;
            } else {
                minX = std::min(minX, point.x()); maxX = std::max(maxX, point.x());
                minY = std::min(minY, point.y()); maxY = std::max(maxY, point.y());
            }
        }
        const auto drawn = pat::DecimateTrajectory(displayed, kMaxDrawPointsPerTrack);
        track.line->replace(drawn);
        track.line->setName(TrackName(track));
        track.current->setName(tr("当前位置: %1").arg(TrackName(track)));
        track.current->replace(displayed.isEmpty() ? QVector<QPointF>{}
                                                   : QVector<QPointF>{displayed.last()});
        historyCount += displayed.size();
        drawnCount += drawn.size();
    }
    if (tracks_.size() == 1) {
        xAxis_->setTitleText(tracks_.first().xCombo->currentText());
        yAxis_->setTitleText(tracks_.first().yCombo->currentText());
    } else {
        xAxis_->setTitleText(tr("X（各轨迹见图例）"));
        yAxis_->setTitleText(tr("Y（各轨迹见图例）"));
    }
    if (hasRange && autoRange_->isChecked()) {
        SetPaddedRange(xAxis_, minX, maxX);
        SetPaddedRange(yAxis_, minY, maxY);
    }
    status_->setText(hasRange
        ? tr("%1 条轨迹；历史点 %2；绘制点 %3；边界操作不清除历史")
              .arg(tracks_.size()).arg(historyCount).arg(drawnCount)
        : tr("等待所选 X/Y 数据列在同一采样点出现有效值"));
}

void TrajectoryPlotWidget::Render3D() {
    if (!canvas3D_) return;
    QVector<Trajectory3DCanvas::Track> rendered;
    qsizetype historyCount = 0;
    qsizetype drawnCount = 0;
    for (int i = 0; i < tracks_.size(); ++i) {
        const auto& track = tracks_[i];
        const int xSignal = track.xCombo->currentData().toInt();
        const int ySignal = track.yCombo->currentData().toInt();
        const int zSignal = track.zCombo ? track.zCombo->currentData().toInt() : -1;
        const auto xTransform = transforms_.value(xSignal, pat::DisplayTransform{});
        const auto yTransform = transforms_.value(ySignal, pat::DisplayTransform{});
        const auto zTransform = transforms_.value(zSignal, pat::DisplayTransform{});
        QVector<pat::TrajectoryPoint3D> displayed;
        displayed.reserve(track.history3D.size());
        for (const auto& point : track.history3D) {
            displayed.append({point.sampleKey, xTransform.Apply(point.x),
                              yTransform.Apply(point.y), zTransform.Apply(point.z)});
        }
        const auto drawn = pat::DecimateTrajectory3D(displayed, kMaxDrawPointsPerTrack);
        const qsizetype colorCount = std::max<qsizetype>(1, theme_.seriesColors.size());
        rendered.append({TrackName(track),
                         theme_.seriesColors.value(i % colorCount, theme_.axisText), drawn});
        historyCount += displayed.size();
        drawnCount += drawn.size();
    }
    canvas3D_->SetTracks(std::move(rendered));
    status_->setText(historyCount > 0
        ? tr("%1 条三维轨迹；历史点 %2；绘制点 %3；三轴分别归一化")
              .arg(tracks_.size()).arg(historyCount).arg(drawnCount)
        : tr("等待所选 X/Y/Z 数据列在同一采样点出现有效值"));
}

void TrajectoryPlotWidget::ResetHistory() {
    for (auto& track : tracks_) {
        track.history2D.clear();
        track.history3D.clear();
        track.lastSampleKey = -1.0;
    }
    Render();
}

QVector<TrajectoryPlotWidget::TrackSnapshot> TrajectoryPlotWidget::Snapshots() const {
    QVector<TrackSnapshot> result;
    result.reserve(tracks_.size());
    for (const auto& track : tracks_) {
        result.append({track.xCombo->currentData().toInt(), track.yCombo->currentData().toInt(),
                       track.zCombo ? track.zCombo->currentData().toInt() : -1,
                       track.history2D, track.history3D, track.lastSampleKey});
    }
    return result;
}

void TrajectoryPlotWidget::AppendSnapshots(const QVector<TrackSnapshot>& snapshots) {
    for (const auto& snapshot : snapshots) AddTrack(&snapshot);
    RebuildTrackRows();
    Render();
}

void TrajectoryPlotWidget::SetRemoveEnabled(bool enabled) {
    if (auto* button = findChild<QToolButton*>(QStringLiteral("removeTrajectoryPlot_%1").arg(plotId_))) {
        button->setEnabled(enabled);
    }
}

bool TrajectoryPlotWidget::DecodeDrag(const QMimeData* mime, int& sourceId,
                                      TrajectoryDimension& dimension) const {
    if (!mime || !mime->hasFormat(kTrajectoryPlotMime)) return false;
    QByteArray payload = mime->data(kTrajectoryPlotMime);
    QDataStream stream(&payload, QIODevice::ReadOnly);
    int dimensionValue = 0;
    stream >> sourceId >> dimensionValue;
    dimension = static_cast<TrajectoryDimension>(dimensionValue);
    return stream.status() == QDataStream::Ok;
}

bool TrajectoryPlotWidget::HandleDragEnter(QDragEnterEvent* event) {
    int sourceId = -1;
    TrajectoryDimension dimension;
    if (!DecodeDrag(event->mimeData(), sourceId, dimension) || sourceId == plotId_) return false;
    if (dimension != dimension_) {
        status_->setText(tr("二维与三维子图使用不同坐标系，不能合并"));
        event->ignore();
        return true;
    }
    event->setDropAction(Qt::MoveAction);
    event->accept();
    return true;
}

bool TrajectoryPlotWidget::HandleDragMove(QDragMoveEvent* event) {
    int sourceId = -1;
    TrajectoryDimension dimension;
    if (!DecodeDrag(event->mimeData(), sourceId, dimension) || sourceId == plotId_
        || dimension != dimension_) return false;
    event->setDropAction(Qt::MoveAction);
    event->accept();
    return true;
}

bool TrajectoryPlotWidget::HandleDrop(QDropEvent* event) {
    int sourceId = -1;
    TrajectoryDimension dimension;
    if (!DecodeDrag(event->mimeData(), sourceId, dimension) || sourceId == plotId_
        || dimension != dimension_) return false;
    emit MergeRequested(sourceId, plotId_);
    event->setDropAction(Qt::MoveAction);
    event->accept();
    return true;
}

bool TrajectoryPlotWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == headerTitle_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                dragStart_ = mouse->position().toPoint();
                headerTitle_->setCursor(Qt::ClosedHandCursor);
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if ((mouse->buttons() & Qt::LeftButton)
                && (mouse->position().toPoint() - dragStart_).manhattanLength()
                    >= QApplication::startDragDistance()) {
                auto* drag = new QDrag(this);
                auto* mime = new QMimeData;
                QByteArray payload;
                QDataStream stream(&payload, QIODevice::WriteOnly);
                stream << plotId_ << static_cast<int>(dimension_);
                mime->setData(kTrajectoryPlotMime, payload);
                drag->setMimeData(mime);
                headerTitle_->setCursor(Qt::OpenHandCursor);
                drag->exec(Qt::MoveAction);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            headerTitle_->setCursor(Qt::OpenHandCursor);
            return true;
        }
    }
    if (event->type() == QEvent::DragEnter) return HandleDragEnter(static_cast<QDragEnterEvent*>(event));
    if (event->type() == QEvent::DragMove) return HandleDragMove(static_cast<QDragMoveEvent*>(event));
    if (event->type() == QEvent::Drop) return HandleDrop(static_cast<QDropEvent*>(event));
    return QFrame::eventFilter(watched, event);
}

void TrajectoryPlotWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (!HandleDragEnter(event)) QFrame::dragEnterEvent(event);
}

void TrajectoryPlotWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (!HandleDragMove(event)) QFrame::dragMoveEvent(event);
}

void TrajectoryPlotWidget::dropEvent(QDropEvent* event) {
    if (!HandleDrop(event)) QFrame::dropEvent(event);
}

TrajectoryView::TrajectoryView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("trajectoryView"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* actions = new QHBoxLayout;
    auto* add2D = new QPushButton(tr("新增二维子图"), this);
    add2D->setObjectName(QStringLiteral("addTrajectory2D"));
    auto* add3D = new QPushButton(tr("新增三维子图"), this);
    add3D->setObjectName(QStringLiteral("addTrajectory3D"));
    auto* hint = new QLabel(tr("拖动子图标题到同维度子图可合并；轨迹历史独立保留"), this);
    actions->addWidget(add2D);
    actions->addWidget(add3D);
    actions->addWidget(hint);
    actions->addStretch();
    layout->addLayout(actions);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* container = new QWidget(scroll);
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    splitter_ = new QSplitter(Qt::Vertical, container);
    splitter_->setChildrenCollapsible(false);
    containerLayout->addWidget(splitter_);
    scroll->setWidget(container);
    layout->addWidget(scroll, 1);

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setSingleShot(true);
    refreshTimer_->setInterval(100);
    connect(refreshTimer_, &QTimer::timeout, this, &TrajectoryView::RefreshNow);
    connect(add2D, &QPushButton::clicked, this, [this]() { AddPlot(TrajectoryDimension::TwoD); });
    connect(add3D, &QPushButton::clicked, this, [this]() { AddPlot(TrajectoryDimension::ThreeD); });
    AddPlot(TrajectoryDimension::TwoD);
}

TrajectoryPlotWidget* TrajectoryView::AddPlot(TrajectoryDimension dimension) {
    auto* plot = new TrajectoryPlotWidget(nextPlotId_++, dimension, splitter_);
    plot->ConfigureSignals(format_);
    plot->SetDataSource(source_, pairing_);
    plot->SetDisplayTransforms(transforms_);
    plot->SetTheme(theme_);
    connect(plot, &TrajectoryPlotWidget::RemoveRequested, this, &TrajectoryView::RemovePlot);
    connect(plot, &TrajectoryPlotWidget::MergeRequested, this,
            [this](int sourceId, int targetId) {
                QString error;
                if (!MergePlots(sourceId, targetId, error) && !error.isEmpty()) {
                    QMessageBox::information(this, tr("无法合并空间子图"), error);
                }
            });
    splitter_->addWidget(plot);
    splitter_->setStretchFactor(splitter_->indexOf(plot), 1);
    plots_.append(plot);
    UpdateRemoveButtons();
    plot->CaptureData();
    plot->Render();
    return plot;
}

bool TrajectoryView::MergePlots(int sourcePlotId, int targetPlotId, QString& errorMessage) {
    errorMessage.clear();
    if (sourcePlotId == targetPlotId) return false;
    TrajectoryPlotWidget* source = nullptr;
    TrajectoryPlotWidget* target = nullptr;
    for (auto* plot : plots_) {
        if (plot->PlotId() == sourcePlotId) source = plot;
        if (plot->PlotId() == targetPlotId) target = plot;
    }
    if (!source || !target) {
        errorMessage = tr("空间子图不存在或已被删除");
        return false;
    }
    if (source->Dimension() != target->Dimension()) {
        errorMessage = tr("二维与三维子图使用不同坐标系，不能合并");
        return false;
    }
    target->AppendSnapshots(source->Snapshots());
    RemovePlot(sourcePlotId);
    target->CaptureData();
    target->Render();
    return true;
}

void TrajectoryView::RemovePlot(int plotId) {
    if (plots_.size() <= 1) return;
    for (int i = 0; i < plots_.size(); ++i) {
        if (plots_[i]->PlotId() != plotId) continue;
        auto* plot = plots_.takeAt(i);
        plot->setParent(nullptr);
        plot->deleteLater();
        break;
    }
    UpdateRemoveButtons();
}

void TrajectoryView::ClearPlots() {
    const auto old = plots_;
    plots_.clear();
    for (auto* plot : old) {
        plot->setParent(nullptr);
        delete plot;
    }
}

void TrajectoryView::UpdateRemoveButtons() {
    for (auto* plot : plots_) plot->SetRemoveEnabled(plots_.size() > 1);
}

void TrajectoryView::SetTheme(const AppTheme& theme) {
    theme_ = theme;
    for (auto* plot : plots_) plot->SetTheme(theme_);
}

void TrajectoryView::ConfigureSignals(const pat::FormatDefinition& format) {
    format_ = format;
    source_ = nullptr;
    refreshTimer_->stop();
    ClearPlots();
    nextPlotId_ = 0;
    AddPlot(TrajectoryDimension::TwoD);
    dirty_ = true;
}

void TrajectoryView::SetDataSource(const QVector<pat::Series>* series,
                                   pat::TrajectoryPairing pairing) {
    source_ = series;
    pairing_ = pairing;
    for (auto* plot : plots_) plot->SetDataSource(source_, pairing_);
    RequestRefresh();
}

void TrajectoryView::SetDisplayTransforms(
    const QHash<int, pat::DisplayTransform>& transforms) {
    transforms_ = transforms;
    for (auto* plot : plots_) plot->SetDisplayTransforms(transforms_);
    dirty_ = true;
}

void TrajectoryView::SetPaused(bool paused) {
    paused_ = paused;
    if (paused_) refreshTimer_->stop();
    else if (dirty_) QTimer::singleShot(0, this, &TrajectoryView::RefreshNow);
}

void TrajectoryView::CaptureAllPlots() {
    for (auto* plot : plots_) plot->CaptureData();
}

void TrajectoryView::RequestRefresh() {
    dirty_ = true;
    CaptureAllPlots();
    if (!paused_ && isVisible() && !refreshTimer_->isActive()) refreshTimer_->start();
}

void TrajectoryView::RefreshNow() {
    CaptureAllPlots();
    if (paused_ || !isVisible()) {
        dirty_ = true;
        return;
    }
    refreshTimer_->stop();
    for (auto* plot : plots_) plot->Render();
    dirty_ = false;
}

void TrajectoryView::ResetSession() {
    for (auto* plot : plots_) plot->ResetHistory();
    dirty_ = true;
}

void TrajectoryView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (dirty_ && !paused_) QTimer::singleShot(0, this, &TrajectoryView::RefreshNow);
}
