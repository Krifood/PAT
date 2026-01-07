#include "ui/MainWindow.h"

#include <QAction>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QTreeWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QSplitter>
#include <QWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QRubberBand>
#include <QPen>
#include <QColor>
#include <QSizePolicy>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <functional>

#include "ui/SignalTreeWidget.h"

#ifdef PAT_ENABLE_QT_CHARTS
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#endif

namespace {

QString FileLeaf(const QString& path) {
    QFileInfo info(path);
    return info.fileName();
}

QStringList SplitGroupPath(const QString& groupPath) {
    if (groupPath.trimmed().isEmpty()) return {};
    return groupPath.split('/', Qt::SkipEmptyParts);
}

const QColor kChartBackground(20, 20, 22);
const QColor kPlotBackground(28, 28, 30);
const QColor kAxisTextColor(210, 210, 210);
const QColor kAxisLineColor(140, 140, 140);
const QColor kGridLineColor(60, 60, 60);
const QColor kCursorLineColor(255, 48, 48);
const QColor kCursorValueColor(255, 215, 0);
const QColor kAssistLineColor(90, 90, 90);

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

#ifdef PAT_ENABLE_QT_CHARTS
QPoint ViewportPoint(QChartView* view, const QPointF& globalPos) {
    if (!view || !view->viewport()) return globalPos.toPoint();
    return view->viewport()->mapFromGlobal(globalPos.toPoint());
}
#endif

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    SetupUi();
}

void MainWindow::SetupUi() {
    setWindowTitle(tr("PAT 飞参解析工具"));

    auto* openFormatAction = new QAction(tr("打开格式..."), this);
    auto* openDataAction = new QAction(tr("打开数据..."), this);
    auto* exitAction = new QAction(tr("退出"), this);

    connect(openFormatAction, &QAction::triggered, this, &MainWindow::OpenFormatFile);
    connect(openDataAction, &QAction::triggered, this, &MainWindow::OpenDataFile);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);

    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(openFormatAction);
    fileMenu->addAction(openDataAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);

    auto* leftLayout = new QVBoxLayout();
    auto* listLabel = new QLabel(tr("信号选择"), this);
    signalTree_ = new SignalTreeWidget(this);
    signalTree_->setHeaderHidden(true);
    signalTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    signalTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(signalTree_, &QTreeWidget::itemChanged, this, &MainWindow::UpdateCharts);
    connect(signalTree_, &QWidget::customContextMenuRequested, this, &MainWindow::ShowSignalTreeMenu);
    connect(signalTree_, &SignalTreeWidget::MergeRequested, this, &MainWindow::MergeSignals);
    leftLayout->addWidget(listLabel);
    leftLayout->addWidget(signalTree_, /*stretch=*/1);
    layout->addLayout(leftLayout, /*stretch=*/1);

#ifdef PAT_ENABLE_QT_CHARTS
    auto* rightLayout = new QVBoxLayout();
    chartsScrollArea_ = new QScrollArea(this);
    chartsScrollArea_->setWidgetResizable(true);
    chartsContainer_ = new QWidget(chartsScrollArea_);
    auto* containerLayout = new QVBoxLayout(chartsContainer_);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    chartsSplitter_ = new QSplitter(Qt::Vertical, chartsContainer_);
    chartsSplitter_->setChildrenCollapsible(false);
    containerLayout->addWidget(chartsSplitter_);
    chartsContainer_->setLayout(containerLayout);
    chartsScrollArea_->setWidget(chartsContainer_);
    rightLayout->addWidget(chartsScrollArea_, /*stretch=*/1);

    connect(chartsSplitter_, &QSplitter::splitterMoved, this, [this](int, int index) {
        if (!chartsSplitter_) return;
        const QList<int> sizes = chartsSplitter_->sizes();
        if (sizes.isEmpty()) return;
        const int widgetIndex = std::clamp(index, 0, sizes.size() - 1);
        chartHeight_ = sizes.value(widgetIndex);
        UpdateChartHeights();
    });
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

