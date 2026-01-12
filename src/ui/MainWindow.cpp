#include "ui/MainWindow.h"

#include "ui/ChartArea.h"
#include "ui/FormatEditorDialog.h"
#include "ui/SignalTreeController.h"
#include "ui/SignalTreeWidget.h"

#include <QAction>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidgetItem>

namespace {

QString FileLeaf(const QString& path) {
    QFileInfo info(path);
    return info.fileName();
}

QString DefaultFormatTemplate() {
    return QStringLiteral(
        "{\n"
        "  \"record_size\": 16,\n"
        "  \"endianness\": \"little\",\n"
        "  \"time_unit\": \"s\",\n"
        "  \"signals\": [\n"
        "    {\n"
        "      \"name\": \"sig1\",\n"
        "      \"description\": \"\",\n"
        "      \"byte_offset\": 0,\n"
        "      \"value_type\": \"int16\",\n"
        "      \"scale\": 1.0,\n"
        "      \"bias\": 0.0,\n"
        "      \"time_scale\": 1.0,\n"
        "      \"time_unit\": \"s\",\n"
        "      \"unit\": \"\",\n"
        "      \"group\": \"\"\n"
        "    }\n"
        "  ],\n"
        "  \"groups\": [\n"
        "    {\n"
        "      \"path\": \"Group/SubGroup\",\n"
        "      \"description\": \"\"\n"
        "    }\n"
        "  ]\n"
        "}\n");
}

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
    layout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);

    auto* leftPane = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    auto* listLabel = new QLabel(tr("信号选择"), leftPane);
    signalTree_ = new SignalTreeWidget(leftPane);
    signalTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    signalTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(signalTree_, &QTreeWidget::itemChanged, this, &MainWindow::HandleSignalTreeItemChanged);
    connect(signalTree_, &QWidget::customContextMenuRequested, this, &MainWindow::ShowSignalTreeMenu);
    leftLayout->addWidget(listLabel);
    leftLayout->addWidget(signalTree_, /*stretch=*/1);
    leftPane->setLayout(leftLayout);

    signalTreeController_ = std::make_unique<SignalTreeController>(signalTree_);

    auto* rightPane = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(4, 4, 4, 4);
#ifdef PAT_ENABLE_QT_CHARTS
    chartArea_ = new ChartArea(this);
    connect(chartArea_, &ChartArea::SignalsDropped, this, &MainWindow::HandleSignalsDropped);
    connect(chartArea_, &ChartArea::MergeRequested, this, &MainWindow::HandleMergeRequested);
    connect(chartArea_, &ChartArea::ReorderRequested, this, &MainWindow::HandleReorderRequested);
    connect(chartArea_, &ChartArea::HideSignalsRequested, this, &MainWindow::HandleHideSignalsRequested);
    rightLayout->addWidget(chartArea_, /*stretch=*/1);
