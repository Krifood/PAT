#include "ui/main_window.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QColor>
#include <QPen>
#include <QSizePolicy>
#include <algorithm>
#include <cmath>

#ifdef PAT_ENABLE_QT_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#endif

namespace {

QString fileLeaf(const QString& path) {
  QFileInfo info(path);
  return info.fileName();
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setupUi();
}

void MainWindow::setupUi() {
  setWindowTitle(tr("param_analysis_tool"));

  auto* openFormatAction = new QAction(tr("打开格式..."), this);
  auto* openDataAction = new QAction(tr("打开数据..."), this);
  auto* exitAction = new QAction(tr("退出"), this);

  connect(openFormatAction, &QAction::triggered, this, &MainWindow::openFormatFile);
  connect(openDataAction, &QAction::triggered, this, &MainWindow::openDataFile);
  connect(exitAction, &QAction::triggered, this, &MainWindow::close);

  auto* fileMenu = menuBar()->addMenu(tr("文件"));
  fileMenu->addAction(openFormatAction);
  fileMenu->addAction(openDataAction);
  fileMenu->addSeparator();
  fileMenu->addAction(exitAction);

  auto* central = new QWidget(this);
  auto* layout = new QHBoxLayout(central);

  // 左侧：信号选择列表
  auto* leftLayout = new QVBoxLayout();
  auto* listLabel = new QLabel(tr("信号选择"), this);
  signalList_ = new QListWidget(this);
  signalList_->setSelectionMode(QAbstractItemView::NoSelection);
  connect(signalList_, &QListWidget::itemChanged, this, &MainWindow::updateChart);
  leftLayout->addWidget(listLabel);
  leftLayout->addWidget(signalList_, /*stretch=*/1);
  layout->addLayout(leftLayout, /*stretch=*/1);

#ifdef PAT_ENABLE_QT_CHARTS
  auto* rightLayout = new QVBoxLayout();
  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  chartsContainer_ = new QWidget(scroll);
  chartsLayout_ = new QVBoxLayout(chartsContainer_);
  chartsLayout_->setContentsMargins(0, 0, 0, 0);
  chartsLayout_->setSpacing(12);
  chartsContainer_->setLayout(chartsLayout_);
  scroll->setWidget(chartsContainer_);
  rightLayout->addWidget(scroll, /*stretch=*/1);
#else
  auto* placeholder = new QLabel(tr("Qt Charts 未启用，无法显示曲线"), this);
  placeholder->setAlignment(Qt::AlignCenter);
  auto* rightLayout = new QVBoxLayout();
  rightLayout->addWidget(placeholder, /*stretch=*/1);
#endif

  statusLabel_ = new QLabel(tr("请先加载格式文件"), this);
  rightLayout->addWidget(statusLabel_);
  layout->addLayout(rightLayout, /*stretch=*/3);

  setCentralWidget(central);
  statusBar()->showMessage(tr("就绪"));
}

void MainWindow::openFormatFile() {
  const QString path = QFileDialog::getOpenFileName(this, tr("选择格式文件"), QString(), tr("JSON (*.json);;所有文件 (*)"));
  if (path.isEmpty()) return;

  pat::FormatDefinition fmt;
  QString error;
  if (!pat::LoadFormatFromJson(path, fmt, error)) {
    QMessageBox::warning(this, tr("格式加载失败"), error);
    return;
  }

  format_ = std::move(fmt);
  hasFormat_ = true;
  loadedFormatPath_ = path;
  series_.clear();
  if (signalList_) {
    signalList_->clear();
    for (const auto& sig : format_.signalFormats) {
      auto* item = new QListWidgetItem(sig.name, signalList_);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Checked);
    }
  }

  updateStatus(tr("格式已加载：%1，信号数：%2").arg(fileLeaf(path)).arg(format_.signalFormats.size()));
}

void MainWindow::openDataFile() {
  if (!hasFormat_) {
    QMessageBox::information(this, tr("提示"), tr("请先加载格式文件"));
    return;
  }

  const QString path = QFileDialog::getOpenFileName(this, tr("选择数据文件"), QString(), tr("数据文件 (*.bin *.dat);;所有文件 (*)"));
  if (path.isEmpty()) return;

  pat::RecordParser parser(format_);
  QVector<pat::Series> parsed;
  QString error;
  if (!parser.parseFile(path, parsed, error)) {
    QMessageBox::warning(this, tr("解析失败"), error);
    return;
  }

  series_ = std::move(parsed);
  loadedDataPath_ = path;

  updateChart();

  const int recordCount = series_.isEmpty() ? 0 : series_.first().samples.size();
  updateStatus(tr("解析完成：%1，记录数 %2").arg(fileLeaf(path)).arg(recordCount));
}