void MainWindow::OpenFormatFile() {
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
    mergedGroups_.clear();
    displayGroups_.clear();
    BuildSignalTree();

    UpdateStatus(tr("格式已加载：%1，信号数：%2").arg(FileLeaf(path)).arg(static_cast<int>(format_.signalFormats.size())));
}

void MainWindow::OpenDataFile() {
    if (!hasFormat_) {
        QMessageBox::information(this, tr("提示"), tr("请先加载格式文件"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this, tr("选择数据文件"), QString(), tr("数据文件 (*.bin *.dat);;所有文件 (*)"));
    if (path.isEmpty()) return;

    pat::RecordParser parser(format_);
    QVector<pat::Series> parsed;
    QString error;
    if (!parser.ParseFile(path, parsed, error)) {
        QMessageBox::warning(this, tr("解析失败"), error);
        return;
    }

    series_ = std::move(parsed);
    loadedDataPath_ = path;

    UpdateGlobalRanges();
    currentMinX_ = 0.0;
    currentMaxX_ = globalMaxX_;
    hasCurrentXRange_ = true;
    UpdateCharts();

    const int recordCount = series_.isEmpty() ? 0 : static_cast<int>(series_.first().samples.size());
    UpdateStatus(tr("解析完成：%1，记录数 %2").arg(FileLeaf(path)).arg(recordCount));
}

void MainWindow::BuildSignalTree() {
    if (!signalTree_) return;
    QSignalBlocker blocker(signalTree_);
    signalTree_->clear();

    QMap<QString, QTreeWidgetItem*> groupItems;
    auto* root = signalTree_->invisibleRootItem();

    for (int i = 0; i < static_cast<int>(format_.signalFormats.size()); ++i) {
        const auto& sig = format_.signalFormats[i];
        QTreeWidgetItem* parent = root;
        const QStringList groups = SplitGroupPath(sig.groupPath);
        QString currentPath;
        for (const auto& groupName : groups) {
            if (!currentPath.isEmpty()) currentPath += "/";
            currentPath += groupName;
            if (!groupItems.contains(currentPath)) {
                auto* groupItem = new QTreeWidgetItem(parent, {groupName});
                groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserTristate | Qt::ItemIsUserCheckable);
                groupItem->setCheckState(0, Qt::Checked);
                groupItem->setData(0, SignalTreeWidget::kItemTypeRole, SignalTreeWidget::kItemTypeGroup);
                groupItems.insert(currentPath, groupItem);
                parent = groupItem;
            } else {
                parent = groupItems.value(currentPath);
            }
        }

        auto* signalItem = new QTreeWidgetItem(parent, {sig.name});
        signalItem->setFlags(signalItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        signalItem->setCheckState(0, Qt::Checked);
        signalItem->setData(0, SignalTreeWidget::kItemTypeRole, SignalTreeWidget::kItemTypeSignal);
        signalItem->setData(0, SignalTreeWidget::kSignalIndexRole, i);
    }

    signalTree_->expandAll();
}

QVector<int> MainWindow::CollectCheckedSignalIndices() const {
    QVector<int> indices;
    if (!signalTree_) return indices;

    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, SignalTreeWidget::kItemTypeRole).toInt();
        if (type == SignalTreeWidget::kItemTypeSignal && item->checkState(0) == Qt::Checked) {
            const int idx = item->data(0, SignalTreeWidget::kSignalIndexRole).toInt();
            if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };

    auto* root = signalTree_->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) {
        visit(root->child(i));
    }
    return indices;
}

QVector<int> MainWindow::CollectSelectedSignalIndices() const {
    QVector<int> indices;
    if (!signalTree_) return indices;
    for (auto* item : signalTree_->selectedItems()) {
        if (item->data(0, SignalTreeWidget::kItemTypeRole).toInt() != SignalTreeWidget::kItemTypeSignal) continue;
        const int idx = item->data(0, SignalTreeWidget::kSignalIndexRole).toInt();
        if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
    }
    return indices;
}