#else
    auto* placeholder = new QLabel(tr("Qt Charts 未启用，无法显示曲线"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(placeholder, /*stretch=*/1);
#endif

    statusLabel_ = new QLabel(tr("请先加载格式文件"), this);
    rightLayout->addWidget(statusLabel_);
    rightPane->setLayout(rightLayout);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    layout->addWidget(splitter);

    setCentralWidget(central);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::OpenFormatFile() {
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("选择格式文件"),
                                                      QString(),
                                                      tr("JSON (*.json);;所有文件 (*)"));
    if (path.isEmpty()) return;

    QString error;
    if (!formatDocument_.LoadFromFile(path, error)) {
        QMessageBox::warning(this, tr("格式加载失败"), error);
        return;
    }

    displayGroupManager_.Clear();
    BuildSignalTree();
    if (chartArea_) chartArea_->SetTimeUnit(formatDocument_.Format().timeAxisUnit);

    if (dataSession_.HasData()) {
        const QString dataPath = dataSession_.Path();
        if (!dataSession_.Load(dataPath, formatDocument_.Format(), error)) {
            dataSession_.Clear();
            QMessageBox::warning(this, tr("重解析失败"), error);
        }
    }

    UpdateCharts();
    UpdateStatus(tr("格式已加载：%1，信号数：%2")
                     .arg(FileLeaf(path))
                     .arg(static_cast<int>(formatDocument_.Format().signalFormats.size())));
}

void MainWindow::OpenDataFile() {
    if (!formatDocument_.HasFormat()) {
        QMessageBox::information(this, tr("提示"), tr("请先加载格式文件"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("选择数据文件"),
                                                      QString(),
                                                      tr("数据文件 (*.bin *.dat);;所有文件 (*)"));
    if (path.isEmpty()) return;

    QString error;
    if (!dataSession_.Load(path, formatDocument_.Format(), error)) {
        QMessageBox::warning(this, tr("解析失败"), error);
        return;
    }

#ifdef PAT_ENABLE_QT_CHARTS
    if (chartArea_) {
        chartArea_->SetStatistics(dataSession_.Statistics());
        chartArea_->SetSeries(&dataSession_.Series());
        chartArea_->SetTimeUnit(dataSession_.TimeUnit());
    }
#endif

    UpdateCharts();

    const int recordCount = dataSession_.Series().isEmpty() ? 0 : dataSession_.Series().first().samples.size();
    UpdateStatus(tr("解析完成：%1，记录数 %2").arg(FileLeaf(path)).arg(recordCount));
}

void MainWindow::NewFormatFile() {
    QString edited;
    FormatEditorDialog dialog(tr("新建格式"), DefaultFormatTemplate(), this);
    if (!dialog.Run(edited)) return;

    QString error;
    formatDocument_.Clear();
    if (!formatDocument_.LoadFromJsonText(edited, error)) {
        QMessageBox::warning(this, tr("格式不合法"), error);
        return;
    }

    displayGroupManager_.Clear();
    BuildSignalTree();
    UpdateCharts();
    SaveFormatFileAs();
}

void MainWindow::EditFormatFile() {
    if (!formatDocument_.HasFormat()) {
        QMessageBox::information(this, tr("提示"), tr("请先打开一个格式文件"));
        return;
    }

    QString edited;
    FormatEditorDialog dialog(tr("编辑格式"), formatDocument_.JsonText(), this);
    if (!dialog.Run(edited)) return;

    QString error;
    if (!formatDocument_.LoadFromJsonText(edited, error)) {
        QMessageBox::warning(this, tr("格式应用失败"), error);
        return;
    }

    displayGroupManager_.Clear();
    BuildSignalTree();
    if (chartArea_) chartArea_->SetTimeUnit(formatDocument_.Format().timeAxisUnit);

    if (dataSession_.HasData()) {
        const QString dataPath = dataSession_.Path();
        if (!dataSession_.Load(dataPath, formatDocument_.Format(), error)) {
            dataSession_.Clear();
            QMessageBox::warning(this, tr("重解析失败"), error);
        }
    }

    UpdateCharts();
    UpdateStatus(tr("格式已应用：%1").arg(formatDocument_.Path().isEmpty()
                                            ? tr("未命名格式")
                                            : FileLeaf(formatDocument_.Path())));
}

void MainWindow::SaveFormatFile() {
    if (!formatDocument_.HasFormat()) {
        QMessageBox::information(this, tr("提示"), tr("请先打开或新建格式"));
        return;
    }
    if (formatDocument_.Path().isEmpty()) {
        SaveFormatFileAs();
        return;
    }

    QString error;
    if (!formatDocument_.Save(error)) {
        QMessageBox::warning(this, tr("保存失败"), error);
        return;
    }
    UpdateStatus(tr("格式已保存：%1").arg(FileLeaf(formatDocument_.Path())));
}

void MainWindow::SaveFormatFileAs() {
    if (!formatDocument_.HasFormat()) {
        QMessageBox::information(this, tr("提示"), tr("请先打开或新建格式"));
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, tr("保存格式文件"), QString(), tr("JSON (*.json)"));
    if (path.isEmpty()) return;

    QString error;
    if (!formatDocument_.SaveAs(path, error)) {
        QMessageBox::warning(this, tr("保存失败"), error);
        return;
    }
    UpdateStatus(tr("格式已保存：%1").arg(FileLeaf(path)));
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
#ifdef PAT_ENABLE_QT_CHARTS
    if (chartArea_) chartArea_->SetMaxVisiblePoints(maxVisiblePoints_);
#endif
}

void MainWindow::HandleSignalTreeItemChanged(QTreeWidgetItem* item, int column) {
    if (signalTreeUpdating_) return;
    if (!signalTree_ || !signalTreeController_ || column != 0) {
        UpdateCharts();
        return;
    }

    signalTreeUpdating_ = true;
    QSignalBlocker blocker(signalTree_);

    const int type = item ? item->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt() : -1;
    if (type == SignalTreeWidget::ITEM_TYPE_GROUP && item) {
        const Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked) {
            signalTreeController_->SetSignalsCheckedUnderItem(item, state);
        }
    }
    signalTreeController_->UpdateParentCheckStates(item);

    signalTreeUpdating_ = false;
    UpdateCharts();
}

void MainWindow::UpdateCharts() {
#ifdef PAT_ENABLE_QT_CHARTS
    if (!chartArea_) return;
    if (!formatDocument_.HasFormat() || !dataSession_.HasData()) {
        chartArea_->SetDisplayGroups({});
        chartArea_->RefreshCharts();
        return;
    }

    const QVector<int> checked = signalTreeController_->CollectCheckedSignalIndices();
    displayGroupManager_.UpdateGroups(checked, formatDocument_.Format());
    chartArea_->SetDisplayGroups(displayGroupManager_.Groups());
    chartArea_->SetStatistics(dataSession_.Statistics());
    chartArea_->SetSeries(&dataSession_.Series());
    chartArea_->SetTimeUnit(dataSession_.TimeUnit());
    chartArea_->SetMaxVisiblePoints(maxVisiblePoints_);
    chartArea_->RefreshCharts();
#else
#endif
}

void MainWindow::UpdateStatus(const QString& text) {
    if (statusLabel_) statusLabel_->setText(text);
    statusBar()->showMessage(text, 5000);
}

void MainWindow::BuildSignalTree() {
    if (!signalTreeController_) return;
    signalTreeController_->Build(formatDocument_.Format());
}

void MainWindow::ShowSignalTreeMenu(const QPoint& pos) {
    if (!signalTreeController_ || !signalTree_ || !formatDocument_.HasFormat()) return;
    QMenu menu(this);
    const QTreeWidgetItem* clickedItem = signalTree_->itemAt(pos);
    QVector<int> targetIndices;
    bool hasGroupContext = false;

    if (clickedItem) {
        const int type = clickedItem->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt();
        if (type == SignalTreeWidget::ITEM_TYPE_GROUP) {
            hasGroupContext = true;
            targetIndices = signalTreeController_->CollectSignalIndices(clickedItem);
        } else if (type == SignalTreeWidget::ITEM_TYPE_SIGNAL) {
            targetIndices = signalTreeController_->CollectSelectedSignalIndices();
            if (targetIndices.isEmpty()) {
                targetIndices = signalTreeController_->CollectSignalIndices(clickedItem);
            }
        }
    } else {
        targetIndices = signalTreeController_->CollectSelectedSignalIndices();
    }

    QString unit;
    const bool canMerge = DisplayGroupManager::CanMerge(targetIndices, formatDocument_.Format(), unit);
    bool hasActions = false;
    if (canMerge) {
        const QString title = hasGroupContext ? tr("合并显示（本组）") : tr("合并显示");
        menu.addAction(title, this, [this, targetIndices]() { HandleMergeRequested(targetIndices); });
        hasActions = true;
    }

    const bool canUnmerge = displayGroupManager_.HasMergedSignals(targetIndices);
    if (canUnmerge) {
        const QString title = hasGroupContext ? tr("取消合并（本组）") : tr("取消合并");
        menu.addAction(title, this, [this, targetIndices]() {
            displayGroupManager_.UnmergeSignals(targetIndices);
            UpdateCharts();
        });
        hasActions = true;
    }

    if (!targetIndices.isEmpty()) {
        if (hasActions) menu.addSeparator();
        if (hasGroupContext) {
            menu.addAction(tr("本组全部显示"), this, [this, targetIndices]() {
                if (!signalTreeController_) return;
                signalTreeController_->SetSignalsChecked(targetIndices, true);
                UpdateCharts();
            });
            menu.addAction(tr("本组全部隐藏"), this, [this, targetIndices]() {
                if (!signalTreeController_) return;
                signalTreeController_->SetSignalsChecked(targetIndices, false);
                displayGroupManager_.RemoveSignals(targetIndices);
                UpdateCharts();
            });
        } else {
            menu.addAction(tr("显示所选信号"), this, [this, targetIndices]() {
                if (!signalTreeController_) return;
                signalTreeController_->SetSignalsChecked(targetIndices, true);
                UpdateCharts();
            });
            menu.addAction(tr("隐藏所选信号"), this, [this, targetIndices]() {
                if (!signalTreeController_) return;
                signalTreeController_->SetSignalsChecked(targetIndices, false);
                displayGroupManager_.RemoveSignals(targetIndices);
                UpdateCharts();
            });
        }
        hasActions = true;
    }

    if (hasActions) menu.addSeparator();
    menu.addAction(tr("取消所有展示信号"), this, [this]() {
        if (!signalTreeController_) return;
        signalTreeController_->SetAllSignalsChecked(false);
        displayGroupManager_.Clear();
        UpdateCharts();
    });

    menu.exec(signalTree_->viewport()->mapToGlobal(pos));
}

void MainWindow::HandleSignalsDropped(const QVector<int>& indices) {
    if (!signalTreeController_) return;
    signalTreeController_->SetSignalsChecked(indices, true);
    displayGroupManager_.RemoveSignals(indices);
    UpdateCharts();
}

void MainWindow::HandleMergeRequested(const QVector<int>& indices) {
    if (!formatDocument_.HasFormat()) return;
    if (signalTreeController_) {
        signalTreeController_->SetSignalsChecked(indices, true);
    }
    QString error;
    if (!displayGroupManager_.MergeSignals(indices, formatDocument_.Format(), error)) {
        if (!error.isEmpty()) QMessageBox::information(this, tr("合并失败"), error);
        return;
    }
    UpdateCharts();
}

void MainWindow::HandleReorderRequested(int fromIndex, int toIndex) {
    Q_UNUSED(fromIndex)
    Q_UNUSED(toIndex)
}

void MainWindow::HandleHideSignalsRequested(const QVector<int>& indices) {
    if (!signalTreeController_) return;
    signalTreeController_->SetSignalsChecked(indices, false);
    displayGroupManager_.RemoveSignals(indices);
    UpdateCharts();
}