void MainWindow::updateChart() {
#ifdef PAT_ENABLE_QT_CHARTS
  if (!chartsLayout_) return;
  clearCharts();
  hideCrosshairs();
  sharedCursorIndex_ = -1;

  QVector<int> selected;
  if (signalList_) {
    for (int i = 0; i < signalList_->count(); ++i) {
      if (signalList_->item(i)->checkState() == Qt::Checked) selected.append(i);
    }
  }
  if (selected.isEmpty()) {
    for (int i = 0; i < series_.size(); ++i) selected.append(i);
  }

  // 计算全局时间范围（供所有子图共享 X 轴）
  double globalMinX = 0.0;
  double globalMaxX = 0.0;
  bool hasX = false;
  for (int idx : selected) {
    if (idx < 0 || idx >= series_.size()) continue;
    const auto& s = series_[idx];
    for (const auto& pt : s.samples) {
      if (!hasX) {
        globalMinX = globalMaxX = pt.x();
        hasX = true;
      } else {
        globalMinX = std::min(globalMinX, pt.x());
        globalMaxX = std::max(globalMaxX, pt.x());
      }
    }
  }
  if (!hasX) {
    globalMinX = 0.0;
    globalMaxX = 1.0;
  }
  globalMinX_ = globalMinX;
  globalMaxX_ = globalMaxX;
  sharedMinX_ = globalMinX_;
  sharedMaxX_ = globalMaxX_;
  hasSharedRange_ = true;

  // 计算全局幅值范围（所有已加载信号，保持比例稳定）
  double globalMinY = 0.0;
  double globalMaxY = 0.0;
  bool hasValue = false;
  for (const auto& s : series_) {
    for (const auto& pt : s.samples) {
      if (!hasValue) {
        globalMinY = globalMaxY = pt.y();
        hasValue = true;
      } else {
        globalMinY = std::min(globalMinY, pt.y());
        globalMaxY = std::max(globalMaxY, pt.y());
      }
    }
  }
  if (!hasValue) {
    globalMinY = -1.0;
    globalMaxY = 1.0;
  }
  if (qFuzzyCompare(globalMinY, globalMaxY)) {
    const double delta = qAbs(globalMinY) > 1.0 ? qAbs(globalMinY) * 0.1 : 1.0;
    globalMinY -= delta;
    globalMaxY += delta;
  }

  for (int idx : selected) {
    if (idx < 0 || idx >= series_.size()) continue;
    const auto& s = series_[idx];
    auto* chart = new QChart();
    chart->setTitle(s.unit.isEmpty() ? s.name : QStringLiteral("%1 (%2)").arg(s.name, s.unit));
    chart->setBackgroundBrush(QColor(24, 26, 30));
    chart->setPlotAreaBackgroundBrush(QColor(24, 26, 30));
    chart->setPlotAreaBackgroundVisible(true);
    chart->setTitleBrush(QBrush(Qt::white));

    auto* line = new QLineSeries(chart);
    for (const auto& pt : s.samples) {
      line->append(pt);
    }
    chart->addSeries(line);

    auto* axisX = new QValueAxis(chart);
    axisX->setTitleText(tr("时间索引"));
    axisX->setRange(sharedMinX_, sharedMaxX_);
    axisX->setLabelsColor(Qt::white);
    axisX->setTitleBrush(QBrush(Qt::white));
    axisX->setLinePen(QPen(QColor(200, 200, 200)));
    axisX->setGridLinePen(QPen(QColor(80, 80, 80)));
    chart->addAxis(axisX, Qt::AlignBottom);
    line->attachAxis(axisX);

    auto* axisY = new QValueAxis(chart);
    axisY->setRange(globalMinY, globalMaxY);
    axisY->setLabelsColor(Qt::white);
    axisY->setTitleBrush(QBrush(Qt::white));
    axisY->setLinePen(QPen(QColor(200, 200, 200)));
    axisY->setGridLinePen(QPen(QColor(80, 80, 80)));
    chart->addAxis(axisY, Qt::AlignLeft);
    line->attachAxis(axisY);

    auto* view = new QChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    view->setMouseTracking(true);
    view->installEventFilter(this);
    if (view->viewport()) {
      view->viewport()->setMouseTracking(true);
      view->viewport()->installEventFilter(this);
    }
    view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    view->setMinimumHeight(320);

    auto* cross = new QGraphicsLineItem();
    QPen pen(Qt::red);
    pen.setStyle(Qt::DashLine);
    cross->setPen(pen);
    cross->setZValue(1000);
    cross->setVisible(false);
    chart->scene()->addItem(cross);

    auto* label = new QGraphicsSimpleTextItem(chart);
    label->setBrush(QColor("#FFD447"));
    label->setVisible(false);

    chartsLayout_->addWidget(view);
    ChartItem item;
    item.view = view;
    item.series = line;
    item.crosshair = cross;
    item.valueLabel = label;
    item.seriesIndex = idx;
    charts_.append(item);
  }

  chartsLayout_->addStretch();
  applySharedXRange();
#endif
}