bool MainWindow::AreUnitsCompatible(const QVector<int>& indices, QString& outUnit) const {
    outUnit.clear();
    for (int idx : indices) {
        if (idx < 0 || idx >= static_cast<int>(format_.signalFormats.size())) return false;
        const QString unit = format_.signalFormats[idx].unit;
        if (outUnit.isEmpty()) {
            outUnit = unit;
        } else if (unit != outUnit) {
            return false;
        }
    }
    return !outUnit.isEmpty() || !indices.isEmpty();
}

bool MainWindow::ComputeGroupRange(const QVector<int>& indices, double& outMinY, double& outMaxY) const {
    bool hasRange = false;
    outMinY = -1.0;
    outMaxY = 1.0;

    for (int idx : indices) {
        if (idx < 0 || idx >= static_cast<int>(series_.size())) continue;
        const auto& series = series_[idx];
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
        const double delta = qAbs(outMinY) > 1.0 ? qAbs(outMinY) * 0.1 : 1.0;
        outMinY -= delta;
        outMaxY += delta;
    }
    return true;
}

void MainWindow::MergeSignals(const QVector<int>& indices) {
    if (indices.size() < 2) return;
    QString unit;
    if (!AreUnitsCompatible(indices, unit)) {
        QMessageBox::information(this, tr("合并失败"), tr("所选信号的单位不一致，无法合并展示"));
        return;
    }

    QVector<int> merged = indices;
    std::sort(merged.begin(), merged.end());
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());

    QVector<QVector<int>> filtered;
    for (const auto& group : mergedGroups_) {
        bool intersects = false;
        for (int idx : group) {
            if (merged.contains(idx)) {
                intersects = true;
                break;
            }
        }
        if (!intersects) filtered.append(group);
    }
    mergedGroups_ = std::move(filtered);
    mergedGroups_.append(merged);

    UpdateCharts();
}

void MainWindow::MergeSelectedSignals() {
    const QVector<int> indices = CollectSelectedSignalIndices();
    MergeSignals(indices);
}

void MainWindow::UnmergeSelectedSignals() {
    const QVector<int> indices = CollectSelectedSignalIndices();
    if (indices.isEmpty()) return;

    QVector<QVector<int>> filtered;
    for (const auto& group : mergedGroups_) {
        bool intersects = false;
        for (int idx : group) {
            if (indices.contains(idx)) {
                intersects = true;
                break;
            }
        }
        if (!intersects) filtered.append(group);
    }
    mergedGroups_ = std::move(filtered);
    UpdateCharts();
}

void MainWindow::ShowSignalTreeMenu(const QPoint& pos) {
    if (!signalTree_) return;
    QMenu menu(this);
    const QVector<int> selected = CollectSelectedSignalIndices();

    QString unit;
    const bool canMerge = selected.size() >= 2 && AreUnitsCompatible(selected, unit);
    if (canMerge) {
        menu.addAction(tr("合并显示"), this, &MainWindow::MergeSelectedSignals);
    }

    bool canUnmerge = false;
    for (const auto& group : mergedGroups_) {
        for (int idx : group) {
            if (selected.contains(idx)) {
                canUnmerge = true;
                break;
            }
        }
        if (canUnmerge) break;
    }
    if (canUnmerge) {
        menu.addAction(tr("取消合并"), this, &MainWindow::UnmergeSelectedSignals);
    }

    if (!menu.actions().isEmpty()) {
        menu.exec(signalTree_->viewport()->mapToGlobal(pos));
    }
}

