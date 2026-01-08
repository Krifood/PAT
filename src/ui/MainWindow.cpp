#include "ui/MainWindow.h"

#include <QAction>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QLabel>
#include <QTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QPushButton>
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
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#include <QContextMenuEvent>
#include <QDrag>
#include <QApplication>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QRubberBand>
#include <QPen>
#include <QColor>
#include <QSizePolicy>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

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

QPointF ChartPointFromViewport(QChartView* view, QChart* chart, const QPoint& viewportPos) {
    if (!view || !chart) return viewportPos;
    const QPointF scenePos = view->mapToScene(viewportPos);
    return chart->mapFromScene(scenePos);
}

constexpr const char* kChartReorderMime = "application/x-pat-chart-reorder";
#endif

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    SetupUi();
}

void MainWindow::SetupUi() {
    setWindowTitle(tr("PAT 飞参解析工具"));

    auto* openFormatAction = new QAction(tr("打开格式..."), this);
    auto* newFormatAction = new QAction(tr("新建格式..."), this);
    auto* editFormatAction = new QAction(tr("编辑格式..."), this);
    auto* saveFormatAction = new QAction(tr("保存格式"), this);
    auto* saveAsFormatAction = new QAction(tr("格式另存为..."), this);
    auto* openDataAction = new QAction(tr("打开数据..."), this);
    auto* setMaxPointsAction = new QAction(tr("设置最大显示点数..."), this);
    auto* exitAction = new QAction(tr("退出"), this);

    connect(openFormatAction, &QAction::triggered, this, &MainWindow::OpenFormatFile);
    connect(newFormatAction, &QAction::triggered, this, &MainWindow::NewFormatFile);
    connect(editFormatAction, &QAction::triggered, this, &MainWindow::EditFormatFile);
    connect(saveFormatAction, &QAction::triggered, this, &MainWindow::SaveFormatFile);
    connect(saveAsFormatAction, &QAction::triggered, this, &MainWindow::SaveFormatFileAs);
    connect(openDataAction, &QAction::triggered, this, &MainWindow::OpenDataFile);
    connect(setMaxPointsAction, &QAction::triggered, this, &MainWindow::SetMaxVisiblePoints);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);

    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(newFormatAction);
    fileMenu->addAction(openFormatAction);
    fileMenu->addAction(editFormatAction);
    fileMenu->addAction(saveFormatAction);
    fileMenu->addAction(saveAsFormatAction);
    fileMenu->addAction(openDataAction);
    fileMenu->addAction(setMaxPointsAction);
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
    chartsScrollArea_->viewport()->setAcceptDrops(true);
    chartsScrollArea_->viewport()->installEventFilter(this);
    chartsContainer_->setAcceptDrops(true);
    chartsContainer_->installEventFilter(this);

    connect(chartsSplitter_, &QSplitter::splitterMoved, this, [this](int, int index) {
        if (!chartsSplitter_) return;
        const QList<int> sizes = chartsSplitter_->sizes();
        if (sizes.isEmpty()) return;
        const int widgetIndex = std::clamp(index, 0, static_cast<int>(sizes.size() - 1));
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
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("格式加载失败"), tr("无法打开格式文件：%1").arg(path));
        return;
    }
    const QByteArray jsonData = file.readAll();
    if (!pat::LoadFormatFromJsonData(jsonData, fmt, error)) {
        QMessageBox::warning(this, tr("格式加载失败"), error);
        return;
    }

    format_ = std::move(fmt);
    hasFormat_ = true;
    loadedFormatPath_ = path;
    formatJsonText_ = QString::fromUtf8(jsonData);
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

void MainWindow::NewFormatFile() {
    const QString templateJson = QStringLiteral(
        "{\n"
        "  \"record_size\": 16,\n"
        "  \"endianness\": \"little\",\n"
        "  \"signals\": [\n"
        "    {\n"
        "      \"name\": \"sig1\",\n"
        "      \"byte_offset\": 0,\n"
        "      \"value_type\": \"int16\",\n"
        "      \"scale\": 1.0,\n"
        "      \"bias\": 0.0,\n"
        "      \"time_scale\": 1.0,\n"
        "      \"unit\": \"\",\n"
        "      \"group\": \"\"\n"
        "    }\n"
        "  ]\n"
        "}\n");

    QString edited;
    if (!RunFormatEditor(templateJson, edited, tr("新建格式"))) return;
    if (!ApplyFormatJsonText(edited, tr("未保存的新建格式"))) return;
    loadedFormatPath_.clear();
    SaveFormatFileAs();
}