void MainWindow::updateStatus(const QString& text) {
  if (statusLabel_) statusLabel_->setText(text);
  statusBar()->showMessage(text, 5000);
}

void MainWindow::clearCharts() {
#ifdef PAT_ENABLE_QT_CHARTS
  charts_.clear();
  if (!chartsLayout_) return;
  QLayoutItem* child = nullptr;
  while ((child = chartsLayout_->takeAt(0)) != nullptr) {
    if (child->widget()) {
      child->widget()->deleteLater();
    }
    delete child;
  }
#endif
}

MainWindow::ChartItem* MainWindow::findChart(QChartView* view) {
#ifdef PAT_ENABLE_QT_CHARTS
  for (auto& item : charts_) {
    if (item.view == view) return &item;
  }
#endif
  return nullptr;
}

void MainWindow::applySharedXRange() {
#ifdef PAT_ENABLE_QT_CHARTS
  if (!hasSharedRange_) return;
  for (auto& item : charts_) {
    if (!item.view || !item.series) continue;
    auto* chart = item.view->chart();
    if (!chart) continue;
    auto* axisX = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal, item.series).value(0));
    if (axisX) axisX->setRange(sharedMinX_, sharedMaxX_);
  }
#endif
}

void MainWindow::resetSharedXRange() {
#ifdef PAT_ENABLE_QT_CHARTS
  if (!hasSharedRange_) return;
  sharedMinX_ = globalMinX_;
  sharedMaxX_ = globalMaxX_;
  applySharedXRange();
#endif
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
#ifdef PAT_ENABLE_QT_CHARTS
  QChartView* targetView = qobject_cast<QChartView*>(obj);
  if (!targetView) {
    if (auto* widget = qobject_cast<QWidget*>(obj)) {
      targetView = qobject_cast<QChartView*>(widget->parent());
    }
  }
  if (targetView) {
    QPoint viewPos;
    if (event->type() == QEvent::MouseMove) {
      if (auto* item = findChart(targetView)) {
        viewPos = mapToView(targetView, obj, static_cast<QMouseEvent*>(event)->position());
        handleMouseMove(*item, static_cast<QMouseEvent*>(event), viewPos);
      }
    } else if (event->type() == QEvent::Wheel) {
      if (auto* item = findChart(targetView)) {
        viewPos = mapToView(targetView, obj, static_cast<QWheelEvent*>(event)->position());
        handleWheelEvent(*item, static_cast<QWheelEvent*>(event), viewPos);
        return true;
      }
    } else if (event->type() == QEvent::MouseButtonPress) {
      if (auto* item = findChart(targetView)) {
        viewPos = mapToView(targetView, obj, static_cast<QMouseEvent*>(event)->position());
        handleMousePress(*item, static_cast<QMouseEvent*>(event), viewPos);
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      if (auto* item = findChart(targetView)) {
        handleMouseRelease(*item, static_cast<QMouseEvent*>(event));
      }
    } else if (event->type() == QEvent::Leave) {
      hideCrosshairs();
      sharedCursorIndex_ = -1;
    }
  }
#endif
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::handleMouseMove(ChartItem& item, QMouseEvent* event, const QPoint& viewPos) {
#ifdef PAT_ENABLE_QT_CHARTS
  if (!item.view || !item.series) return;
  auto* chart = item.view->chart();
  if (!chart) return;
  const int samplesCount = static_cast<int>(series_.value(item.seriesIndex).samples.size());
  if (samplesCount == 0) return;

  if (item.panning && hasSharedRange_) {
    const QRectF plotArea = chart->plotArea();
    const double span = sharedMaxX_ - sharedMinX_;
    const double globalSpan = globalMaxX_ - globalMinX_;
    if (!plotArea.isEmpty() && span > 0.0 && globalSpan > 0.0) {
      const double dx = static_cast<double>(viewPos.x() - item.lastPanPos.x());
      const double deltaValue = dx / plotArea.width() * span;
      double newMin = sharedMinX_ - deltaValue;
      double newMax = sharedMaxX_ - deltaValue;
      if (newMin < globalMinX_) {
        newMin = globalMinX_;
        newMax = globalMinX_ + span;
      }
      if (newMax > globalMaxX_) {
        newMax = globalMaxX_;
        newMin = globalMaxX_ - span;
      }
      sharedMinX_ = newMin;
      sharedMaxX_ = newMax;
      applySharedXRange();
      item.lastPanPos = viewPos;
    }
  }

  // 统一使用 viewport 坐标映射，保证所有子图的竖线同步
  const QPointF plotPoint = chart->mapToValue(item.view->mapToScene(viewPos), item.series);
  int idx = static_cast<int>(std::round(plotPoint.x()));
  idx = std::max(0, std::min(idx, samplesCount - 1));
  updateSharedCursor(idx);
#endif
}

void MainWindow::handleWheelEvent(ChartItem& item, QWheelEvent* event, const QPoint& viewPos) {
#ifdef PAT_ENABLE_QT_CHARTS
  if (!item.view || !item.series) return;
  auto* chart = item.view->chart();
  if (!chart || !hasSharedRange_) return;

  const auto& data = series_.value(item.seriesIndex);
  if (data.samples.isEmpty()) return;

  const QPointF valuePoint = chart->mapToValue(item.view->mapToScene(viewPos), item.series);
  const double focusX = valuePoint.x();

  const double span = sharedMaxX_ - sharedMinX_;
  const double globalSpan = globalMaxX_ - globalMinX_;
  if (span <= 0.0 || globalSpan <= 0.0) return;

  const int deltaY = event->angleDelta().y();
  const double factor = deltaY > 0 ? 0.8 : 1.25;
  const double minSpan = 1e-6;
  const double desiredSpan = std::max(span * factor, minSpan);
  const double targetSpan = std::min(desiredSpan, globalSpan);

  double left = focusX - (focusX - sharedMinX_) * (targetSpan / span);
  double right = left + targetSpan;

  if (left < globalMinX_) {
    left = globalMinX_;
    right = left + targetSpan;
  }
  if (right > globalMaxX_) {
    right = globalMaxX_;
    left = right - targetSpan;
  }

  sharedMinX_ = left;
  sharedMaxX_ = right;
  applySharedXRange();
  event->accept();
#endif
}

void MainWindow::handleMousePress(ChartItem& item, QMouseEvent* event, const QPoint& viewPos) {
#ifdef PAT_ENABLE_QT_CHARTS
  if (event->button() == Qt::LeftButton) {
    item.panning = true;
    item.lastPanPos = viewPos;
    event->accept();
  }
#endif
}

void MainWindow::handleMouseRelease(ChartItem& item, QMouseEvent* event) {
#ifdef PAT_ENABLE_QT_CHARTS
  if (event->button() == Qt::LeftButton) {
    item.panning = false;
    event->accept();
  }
#endif
}

QPoint MainWindow::mapToView(QChartView* view, QObject* eventObj, const QPointF& pos) const {
#ifdef PAT_ENABLE_QT_CHARTS
  if (!view) return {};
  if (auto* widget = qobject_cast<QWidget*>(eventObj)) {
    const QPoint globalPos = widget->mapToGlobal(pos.toPoint());
    return view->mapFromGlobal(globalPos);
  }
#endif
  return pos.toPoint();
}

void MainWindow::updateSharedCursor(int sampleIndex) {
#ifdef PAT_ENABLE_QT_CHARTS
  sharedCursorIndex_ = sampleIndex;
  for (auto& item : charts_) {
    if (!item.view || !item.series) continue;
    auto* chart = item.view->chart();
    if (!chart) continue;
    const auto& seriesData = series_.value(item.seriesIndex);
    if (seriesData.samples.isEmpty()) continue;
    const int lastIdx = static_cast<int>(seriesData.samples.size()) - 1;
    int idx = std::max(0, std::min(sampleIndex, lastIdx));
    const auto& sample = seriesData.samples.at(idx);

    auto* axisY = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical, item.series).value(0));
    auto* axisX = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal, item.series).value(0));
    if (!axisY || !axisX) continue;

    const double yMin = axisY->min();
    const double yMax = axisY->max();

    const QPointF top = chart->mapToPosition(QPointF(sample.x(), yMax), item.series);
    const QPointF bottom = chart->mapToPosition(QPointF(sample.x(), yMin), item.series);
    if (item.crosshair) {
      item.crosshair->setLine(QLineF(chart->mapToScene(top), chart->mapToScene(bottom)));
      item.crosshair->setVisible(true);
    }
    if (item.valueLabel) {
      item.valueLabel->setText(QStringLiteral("t=%1  val=%2").arg(sample.x()).arg(sample.y()));
      QPointF labelPos = chart->mapToScene(top) + QPointF(6, -16);
      item.valueLabel->setPos(labelPos);
      item.valueLabel->setVisible(true);
    }
  }
#endif
}

void MainWindow::hideCrosshairs() {
#ifdef PAT_ENABLE_QT_CHARTS
  for (auto& item : charts_) {
    if (item.crosshair) item.crosshair->setVisible(false);
    if (item.valueLabel) item.valueLabel->setVisible(false);
  }
#endif
}