void MainWindow::UpdateDisplayGroups() {
    displayGroups_.clear();
    const QVector<int> checked = CollectCheckedSignalIndices();
    if (checked.isEmpty()) return;

    QVector<QVector<int>> validMerged;
    QVector<int> mergedIndices;

    for (const auto& group : mergedGroups_) {
        bool allChecked = true;
        for (int idx : group) {
            if (!checked.contains(idx)) {
                allChecked = false;
                break;
            }
        }
        if (!allChecked || group.size() < 2) continue;
        validMerged.append(group);
        for (int idx : group) {
            if (!mergedIndices.contains(idx)) mergedIndices.append(idx);
        }

        DisplayGroup displayGroup;
        displayGroup.signalIndices = group;
        displayGroup.merged = true;
        QString unit;
        AreUnitsCompatible(group, unit);
        displayGroup.unit = unit;
        QStringList names;
        for (int idx : group) {
            if (idx >= 0 && idx < static_cast<int>(format_.signalFormats.size())) {
                names.append(format_.signalFormats[idx].name);
            }
        }
        displayGroup.title = tr("合并: %1").arg(names.join(QStringLiteral(" + ")));
        displayGroups_.append(displayGroup);
    }
    mergedGroups_ = std::move(validMerged);

    for (int idx : checked) {
        if (mergedIndices.contains(idx)) continue;
        DisplayGroup group;
        group.signalIndices = {idx};
        group.merged = false;
        if (idx >= 0 && idx < static_cast<int>(format_.signalFormats.size())) {
            group.title = format_.signalFormats[idx].name;
            group.unit = format_.signalFormats[idx].unit;
        }
        displayGroups_.append(group);
    }
}

void MainWindow::UpdateCharts() {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!chartsSplitter_) return;
    ClearCharts();
    HideCrosshairs();
    sharedCursorX_ = 0.0;

    UpdateDisplayGroups();
    if (displayGroups_.isEmpty()) return;
    if (!hasGlobalRange_) UpdateGlobalRanges();

    const auto palette = SeriesPalette();

    for (const auto& group : displayGroups_) {
        auto* chart = new QChart();
        chart->setTitle(group.unit.isEmpty() ? group.title : QStringLiteral("%1 (%2)").arg(group.title, group.unit));
        chart->setBackgroundBrush(kChartBackground);
        chart->setPlotAreaBackgroundVisible(true);
        chart->setPlotAreaBackgroundBrush(kPlotBackground);
        chart->setTitleBrush(QBrush(kAxisTextColor));
        chart->legend()->setLabelColor(kAxisTextColor);

        QVector<QLineSeries*> seriesList;
        QVector<int> seriesIndices;
        for (int i = 0; i < group.signalIndices.size(); ++i) {
            const int idx = group.signalIndices[i];
            if (idx < 0 || idx >= static_cast<int>(series_.size())) continue;
            auto* line = new QLineSeries(chart);
            const auto& data = series_[idx];
            for (const auto& pt : data.samples) {
                line->append(pt);
            }
            QPen seriesPen(palette[i % palette.size()]);
            seriesPen.setWidthF(1.5);
            line->setPen(seriesPen);
            line->setName(data.unit.isEmpty() ? data.name : QStringLiteral("%1 (%2)").arg(data.name, data.unit));
            chart->addSeries(line);
            seriesList.append(line);
            seriesIndices.append(idx);
        }

        auto* axisX = new QValueAxis(chart);
        axisX->setTitleText(tr("时间索引"));
        if (hasCurrentXRange_) {
            axisX->setRange(currentMinX_, currentMaxX_);
        } else {
            axisX->setRange(0.0, globalMaxX_);
        }
        axisX->setTitleBrush(QBrush(kAxisTextColor));
        axisX->setLabelsColor(kAxisTextColor);
        axisX->setLinePenColor(kAxisLineColor);
        axisX->setGridLineColor(kGridLineColor);
        chart->addAxis(axisX, Qt::AlignBottom);
        for (auto* line : seriesList) {
            line->attachAxis(axisX);
        }

        auto* axisY = new QValueAxis(chart);
        double groupMinY = -1.0;
        double groupMaxY = 1.0;
        if (!ComputeGroupRange(group.signalIndices, groupMinY, groupMaxY)) {
            groupMinY = -1.0;
            groupMaxY = 1.0;
        }
        axisY->setRange(groupMinY, groupMaxY);
        axisY->setTitleBrush(QBrush(kAxisTextColor));
        axisY->setLabelsColor(kAxisTextColor);
        axisY->setLinePenColor(kAxisLineColor);
        axisY->setGridLineColor(kGridLineColor);
        chart->addAxis(axisY, Qt::AlignLeft);
        for (auto* line : seriesList) {
            line->attachAxis(axisY);
        }

        auto* view = new QChartView(chart);
        view->setRenderHint(QPainter::Antialiasing);
        view->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        view->setMouseTracking(true);
        view->installEventFilter(this);
        if (view->viewport()) {
            view->viewport()->setMouseTracking(true);
            view->viewport()->installEventFilter(this);
        }

        auto* cross = new QGraphicsLineItem();
        QPen cursorPen(kCursorLineColor);
        cursorPen.setStyle(Qt::DashLine);
        cross->setPen(cursorPen);
        cross->setZValue(1000);
        cross->setVisible(false);
        chart->scene()->addItem(cross);

        auto* zeroLine = new QGraphicsLineItem();
        QPen assistPen(kAssistLineColor);
        assistPen.setStyle(Qt::DashLine);
        zeroLine->setPen(assistPen);
        zeroLine->setZValue(500);
        zeroLine->setVisible(true);
        chart->scene()->addItem(zeroLine);

        QVector<QGraphicsSimpleTextItem*> labels;
        for (int i = 0; i < seriesList.size(); ++i) {
            auto* label = new QGraphicsSimpleTextItem(chart);
            label->setBrush(kCursorValueColor);
            label->setZValue(1001 + i);
            label->setVisible(false);
            labels.append(label);
        }

        auto* rubberBand = new QRubberBand(QRubberBand::Rectangle, view->viewport());
        rubberBand->hide();

        chartsSplitter_->addWidget(view);
        chartsSplitter_->setStretchFactor(chartsSplitter_->indexOf(view), 1);
        ChartItem item;
        item.view = view;
        item.series = seriesList;
        item.seriesIndices = seriesIndices;
        item.crosshair = cross;
        item.zeroLine = zeroLine;
        item.valueLabels = labels;
        item.rubberBand = rubberBand;
        charts_.append(item);
    }

    UpdateChartHeights();
    if (hasCurrentXRange_) {
        ApplyXRange(currentMinX_, currentMaxX_);
    } else {
        ApplyXRange(0.0, globalMaxX_);
    }