void MainWindow::EditFormatFile() {
    if (!hasFormat_) {
        QMessageBox::information(this, tr("提示"), tr("请先打开一个格式文件"));
        return;
    }
    QString edited;
    if (!RunFormatEditor(formatJsonText_, edited, tr("编辑格式"))) return;
    ApplyFormatJsonText(edited, loadedFormatPath_.isEmpty() ? tr("未命名格式") : FileLeaf(loadedFormatPath_));
}

void MainWindow::SaveFormatFile() {
    if (!hasFormat_) {
        QMessageBox::information(this, tr("提示"), tr("请先打开或新建格式"));
        return;
    }
    if (loadedFormatPath_.isEmpty()) {
        SaveFormatFileAs();
        return;
    }
    pat::FormatDefinition tmp;
    QString error;
    if (!pat::LoadFormatFromJsonData(formatJsonText_.toUtf8(), tmp, error)) {
        QMessageBox::warning(this, tr("保存失败"), tr("当前格式内容不合法：%1").arg(error));
        return;
    }
    QFile file(loadedFormatPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法写入：%1").arg(loadedFormatPath_));
        return;
    }
    file.write(formatJsonText_.toUtf8());
    UpdateStatus(tr("格式已保存：%1").arg(FileLeaf(loadedFormatPath_)));
}

