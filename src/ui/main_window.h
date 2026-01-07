#pragma once

#include <QMainWindow>

#include "core/format_definition.h"
#include "core/record_parser.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QVBoxLayout;
class QMouseEvent;
class QWheelEvent;
class QGraphicsLineItem;
class QGraphicsSimpleTextItem;
class QPoint;
QT_END_NAMESPACE

#ifdef PAT_ENABLE_QT_CHARTS
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#endif

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private slots:
 void openFormatFile();
  void openDataFile();

 private:
  void setupUi();
  void updateChart();
  void updateStatus(const QString& text);
  void clearCharts();
  bool eventFilter(QObject* obj, QEvent* event) override;
  struct ChartItem;
  ChartItem* findChart(QChartView* view);
  void applySharedXRange();
  void resetSharedXRange();
  void handleMouseMove(ChartItem& item, QMouseEvent* event, const QPoint& viewPos);
  void handleWheelEvent(ChartItem& item, QWheelEvent* event, const QPoint& viewPos);
  void handleMousePress(ChartItem& item, QMouseEvent* event, const QPoint& viewPos);
  void handleMouseRelease(ChartItem& item, QMouseEvent* event);
  QPoint mapToView(QChartView* view, QObject* eventObj, const QPointF& pos) const;
  void updateSharedCursor(int sampleIndex);
  void hideCrosshairs();

  struct ChartItem {
    QChartView* view = nullptr;
    QLineSeries* series = nullptr;
    QGraphicsLineItem* crosshair = nullptr;
    QGraphicsSimpleTextItem* valueLabel = nullptr;
    int seriesIndex = -1;
    bool panning = false;
    QPoint lastPanPos;
  };

  pat::FormatDefinition format_;
  bool hasFormat_ = false;
  QString loadedFormatPath_;
  QString loadedDataPath_;
  QVector<pat::Series> series_;

  QListWidget* signalList_ = nullptr;
#ifdef PAT_ENABLE_QT_CHARTS
  QWidget* chartsContainer_ = nullptr;
  QVBoxLayout* chartsLayout_ = nullptr;
  QVector<ChartItem> charts_;
  int sharedCursorIndex_ = -1;
  double globalMinX_ = 0.0;
  double globalMaxX_ = 0.0;
  double sharedMinX_ = 0.0;
  double sharedMaxX_ = 0.0;
  bool hasSharedRange_ = false;
#endif
  QLabel* statusLabel_ = nullptr;
};