#endif
}

void MainWindow::UpdateStatus(const QString& text) {
    if (statusLabel_) statusLabel_->setText(text);
    statusBar()->showMessage(text, 5000);
}

void MainWindow::ClearCharts() {
#ifdef PAT_ENABLE_QT_CHARTS
    charts_.clear();
    cursorActive_ = false;
    if (!chartsSplitter_) return;
    while (chartsSplitter_->count() > 0) {
        QWidget* widget = chartsSplitter_->widget(0);
        if (!widget) break;
        widget->setParent(nullptr);
        widget->deleteLater();
    }
#endif
}

MainWindow::ChartItem* MainWindow::FindChart(QChartView* view) {
#ifdef PAT_ENABLE_QT_CHARTS
    for (auto& item : charts_) {
        if (item.view == view) return &item;
    }
#endif
    return nullptr;
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
        if (event->type() == QEvent::MouseMove) {
            if (auto* item = FindChart(targetView)) {
                HandleMouseMove(*item, static_cast<QMouseEvent*>(event));
                if (item->rubberActive) HandleRubberBand(*item, static_cast<QMouseEvent*>(event));
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            if (auto* item = FindChart(targetView)) {
                HandleRubberBand(*item, static_cast<QMouseEvent*>(event));
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (auto* item = FindChart(targetView)) {
                HandleRubberBand(*item, static_cast<QMouseEvent*>(event));
            }
        } else if (event->type() == QEvent::Wheel) {
            if (auto* item = FindChart(targetView)) {
                HandleWheel(*item, static_cast<QWheelEvent*>(event));
                return true;
            }
        } else if (event->type() == QEvent::Leave) {
            HideCrosshairs();
            sharedCursorX_ = 0.0;
        }
    }
#endif
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::HandleMouseMove(ChartItem& item, QMouseEvent* event) {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!item.view || item.series.isEmpty()) return;
    auto* chart = item.view->chart();
    if (!chart) return;
    if (!hasGlobalRange_) UpdateGlobalRanges();

    const QPoint viewPos = ViewportPoint(item.view, event->globalPosition());
    const QPointF scenePos = item.view->mapToScene(viewPos);
    const QPointF chartPos = chart->mapFromScene(scenePos);
    const QPointF plotPoint = chart->mapToValue(chartPos, item.series.first());
    const double minX = hasCurrentXRange_ ? currentMinX_ : 0.0;
    const double maxX = hasCurrentXRange_ ? currentMaxX_ : globalMaxX_;
    double cursorX = plotPoint.x();
    if (cursorX < minX) cursorX = minX;
    if (cursorX > maxX) cursorX = maxX;
    cursorActive_ = true;
    UpdateSharedCursor(cursorX);
#else
    Q_UNUSED(item)
    Q_UNUSED(event)
#endif
}

void MainWindow::HandleRubberBand(ChartItem& item, QMouseEvent* event) {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!item.view || !item.rubberBand) return;
    if (event->button() == Qt::LeftButton && event->type() == QEvent::MouseButtonPress) {
        item.rubberActive = true;
        item.rubberOrigin = ViewportPoint(item.view, event->globalPosition());
        item.rubberBand->setGeometry(QRect(item.rubberOrigin, QSize()));
        item.rubberBand->show();
        return;
    }

    if (event->type() == QEvent::MouseMove && item.rubberActive) {
        const QPoint currentPos = ViewportPoint(item.view, event->globalPosition());
        const QRect rect(item.rubberOrigin, currentPos);
        item.rubberBand->setGeometry(rect.normalized());
        return;
    }

    if (event->button() == Qt::LeftButton && event->type() == QEvent::MouseButtonRelease && item.rubberActive) {
        item.rubberBand->hide();
        item.rubberActive = false;
        const QPoint currentPos = ViewportPoint(item.view, event->globalPosition());
        QRect rect(item.rubberOrigin, currentPos);
        rect = rect.normalized();
        if (rect.width() < 6) return;

        auto* chart = item.view->chart();
        if (!chart || item.series.isEmpty()) return;

        const QPointF topLeft = item.view->mapToScene(rect.topLeft());
        const QPointF bottomRight = item.view->mapToScene(rect.bottomRight());
        const QPointF chartTopLeft = chart->mapFromScene(topLeft);
        const QPointF chartBottomRight = chart->mapFromScene(bottomRight);
        const QPointF p1 = chart->mapToValue(chartTopLeft, item.series.first());
        const QPointF p2 = chart->mapToValue(chartBottomRight, item.series.first());
        const double minX = std::min(p1.x(), p2.x());
        const double maxX = std::max(p1.x(), p2.x());
        ApplyXRange(minX, maxX);
        return;
    }
#else
    Q_UNUSED(item)
    Q_UNUSED(event)
#endif
}