void MainWindow::SaveFormatFileAs() {
    if (!hasFormat_) {
        QMessageBox::information(this, tr("提示"), tr("请先打开或新建格式"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("保存格式文件"), QString(), tr("JSON (*.json)"));
    if (path.isEmpty()) return;
    loadedFormatPath_ = path;
    SaveFormatFile();
}

void MainWindow::SetMaxVisiblePoints() {
    const int value = QInputDialog::getInt(this,
                                          tr("最大显示点数"),
                                          tr("每条曲线最大绘制点数（用于大文件抽稀显示）"),
                                          maxVisiblePoints_,
                                          200,
                                          200000,
                                          100);
    if (value <= 0) return;
    maxVisiblePoints_ = value;
    if (hasCurrentXRange_) {
        RefreshVisibleSeries(currentMinX_, currentMaxX_);
    }
}

bool MainWindow::RunFormatEditor(QString initialText, QString& outText, const QString& title) {
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setModal(true);

    auto* layout = new QVBoxLayout(&dlg);
    auto* editor = new QTextEdit(&dlg);
    editor->setPlainText(std::move(initialText));
    editor->setTabStopDistance(24);
    layout->addWidget(editor, /*stretch=*/1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto* validateBtn = buttons->addButton(tr("验证"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    const auto validate = [&]() -> bool {
        pat::FormatDefinition tmp;
        QString error;
        if (!pat::LoadFormatFromJsonData(editor->toPlainText().toUtf8(), tmp, error)) {
            QMessageBox::warning(&dlg, tr("格式不合法"), error);
            return false;
        }
        QMessageBox::information(&dlg, tr("验证通过"), tr("格式校验通过，信号数：%1").arg(static_cast<int>(tmp.signalFormats.size())));
        return true;
    };

    QObject::connect(validateBtn, &QPushButton::clicked, &dlg, validate);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        pat::FormatDefinition tmp;
        QString error;
        const QString text = editor->toPlainText();
        if (!pat::LoadFormatFromJsonData(text.toUtf8(), tmp, error)) {
            QMessageBox::warning(&dlg, tr("格式不合法"), error);
            return;
        }
        outText = text;
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.resize(900, 700);
    return dlg.exec() == QDialog::Accepted;
}

bool MainWindow::ApplyFormatJsonText(const QString& jsonText, const QString& sourceLabel) {
    pat::FormatDefinition fmt;
    QString error;
    if (!pat::LoadFormatFromJsonData(jsonText.toUtf8(), fmt, error)) {
        QMessageBox::warning(this, tr("格式应用失败"), error);
        return false;
    }

    format_ = std::move(fmt);
    hasFormat_ = true;
    formatJsonText_ = jsonText;

    mergedGroups_.clear();
    displayGroups_.clear();
    BuildSignalTree();

    if (!loadedDataPath_.isEmpty()) {
        pat::RecordParser parser(format_);
        QVector<pat::Series> parsed;
        QString parseError;
        if (!parser.ParseFile(loadedDataPath_, parsed, parseError)) {
            series_.clear();
            QMessageBox::warning(this, tr("重解析失败"), parseError);
        } else {
            series_ = std::move(parsed);
            UpdateGlobalRanges();
            currentMinX_ = 0.0;
            currentMaxX_ = globalMaxX_;
            hasCurrentXRange_ = true;
        }
    } else {
        series_.clear();
    }

    UpdateCharts();
    UpdateStatus(tr("格式已应用：%1").arg(sourceLabel));
    return true;
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

    const double viewMinX = hasCurrentXRange_ ? currentMinX_ : 0.0;
    const double viewMaxX = hasCurrentXRange_ ? currentMaxX_ : globalMaxX_;
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
            line->replace(DecimateSamples(data.samples, viewMinX, viewMaxX, maxVisiblePoints_));
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
        axisX->setRange(viewMinX, viewMaxX);
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
        view->setAcceptDrops(true);
        view->setMouseTracking(true);
        view->installEventFilter(this);
        if (view->viewport()) {
            view->viewport()->setAcceptDrops(true);
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

void MainWindow::ResetXRange() {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!hasGlobalRange_) UpdateGlobalRanges();
    ApplyXRange(0.0, globalMaxX_);
#endif
}

int MainWindow::ChartIndex(QChartView* view) const {
#ifdef PAT_ENABLE_QT_CHARTS
    for (int i = 0; i < charts_.size(); ++i) {
        if (charts_[i].view == view) return i;
    }
#else
    Q_UNUSED(view)
#endif
    return -1;
}

void MainWindow::ReorderCharts(int fromIndex, int toIndex) {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!chartsSplitter_) return;
    if (fromIndex < 0 || fromIndex >= charts_.size()) return;
    if (toIndex < 0 || toIndex >= charts_.size()) return;
    if (fromIndex == toIndex) return;

    // 先移动 splitter 内部 widget（Qt 会自动重排 handle）
    if (auto* w = charts_[fromIndex].view) {
        chartsSplitter_->insertWidget(toIndex, w);
        chartsSplitter_->setStretchFactor(chartsSplitter_->indexOf(w), 1);
    }

    // 再同步 ChartItem 顺序
    ChartItem moved = charts_.at(fromIndex);
    charts_.removeAt(fromIndex);
    charts_.insert(toIndex, moved);

    UpdateChartHeights();
#else
    Q_UNUSED(fromIndex)
    Q_UNUSED(toIndex)
#endif
}

bool MainWindow::ReadSignalIndicesMime(const QMimeData* mime, QVector<int>& outIndices) const {
    outIndices.clear();
    if (!mime) return false;
    if (!mime->hasFormat(SignalTreeWidget::kSignalIndicesMime)) return false;
    QByteArray payload = mime->data(SignalTreeWidget::kSignalIndicesMime);
    QDataStream stream(&payload, QIODevice::ReadOnly);
    stream >> outIndices;
    return !outIndices.isEmpty();
}

void MainWindow::SetSignalsChecked(const QVector<int>& indices, bool checked) {
    if (!signalTree_ || indices.isEmpty()) return;
    QSignalBlocker blocker(signalTree_);
    const QSet<int> set = QSet<int>(indices.begin(), indices.end());

    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, SignalTreeWidget::kItemTypeRole).toInt();
        if (type == SignalTreeWidget::kItemTypeSignal) {
            const int idx = item->data(0, SignalTreeWidget::kSignalIndexRole).toInt();
            if (set.contains(idx)) item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            return;
        }
        for (int i = 0; i < item->childCount(); ++i) visit(item->child(i));
    };
    auto* root = signalTree_->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) visit(root->child(i));
}

void MainWindow::RemoveFromMergedGroups(const QVector<int>& indices) {
    if (indices.isEmpty()) return;
    QVector<QVector<int>> filtered;
    for (auto group : mergedGroups_) {
        group.erase(std::remove_if(group.begin(),
                                   group.end(),
                                   [&](int idx) { return indices.contains(idx); }),
                    group.end());
        if (group.size() >= 2) filtered.append(group);
    }
    mergedGroups_ = std::move(filtered);
}

QVector<QPointF> MainWindow::DecimateSamples(const QVector<QPointF>& samples, double minX, double maxX, int maxPoints) const {
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

void MainWindow::RefreshVisibleSeries(double minX, double maxX) {
#ifdef PAT_ENABLE_QT_CHARTS
    for (auto& chartItem : charts_) {
        for (int i = 0; i < chartItem.series.size() && i < chartItem.seriesIndices.size(); ++i) {
            auto* line = chartItem.series[i];
            const int idx = chartItem.seriesIndices[i];
            if (!line || idx < 0 || idx >= static_cast<int>(series_.size())) continue;
            line->replace(DecimateSamples(series_[idx].samples, minX, maxX, maxVisiblePoints_));
        }
    }
#else
    Q_UNUSED(minX)
    Q_UNUSED(maxX)
#endif
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event) {
#ifdef PAT_ENABLE_QT_CHARTS
    if (obj == chartsScrollArea_->viewport() || obj == chartsContainer_) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            QVector<int> indices;
            if (ReadSignalIndicesMime(dragEvent->mimeData(), indices)) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            QVector<int> indices;
            if (ReadSignalIndicesMime(dropEvent->mimeData(), indices)) {
                SetSignalsChecked(indices, true);
                RemoveFromMergedGroups(indices);
                UpdateCharts();
                dropEvent->acceptProposedAction();
                return true;
            }
        }
    }

    QChartView* targetView = qobject_cast<QChartView*>(obj);
    if (!targetView) {
        if (auto* widget = qobject_cast<QWidget*>(obj)) {
            targetView = qobject_cast<QChartView*>(widget->parent());
        }
    }
    if (targetView) {
        if (event->type() == QEvent::ContextMenu) {
            auto* ctx = static_cast<QContextMenuEvent*>(event);
            QMenu menu(this);
            menu.addAction(tr("还原时间轴"), this, &MainWindow::ResetXRange);
            menu.exec(ctx->globalPos());
            return true;
        }

        if (event->type() == QEvent::DragEnter) {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData() &&
                (dragEvent->mimeData()->hasFormat(SignalTreeWidget::kSignalIndicesMime) ||
                 dragEvent->mimeData()->hasFormat(kChartReorderMime))) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData() &&
                (dragEvent->mimeData()->hasFormat(SignalTreeWidget::kSignalIndicesMime) ||
                 dragEvent->mimeData()->hasFormat(kChartReorderMime))) {
                dragEvent->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            if (!dropEvent->mimeData()) return false;

            if (dropEvent->mimeData()->hasFormat(kChartReorderMime)) {
                QByteArray payload = dropEvent->mimeData()->data(kChartReorderMime);
                QDataStream stream(&payload, QIODevice::ReadOnly);
                int fromIndex = -1;
                stream >> fromIndex;
                const int toIndex = ChartIndex(targetView);
                ReorderCharts(fromIndex, toIndex);
                dropEvent->acceptProposedAction();
                return true;
            }

            QVector<int> dropped;
            if (ReadSignalIndicesMime(dropEvent->mimeData(), dropped)) {
                if (auto* item = FindChart(targetView)) {
                    QVector<int> merged = item->seriesIndices;
                    for (int idx : dropped) {
                        if (idx >= 0 && !merged.contains(idx)) merged.append(idx);
                    }
                    SetSignalsChecked(merged, true);
                    MergeSignals(merged);
                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (auto* item = FindChart(targetView)) {
                if (item->reorderArmed && (static_cast<QMouseEvent*>(event)->buttons() & Qt::LeftButton)) {
                    const QPoint pos = ViewportPoint(item->view, static_cast<QMouseEvent*>(event)->globalPosition());
                    if ((pos - item->reorderStartPos).manhattanLength() >= QApplication::startDragDistance()) {
                        const int fromIndex = ChartIndex(item->view);
                        if (fromIndex >= 0) {
                            auto* drag = new QDrag(item->view);
                            auto* mime = new QMimeData();
                            QByteArray payload;
                            QDataStream stream(&payload, QIODevice::WriteOnly);
                            stream << fromIndex;
                            mime->setData(kChartReorderMime, payload);
                            drag->setMimeData(mime);
                            drag->exec(Qt::MoveAction);
                        }
                        item->reorderArmed = false;
                        return true;
                    }
                }

                HandleMouseMove(*item, static_cast<QMouseEvent*>(event));
                if (item->rubberActive) HandleRubberBand(*item, static_cast<QMouseEvent*>(event));
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            if (auto* item = FindChart(targetView)) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    const QPoint viewPos = ViewportPoint(item->view, mouseEvent->globalPosition());
                    auto* chart = item->view ? item->view->chart() : nullptr;
                    if (chart) {
                        const QPointF chartPos = ChartPointFromViewport(item->view, chart, viewPos);
                        const QRectF plotArea = chart->plotArea();
                        if (!plotArea.isEmpty() && chartPos.y() < plotArea.top()) {
                            item->reorderArmed = true;
                            item->reorderStartPos = viewPos;
                            return true;
                        }
                    }

                    if (mouseEvent->modifiers() & Qt::AltModifier) {
                        item->panning = true;
                        item->panLastPos = viewPos;
                        return true;
                    }

                    HandleRubberBand(*item, mouseEvent);
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (auto* item = FindChart(targetView)) {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    item->reorderArmed = false;
                    item->panning = false;
                    if (item->rubberActive) {
                        HandleRubberBand(*item, mouseEvent);
                        return true;
                    }
                }
            }
        } else if (event->type() == QEvent::Wheel) {
            if (auto* item = FindChart(targetView)) {
                HandleWheel(*item, static_cast<QWheelEvent*>(event));
                return true;
            }
        } else if (event->type() == QEvent::Leave) {
            if (auto* item = FindChart(targetView)) {
                item->panning = false;
                item->reorderArmed = false;
                item->rubberActive = false;
                item->rubberShown = false;
                if (item->rubberBand) item->rubberBand->hide();
            }
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
    if (item.panning && !item.rubberActive && (event->buttons() & Qt::LeftButton)) {
        const double minX = hasCurrentXRange_ ? currentMinX_ : 0.0;
        const double maxX = hasCurrentXRange_ ? currentMaxX_ : globalMaxX_;
        const double span = maxX - minX;
        const QRectF plotArea = chart->plotArea();
        if (!plotArea.isEmpty() && span > 0.0) {
            const double dx = static_cast<double>(viewPos.x() - item.panLastPos.x());
            const double deltaValue = dx / plotArea.width() * span;
            ApplyXRange(minX - deltaValue, maxX - deltaValue);
            item.panLastPos = viewPos;
        }
    }
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
        item.rubberShown = false;
        item.rubberOrigin = ViewportPoint(item.view, event->globalPosition());
        item.rubberBand->setGeometry(QRect(item.rubberOrigin, QSize()));
        item.rubberBand->hide();
        return;
    }

    if (event->type() == QEvent::MouseMove && item.rubberActive) {
        const QPoint currentPos = ViewportPoint(item.view, event->globalPosition());
        const QRect rect(item.rubberOrigin, currentPos);
        const QRect normalized = rect.normalized();
        if (!item.rubberShown && (normalized.width() + normalized.height()) >= QApplication::startDragDistance()) {
            item.rubberShown = true;
            item.rubberBand->show();
        }
        if (item.rubberShown) item.rubberBand->setGeometry(normalized);
        return;
    }

    if (event->button() == Qt::LeftButton && event->type() == QEvent::MouseButtonRelease && item.rubberActive) {
        item.rubberBand->hide();
        item.rubberActive = false;
        const bool didShow = item.rubberShown;
        item.rubberShown = false;
        const QPoint currentPos = ViewportPoint(item.view, event->globalPosition());
        QRect rect(item.rubberOrigin, currentPos);
        rect = rect.normalized();
        if (!didShow || rect.width() < 6) return;

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
    if (currentMax <= currentMin) return;

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

        for (auto* label : item.valueLabels) {
            if (label) label->setVisible(false);
        }

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
                if (dx == 0.0) {
                    value = p1.y();
                } else {
                    const double t = (clampedX - p0.x()) / dx;
                    value = p0.y() + (p1.y() - p0.y()) * t;
                }
            }

            if (i < item.valueLabels.size() && item.valueLabels[i]) {
                item.valueLabels[i]->setText(QStringLiteral("t=%1  val=%2")
                                                 .arg(cursorX, 0, 'f', 2)
                                                 .arg(value, 0, 'f', 4));
                auto* seriesRef = item.series.value(i);
                if (!seriesRef) seriesRef = item.series.first();
                const QPointF valuePoint = chart->mapToPosition(QPointF(cursorX, value), seriesRef);
                QPointF labelPos = valuePoint + QPointF(6, -12 - (12 * i));
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
    double minStep = std::numeric_limits<double>::infinity();
    bool hasStep = false;

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

        constexpr int kMaxStepScan = 4096;
        const int n = std::min(static_cast<int>(series.samples.size()), kMaxStepScan);
        for (int i = 1; i < n; ++i) {
            const double dx = std::abs(series.samples.at(i).x() - series.samples.at(i - 1).x());
            if (dx > 0.0 && dx < minStep) {
                minStep = dx;
                hasStep = true;
            }
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

    // 防止缩放到极小范围导致映射/滚轮逻辑不可恢复
    minXSpan_ = hasStep ? std::max(minStep * 0.01, 1e-3) : 1e-3;
}

void MainWindow::ApplyXRange(double minX, double maxX) {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!hasGlobalRange_) return;
    const double boundMin = 0.0;
    const double boundMax = globalMaxX_;
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
    RefreshVisibleSeries(minX, maxX);
    if (cursorActive_) {
        if (sharedCursorX_ < currentMinX_) sharedCursorX_ = currentMinX_;
        if (sharedCursorX_ > currentMaxX_) sharedCursorX_ = currentMaxX_;
        UpdateSharedCursor(sharedCursorX_);
    }
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