void MainWindow::HandleWheel(ChartItem& item, QWheelEvent* event) {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!item.view || item.series.isEmpty()) return;
    auto* chart = item.view->chart();
    if (!chart) return;
    auto* axisX = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal, item.series.first()).value(0));
    if (!axisX) return;

    const double currentMin = axisX->min();
    const double currentMax = axisX->max();
    if (qFuzzyCompare(currentMin, currentMax)) return;

    const double delta = static_cast<double>(event->angleDelta().y());
    if (delta == 0.0) return;
    const double zoomFactor = delta > 0.0 ? 0.8 : 1.25;

    const QPoint viewPos = ViewportPoint(item.view, event->globalPosition());
    const QPointF scenePos = item.view->mapToScene(viewPos);
    const QPointF chartPos = chart->mapFromScene(scenePos);
    const QPointF plotPoint = chart->mapToValue(chartPos, item.series.first());
    const double centerX = plotPoint.x();
    const double leftSpan = centerX - currentMin;
    const double rightSpan = currentMax - centerX;
    double newMin = centerX - leftSpan * zoomFactor;
    double newMax = centerX + rightSpan * zoomFactor;

    ApplyXRange(newMin, newMax);
    event->accept();
#else
    Q_UNUSED(item)
    Q_UNUSED(event)
#endif
}

void MainWindow::UpdateSharedCursor(double cursorX) {
#ifdef PAT_ENABLE_QT_CHARTS
    sharedCursorX_ = cursorX;
    for (auto& item : charts_) {
        if (!item.view || item.series.isEmpty()) continue;
        auto* chart = item.view->chart();
        if (!chart) continue;
        auto* axisY = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical, item.series.first()).value(0));
        if (!axisY) continue;

        const double yMin = axisY->min();
        const double yMax = axisY->max();
        const QPointF top = chart->mapToPosition(QPointF(cursorX, yMax), item.series.first());
        const QPointF bottom = chart->mapToPosition(QPointF(cursorX, yMin), item.series.first());
        if (item.crosshair) {
            item.crosshair->setLine(QLineF(chart->mapToScene(top), chart->mapToScene(bottom)));
            item.crosshair->setVisible(true);
        }

        for (int i = 0; i < item.seriesIndices.size(); ++i) {
            const int idx = item.seriesIndices[i];
            if (idx < 0 || idx >= static_cast<int>(series_.size())) continue;
            const auto& seriesData = series_[idx];
            if (seriesData.samples.isEmpty()) continue;
            const int lastIdx = static_cast<int>(seriesData.samples.size()) - 1;
            const double seriesMaxX = seriesData.samples.last().x();
            double clampedX = cursorX;
            if (clampedX < 0.0) clampedX = 0.0;
            if (clampedX > seriesMaxX) clampedX = seriesMaxX;

            const int leftIdx = std::max(0, std::min(static_cast<int>(std::floor(clampedX)), lastIdx));
            const int rightIdx = std::min(leftIdx + 1, lastIdx);
            double value = seriesData.samples.at(leftIdx).y();
            if (rightIdx != leftIdx) {
                const double x0 = seriesData.samples.at(leftIdx).x();
                const double x1 = seriesData.samples.at(rightIdx).x();
                const double y0 = seriesData.samples.at(leftIdx).y();
                const double y1 = seriesData.samples.at(rightIdx).y();
                const double t = (clampedX - x0) / (x1 - x0);
                value = y0 + (y1 - y0) * t;
            }

            if (i < item.valueLabels.size() && item.valueLabels[i]) {
                item.valueLabels[i]->setText(QStringLiteral("t=%1  val=%2")
                                                 .arg(cursorX, 0, 'f', 2)
                                                 .arg(value, 0, 'f', 4));
                auto* seriesRef = item.series.value(i);
                if (!seriesRef) seriesRef = item.series.first();
                const QPointF valuePoint = chart->mapToPosition(QPointF(cursorX, value), seriesRef);
                QPointF labelPos = chart->mapToScene(valuePoint) + QPointF(6, -12 - (12 * i));
                item.valueLabels[i]->setPos(labelPos);
                item.valueLabels[i]->setVisible(true);
            }
        }
    }
#else
    Q_UNUSED(cursorX)
#endif
}

void MainWindow::HideCrosshairs() {
#ifdef PAT_ENABLE_QT_CHARTS
    for (auto& item : charts_) {
        if (item.crosshair) item.crosshair->setVisible(false);
        for (auto* label : item.valueLabels) {
            if (label) label->setVisible(false);
        }
    }
    cursorActive_ = false;
#endif
}

void MainWindow::UpdateGlobalRanges() {
    globalMinY_ = -1.0;
    globalMaxY_ = 1.0;
    globalMaxX_ = 0.0;
    hasGlobalRange_ = false;

    for (const auto& series : series_) {
        for (const auto& pt : series.samples) {
            if (!hasGlobalRange_) {
                globalMinY_ = globalMaxY_ = pt.y();
                hasGlobalRange_ = true;
            } else {
                globalMinY_ = std::min(globalMinY_, pt.y());
                globalMaxY_ = std::max(globalMaxY_, pt.y());
            }
        }
        if (!series.samples.isEmpty()) {
            globalMaxX_ = std::max(globalMaxX_, series.samples.last().x());
        }
    }

    if (!hasGlobalRange_) {
        globalMinY_ = -1.0;
        globalMaxY_ = 1.0;
    }
    if (qFuzzyCompare(globalMinY_, globalMaxY_)) {
        const double delta = qAbs(globalMinY_) > 1.0 ? qAbs(globalMinY_) * 0.1 : 1.0;
        globalMinY_ -= delta;
        globalMaxY_ += delta;
    }
    hasGlobalRange_ = true;
}

void MainWindow::ApplyXRange(double minX, double maxX) {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!hasGlobalRange_) return;
    minX = std::max(0.0, minX);
    maxX = std::min(globalMaxX_, maxX);
    if (maxX <= minX) return;
    currentMinX_ = minX;
    currentMaxX_ = maxX;
    hasCurrentXRange_ = true;

    for (auto& chartItem : charts_) {
        if (!chartItem.view || chartItem.series.isEmpty()) continue;
        auto* axisX = qobject_cast<QValueAxis*>(chartItem.view->chart()->axes(Qt::Horizontal, chartItem.series.first()).value(0));
        if (!axisX) continue;
        axisX->setRange(minX, maxX);

        if (chartItem.zeroLine) {
            auto* axisY = qobject_cast<QValueAxis*>(chartItem.view->chart()->axes(Qt::Vertical, chartItem.series.first()).value(0));
            if (!axisY) continue;
            const double yMin = axisY->min();
            const double yMax = axisY->max();
            if (0.0 < yMin || 0.0 > yMax) {
                chartItem.zeroLine->setVisible(false);
            } else {
                chartItem.zeroLine->setVisible(true);
                const QPointF left = chartItem.view->chart()->mapToPosition(QPointF(minX, 0.0), chartItem.series.first());
                const QPointF right = chartItem.view->chart()->mapToPosition(QPointF(maxX, 0.0), chartItem.series.first());
                chartItem.zeroLine->setLine(QLineF(chartItem.view->chart()->mapToScene(left),
                                                   chartItem.view->chart()->mapToScene(right)));
            }
        }
    }
    if (cursorActive_) UpdateSharedCursor(sharedCursorX_);
#endif
}

void MainWindow::UpdateChartHeights() {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!chartsSplitter_ || !chartsScrollArea_) return;
    const int viewportHeight = chartsScrollArea_->viewport()->height();
    minChartHeight_ = std::max(80, viewportHeight / 6);
    maxChartHeight_ = std::max(minChartHeight_, viewportHeight);
    chartHeight_ = std::clamp(chartHeight_, minChartHeight_, maxChartHeight_);

    for (auto& item : charts_) {
        if (!item.view) continue;
        item.view->setMinimumHeight(minChartHeight_);
        item.view->setMaximumHeight(maxChartHeight_);
    }

    QList<int> sizes;
    for (int i = 0; i < chartsSplitter_->count(); ++i) sizes.append(chartHeight_);
    if (!sizes.isEmpty()) {
        QSignalBlocker blocker(chartsSplitter_);
        chartsSplitter_->setSizes(sizes);
    }
#endif
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    UpdateChartHeights();
}
