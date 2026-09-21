#include "ui/MainWindow.h"

#include "ui/ChartArea.h"
#include "ui/AboutDialog.h"
#include "ui/FormatEditorDialog.h"
#include "ui/SignalTreeController.h"
#include "ui/SignalTreeWidget.h"
#include "ui/SignalProcessingDialogs.h"
#include "ui/TrajectoryView.h"
#include "core/FrameExporter.h"
#include "core/InputReader.h"
#include "core/RecordDecoder.h"
#include "core/SerialFrameAssembler.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCheckBox>
#include <QBuffer>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QHBoxLayout>
#include <QSplitter>
#include <QStringList>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QTreeWidgetItem>
#ifdef PAT_ENABLE_SERIAL
#include <QSerialPortInfo>
#endif

#include <algorithm>

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

QString DefaultSerialTemplate() {
    return QStringLiteral(
        "{\n"
        "  \"schema_version\": 2,\n"
        "  \"template_id\": \"sensor_v1\",\n"
        "  \"record_size\": 4,\n"
        "  \"endianness\": \"little\",\n"
        "  \"framing\": {\"kind\": \"fixed_header\", \"header_hex\": \"AA55\", \"frame_length\": 4},\n"
        "  \"signals\": [\n"
        "    {\"name\": \"温度\", \"byte_offset\": 2, \"value_type\": \"int16\",\n"
        "     \"scale\": 0.1, \"bias\": -40.0, \"unit\": \"°C\",\n"
        "     \"description\": \"探头温度\"}\n"
        "  ]\n"
        "}\n");
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    SetupUi();
}

MainWindow::~MainWindow() {
#ifdef PAT_ENABLE_SERIAL
    if (serialThread_ && serialThread_->isRunning()) {
        if (captureRunning_ && serialWorker_) {
            QMetaObject::invokeMethod(serialWorker_, "Stop", Qt::BlockingQueuedConnection);
        }
        serialThread_->quit();
        serialThread_->wait();
    }
#endif
}

void MainWindow::SetupUi() {
    setWindowTitle(tr("PAT 飞参解析工具"));
    resize(1280, 800);

    auto* openFormatAction = new QAction(tr("打开格式..."), this);
    auto* newFormatAction = new QAction(tr("新建格式..."), this);
    auto* newSerialFormatAction = new QAction(tr("新建串口模板..."), this);
    auto* editFormatAction = new QAction(tr("编辑格式..."), this);
    auto* saveFormatAction = new QAction(tr("保存格式"), this);
    auto* saveAsFormatAction = new QAction(tr("格式另存为..."), this);
    auto* openDataAction = new QAction(tr("打开数据..."), this);
    auto* exportFramesAction = new QAction(tr("导出原始帧..."), this);
    auto* previewFrameAction = new QAction(tr("预览帧解码..."), this);
    auto* setMaxPointsAction = new QAction(tr("设置最大显示点数..."), this);
    auto* exitAction = new QAction(tr("退出"), this);

    connect(openFormatAction, &QAction::triggered, this, &MainWindow::OpenFormatFile);
    connect(newFormatAction, &QAction::triggered, this, &MainWindow::NewFormatFile);
    connect(newSerialFormatAction, &QAction::triggered, this, &MainWindow::NewSerialFormatFile);
    connect(editFormatAction, &QAction::triggered, this, &MainWindow::EditFormatFile);
    connect(saveFormatAction, &QAction::triggered, this, &MainWindow::SaveFormatFile);
    connect(saveAsFormatAction, &QAction::triggered, this, &MainWindow::SaveFormatFileAs);
    connect(openDataAction, &QAction::triggered, this, &MainWindow::OpenDataFile);
    connect(exportFramesAction, &QAction::triggered, this, &MainWindow::ExportRawFrames);
    connect(previewFrameAction, &QAction::triggered, this, &MainWindow::PreviewFrame);
    connect(setMaxPointsAction, &QAction::triggered, this, &MainWindow::SetMaxVisiblePoints);
    connect(exitAction, &QAction::triggered, this, &MainWindow::close);

    auto* fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(newFormatAction);
    fileMenu->addAction(newSerialFormatAction);
    fileMenu->addAction(openFormatAction);
    fileMenu->addAction(editFormatAction);
    fileMenu->addAction(saveFormatAction);
    fileMenu->addAction(saveAsFormatAction);
    fileMenu->addAction(openDataAction);
    fileMenu->addAction(exportFramesAction);
    fileMenu->addAction(previewFrameAction);
    fileMenu->addAction(setMaxPointsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    viewMenu_ = menuBar()->addMenu(tr("视图"));
    auto* themeMenu = viewMenu_->addMenu(tr("显示主题"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (AppThemeId id : AvailableThemeIds()) {
        auto* action = themeMenu->addAction(ThemeDisplayName(id));
        action->setCheckable(true);
        action->setData(ThemeKey(id));
        action->setObjectName(QStringLiteral("themeAction_%1").arg(ThemeKey(id)));
        themeGroup->addAction(action);
    }
    connect(themeGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        if (action) ApplyTheme(ThemeIdFromKey(action->data().toString()));
    });

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("mainCentral"));
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setChildrenCollapsible(false);

    auto* leftPane = new QWidget(splitter);
    leftPane->setObjectName(QStringLiteral("signalPane"));
    auto* leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(4, 4, 4, 4);
    auto* listLabel = new QLabel(tr("信号选择"), leftPane);
    signalTree_ = new SignalTreeWidget(leftPane);
    signalTree_->setObjectName(QStringLiteral("signalTree"));
    signalTree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    signalTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(signalTree_, &QTreeWidget::itemChanged, this, &MainWindow::HandleSignalTreeItemChanged);
    connect(signalTree_, &QWidget::customContextMenuRequested, this, &MainWindow::ShowSignalTreeMenu);
    leftLayout->addWidget(listLabel);
    leftLayout->addWidget(signalTree_, /*stretch=*/1);
    leftPane->setLayout(leftLayout);

    signalTreeController_ = std::make_unique<SignalTreeController>(signalTree_);

    auto* rightPane = new QWidget(splitter);
    rightPane->setObjectName(QStringLiteral("chartPane"));
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(4, 4, 4, 4);
#ifdef PAT_ENABLE_QT_CHARTS
    dataViewTabs_ = new QTabWidget(rightPane);
    dataViewTabs_->setObjectName(QStringLiteral("dataViewTabs"));
    chartArea_ = new ChartArea(dataViewTabs_);
    connect(chartArea_, &ChartArea::SignalsDropped, this, &MainWindow::HandleSignalsDropped);
    connect(chartArea_, &ChartArea::MergeRequested, this, &MainWindow::HandleMergeRequested);
    connect(chartArea_, &ChartArea::ReorderRequested, this, &MainWindow::HandleReorderRequested);
    connect(chartArea_, &ChartArea::HideSignalsRequested, this, &MainWindow::HandleHideSignalsRequested);
    connect(chartArea_, &ChartArea::SplitRequested, this, &MainWindow::HandleSplitRequested);
    connect(chartArea_, &ChartArea::TransformRequested, this, &MainWindow::ShowSignalTransformDialog);
    trajectoryView_ = new TrajectoryView(dataViewTabs_);
    dataViewTabs_->addTab(chartArea_, tr("时序曲线"));
    dataViewTabs_->addTab(trajectoryView_, tr("二维轨迹"));
    rightLayout->addWidget(dataViewTabs_, /*stretch=*/1);
#ifdef PAT_ENABLE_SERIAL
    connect(dataViewTabs_, &QTabWidget::currentChanged, this, [this](int index) {
        if (index != 0 || !liveMode_ || displayPaused_ || !liveSession_.HasData()) return;
        liveSession_.UpdateStatistics();
        chartArea_->SetStatistics(liveSession_.Statistics());
        chartArea_->RefreshLiveData(followLatestCheck_->isChecked());
    });
#endif
#else
    auto* placeholder = new QLabel(tr("Qt Charts 未启用，无法显示曲线"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    rightLayout->addWidget(placeholder, /*stretch=*/1);
#endif

    statusLabel_ = new QLabel(tr("请先加载格式文件"), this);
    rightLayout->addWidget(statusLabel_);
#ifdef PAT_ENABLE_SERIAL
    captureStatsLabel_ = new QLabel(tr("串口：未采集"), this);
    captureStatsLabel_->setObjectName(QStringLiteral("captureStatsLabel"));
    rightLayout->addWidget(captureStatsLabel_);
#endif
    rightPane->setLayout(rightLayout);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({300, 980});
    layout->addWidget(splitter);

    setCentralWidget(central);
#ifdef PAT_ENABLE_SERIAL
    SetupSerialDock();
#endif
    auto* helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(tr("版本信息..."), this, &MainWindow::ShowVersionInfo);
    helpMenu->addAction(tr("更新日志..."), this, &MainWindow::ShowChangelog);
    QSettings settings(QStringLiteral("PAT"), QStringLiteral("PAT"));
    ApplyTheme(ThemeIdFromKey(settings.value(QStringLiteral("appearance/theme"),
                                               ThemeKey(AppThemeId::IndoorDark)).toString()),
               false);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::ApplyTheme(AppThemeId id, bool persist) {
    currentThemeId_ = id;
    const auto& theme = ThemeFor(id);
    setStyleSheet(BuildApplicationStyleSheet(theme));
    if (chartArea_) chartArea_->SetTheme(theme);
    if (trajectoryView_) trajectoryView_->SetTheme(theme);
    if (viewMenu_) {
        const auto actions = viewMenu_->findChildren<QAction*>();
        for (auto* action : actions) {
            if (action && action->objectName().startsWith(QStringLiteral("themeAction_"))) {
                action->setChecked(action->data().toString() == ThemeKey(id));
            }
        }
    }
    if (persist) {
        QSettings settings(QStringLiteral("PAT"), QStringLiteral("PAT"));
        settings.setValue(QStringLiteral("appearance/theme"), ThemeKey(id));
        statusBar()->showMessage(tr("已切换为%1主题").arg(ThemeDisplayName(id)), 3000);
    }
}

void MainWindow::ShowVersionInfo() {
    if (!infoDialog_) infoDialog_ = new AboutDialog(this);
    infoDialog_->SelectPage(AboutDialog::Page::Version);
    infoDialog_->show();
    infoDialog_->raise();
    infoDialog_->activateWindow();
}

void MainWindow::ShowChangelog() {
    if (!infoDialog_) infoDialog_ = new AboutDialog(this);
    infoDialog_->SelectPage(AboutDialog::Page::Changelog);
    infoDialog_->show();
    infoDialog_->raise();
    infoDialog_->activateWindow();
}

void MainWindow::OpenFormatFile() {
#ifdef PAT_ENABLE_SERIAL
    if (captureRunning_) {
        QMessageBox::information(this, tr("提示"), tr("请先停止串口采集，再切换格式"));
        return;
    }
#endif
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("选择格式文件"),
                                                      QString(),
                                                      tr("JSON (*.json);;所有文件 (*)"),
                                                      nullptr,
                                                      QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;

    QString error;
    if (!formatDocument_.LoadFromFile(path, error)) {
        QMessageBox::warning(this, tr("格式加载失败"), error);
        return;
    }

    displayGroupManager_.Clear();
    liveMode_ = false;
#ifdef PAT_ENABLE_SERIAL
    liveChartsInitialized_ = false;
#endif
    if (chartArea_) chartArea_->ResetSessionView();
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
#ifdef PAT_ENABLE_SERIAL
    if (captureRunning_) {
        QMessageBox::information(this, tr("提示"), tr("请先停止串口采集，再打开数据文件"));
        return;
    }
#endif
    if (!formatDocument_.HasFormat()) {
        QMessageBox::information(this, tr("提示"), tr("请先加载格式文件"));
        return;
    }

    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("选择数据文件"),
                                                      QString(),
                                                      tr("数据文件 (*.bin *.dat *.txt *.log *.hex *.csv);;所有文件 (*)"));
    if (path.isEmpty()) return;

    QString error;
    if (!dataSession_.Load(path, formatDocument_.Format(), error)) {
        QMessageBox::warning(this, tr("解析失败"), error);
        return;
    }
    seriesProcessing_.RebuildDerived(dataSession_.Series());

    liveMode_ = false;
    if (trajectoryView_) trajectoryView_->SetPaused(false);
    if (chartArea_) chartArea_->ResetSessionView();

#ifdef PAT_ENABLE_QT_CHARTS
    if (chartArea_) {
        chartArea_->SetStatistics(dataSession_.Statistics());
        chartArea_->SetSeries(&dataSession_.Series());
        if (formatDocument_.Format().HasFraming()) {
            chartArea_->SetAxisTitle(tr("采样序号"));
        } else {
            chartArea_->SetTimeUnit(dataSession_.TimeUnit());
        }
    }
#endif

    UpdateCharts();

    const int recordCount = dataSession_.Series().isEmpty() ? 0 : dataSession_.Series().first().samples.size();
    UpdateStatus(tr("解析完成：%1，记录数 %2").arg(FileLeaf(path)).arg(recordCount));
}

void MainWindow::ExportRawFrames() {
#ifdef PAT_ENABLE_SERIAL
    if (captureRunning_) {
        QMessageBox::information(this, tr("提示"), tr("请先停止串口采集，再导出日志中的原始帧"));
        return;
    }
#endif
    if (!formatDocument_.HasFormat() || !formatDocument_.Format().HasFraming()) {
        QMessageBox::information(this, tr("提示"), tr("请先加载带 framing 的协议模板"));
        return;
    }
    const QString input = QFileDialog::getOpenFileName(this, tr("选择原始数据或十六进制文本"),
                                                       dataSession_.Path(),
                                                       tr("数据文件 (*.bin *.dat *.txt *.log *.hex *.csv);;所有文件 (*)"));
    if (input.isEmpty()) return;
    QString selectedFilter;
    QString output = QFileDialog::getSaveFileName(this, tr("保存原始帧"),
                                                  input + QStringLiteral("_frames.txt"),
                                                  tr("TXT (*.txt);;CSV (*.csv)"), &selectedFilter);
    if (output.isEmpty()) return;
    if (QFileInfo(output).suffix().isEmpty()) {
        output += selectedFilter.contains(QStringLiteral("CSV"))
                      ? QStringLiteral(".csv") : QStringLiteral(".txt");
    }
    const QStringList categories = {tr("不分类"), tr("按模板"), tr("按完整帧"),
                                    tr("按指定字节"), tr("按解析字段")};
    bool accepted = false;
    const QString choice = QInputDialog::getItem(this, tr("帧归类"), tr("分类依据"),
                                                 categories, 0, false, &accepted);
    if (!accepted) return;
    pat::FrameExportOptions options;
    options.format = output.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
                         ? pat::FrameExportFormat::Csv : pat::FrameExportFormat::Text;
    const int categoryIndex = categories.indexOf(choice);
    if (categoryIndex == 1) options.category = pat::FrameCategory::Template;
    if (categoryIndex == 2) options.category = pat::FrameCategory::FullFrame;
    if (categoryIndex == 3) {
        options.category = pat::FrameCategory::ByteIndex;
        options.byteIndex = QInputDialog::getInt(this, tr("分类字节"), tr("字节位置（从 0 开始）"),
                                                 0, 0, formatDocument_.Format().recordSize - 1,
                                                 1, &accepted);
        if (!accepted) return;
    }
    if (categoryIndex == 4) {
        options.category = pat::FrameCategory::SignalValue;
        QStringList names;
        for (const auto& signal : formatDocument_.Format().signalFormats) names.append(signal.name);
        const QString chosen = QInputDialog::getItem(this, tr("分类字段"), tr("信号"),
                                                     names, 0, false, &accepted);
        if (!accepted) return;
        options.signalIndex = names.indexOf(chosen);
    }
    pat::FrameExportResult result;
    QString error;
    if (!pat::ExportFrames(input, output, formatDocument_.Format(), options, result, error)) {
        QMessageBox::warning(this, tr("导出失败"), error);
        return;
    }
    UpdateStatus(tr("已导出 %1 帧；跳过 %2 字节、%3 个无效文本片段")
                     .arg(result.frameCount).arg(result.skippedBytes).arg(result.skippedTextTokens));
}

void MainWindow::PreviewFrame() {
    if (!formatDocument_.HasFormat()) {
        QMessageBox::information(this, tr("提示"), tr("请先加载协议模板"));
        return;
    }
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(this, tr("预览帧解码"),
                                                        tr("输入单帧十六进制字节"),
                                                        QString(), &accepted);
    if (!accepted) return;
    QBuffer input;
    input.setData(text.toUtf8());
    input.open(QIODevice::ReadOnly);
    QByteArray raw;
    QString error;
    pat::InputReadStats stats;
    const auto collect = [&raw](const QByteArray& bytes) {
        raw.append(bytes);
        return true;
    };
    if (!pat::ReadInputBytes(input, true, collect, stats, error)) {
        QMessageBox::warning(this, tr("预览失败"), error);
        return;
    }
    if (raw.size() != formatDocument_.Format().recordSize) {
        QMessageBox::warning(this, tr("预览失败"),
                             tr("输入 %1 字节，模板要求 %2 字节")
                                 .arg(raw.size()).arg(formatDocument_.Format().recordSize));
        return;
    }
    if (formatDocument_.Format().HasFraming()) {
        pat::SerialFrameAssembler assembler({formatDocument_.Format()});
        if (assembler.Feed(raw).size() != 1) {
            QMessageBox::warning(this, tr("预览失败"), tr("输入帧头或校验与模板不匹配"));
            return;
        }
    }
    pat::RecordDecoder decoder(formatDocument_.Format());
    QVector<double> values;
    if (!decoder.Decode(raw, values, error)) {
        QMessageBox::warning(this, tr("预览失败"), error);
        return;
    }
    QStringList rows;
    for (qsizetype i = 0; i < values.size(); ++i) {
        const auto& signal = formatDocument_.Format().signalFormats[static_cast<size_t>(i)];
        rows.append(QStringLiteral("%1 = %2 %3")
                        .arg(signal.name, QString::number(values[i], 'g', 17), signal.unit));
    }
    QMessageBox::information(this, tr("解码预览"), rows.join('\n'));
}

void MainWindow::NewFormatFile() {
#ifdef PAT_ENABLE_SERIAL
    if (captureRunning_) {
        QMessageBox::information(this, tr("提示"), tr("请先停止串口采集，再新建格式"));
        return;
    }
#endif
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
    liveMode_ = false;
    dataSession_.Clear();
    if (chartArea_) chartArea_->ResetSessionView();
    BuildSignalTree();
    UpdateCharts();
    SaveFormatFileAs();
}

void MainWindow::NewSerialFormatFile() {
#ifdef PAT_ENABLE_SERIAL
    if (captureRunning_) {
        QMessageBox::information(this, tr("提示"), tr("请先停止串口采集，再新建模板"));
        return;
    }
#endif
    QString edited;
    FormatEditorDialog dialog(tr("新建串口模板"), DefaultSerialTemplate(), this);
    if (!dialog.Run(edited)) return;
    QString error;
    formatDocument_.Clear();
    if (!formatDocument_.LoadFromJsonText(edited, error)) {
        QMessageBox::warning(this, tr("模板不合法"), error);
        return;
    }
    dataSession_.Clear();
    liveMode_ = false;
    displayGroupManager_.Clear();
    if (chartArea_) chartArea_->ResetSessionView();
    BuildSignalTree();
    UpdateCharts();
    SaveFormatFileAs();
}

void MainWindow::EditFormatFile() {
#ifdef PAT_ENABLE_SERIAL
    if (captureRunning_) {
        QMessageBox::information(this, tr("提示"), tr("请先停止串口采集，再编辑格式"));
        return;
    }
#endif
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
    liveMode_ = false;
    if (chartArea_) chartArea_->ResetSessionView();
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
    const bool hasData = liveMode_ ? liveSession_.HasData() : dataSession_.HasData();
    if (trajectoryView_) {
        trajectoryView_->SetDataSource(liveMode_ ? &liveSession_.SeriesData()
                                                 : &dataSession_.Series(),
                                       liveMode_ ? pat::TrajectoryPairing::SampleIndex
                                                 : pat::TrajectoryPairing::RecordOrder);
    }
    // Framed signals can show empty axes before capture and while waiting for a valid frame.
    if (!formatDocument_.HasFormat()
        || (!liveMode_ && !hasData && !formatDocument_.Format().HasFraming())) {
        chartArea_->SetDisplayGroups({});
        chartArea_->RefreshCharts();
        return;
    }

    const QVector<int> checked = signalTreeController_->CollectCheckedSignalIndices();
    displayGroupManager_.UpdateGroups(checked, formatDocument_.Format());
    QVector<DisplayGroup> groups = displayGroupManager_.Groups();
    const auto& differences = seriesProcessing_.Differences();
    for (int i = 0; i < differences.size(); ++i) {
        const auto& difference = differences[i];
        groups.append({{pat::SeriesProcessing::DerivedDisplayIndex(i)},
                       difference.title, difference.unit, false});
    }
    chartArea_->SetDisplayGroups(groups);
    chartArea_->SetDerivedSeries(&seriesProcessing_.DerivedSeries());
    chartArea_->SetDisplayTransforms(seriesProcessing_.Transforms());
    if (liveMode_) {
        chartArea_->SetStatistics(liveSession_.Statistics());
        chartArea_->SetSeries(&liveSession_.SeriesData());
        chartArea_->SetAxisTitle(tr("采样序号"));
    } else {
        chartArea_->SetStatistics(dataSession_.Statistics());
        chartArea_->SetSeries(&dataSession_.Series());
        if (formatDocument_.Format().HasFraming()) chartArea_->SetAxisTitle(tr("采样序号"));
        else chartArea_->SetTimeUnit(dataSession_.TimeUnit());
    }
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
    seriesProcessing_.Reset(formatDocument_.Format());
    if (trajectoryView_) {
        trajectoryView_->SetPaused(false);
        trajectoryView_->ConfigureSignals(formatDocument_.Format());
    }
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

    if (targetIndices.size() == 1) {
        if (hasActions) menu.addSeparator();
        const int signalIndex = targetIndices.first();
        menu.addAction(tr("显示比例与平移..."), this, [this, signalIndex]() {
            ShowSignalTransformDialog(signalIndex);
        });
        if (!seriesProcessing_.Transform(signalIndex).IsIdentity()) {
            menu.addAction(tr("重置显示变换"), this, [this, signalIndex]() {
                seriesProcessing_.ResetTransform(signalIndex);
                UpdateCharts();
            });
        }
        hasActions = true;
    } else if (targetIndices.size() == 2) {
        if (hasActions) menu.addSeparator();
        menu.addAction(tr("创建差值图..."), this, [this, targetIndices]() {
            ShowDifferenceDialog(targetIndices);
        });
        hasActions = true;
    }

    if (hasActions) menu.addSeparator();
    menu.addAction(tr("取消所有展示信号"), this, [this]() {
        if (!signalTreeController_) return;
        signalTreeController_->SetAllSignalsChecked(false);
        displayGroupManager_.Clear();
        seriesProcessing_.ClearDerivedSignals();
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
    QVector<int> rawSignals;
    bool removedDerived = false;
    for (int index : indices) {
        if (index < 0) {
            seriesProcessing_.RemoveDerivedSignal(index);
            removedDerived = true;
        } else {
            rawSignals.append(index);
        }
    }
    if (removedDerived) {
        seriesProcessing_.RebuildDerived(liveMode_ ? liveSession_.SeriesData() : dataSession_.Series());
    }
    signalTreeController_->SetSignalsChecked(rawSignals, false);
    displayGroupManager_.RemoveSignals(rawSignals);
    UpdateCharts();
}

void MainWindow::HandleSplitRequested(const QVector<int>& indices) {
    displayGroupManager_.UnmergeSignals(indices);
    UpdateCharts();
}

void MainWindow::ShowSignalTransformDialog(int signalIndex) {
    if (!formatDocument_.HasFormat() || signalIndex < 0
        || signalIndex >= static_cast<int>(formatDocument_.Format().signalFormats.size())) return;
    const auto& signal = formatDocument_.Format().signalFormats[static_cast<size_t>(signalIndex)];
    SignalTransformDialog dialog(signal.name, seriesProcessing_.Transform(signalIndex), this);
    if (dialog.exec() != QDialog::Accepted) return;
    seriesProcessing_.SetTransform(signalIndex, dialog.Transform());
    UpdateCharts();
}

void MainWindow::ShowDifferenceDialog(const QVector<int>& suggestedSignals) {
    if (!formatDocument_.HasFormat()) return;
    const int initialLeft = suggestedSignals.value(0, 0);
    const int initialRight = suggestedSignals.value(1,
        formatDocument_.Format().signalFormats.size() > 1 ? 1 : 0);
    DifferenceDialog dialog(formatDocument_.Format(), initialLeft, initialRight, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QString error;
    if (!CreateDifference(dialog.LeftSignal(), dialog.RightSignal(), error)) {
        QMessageBox::information(this, tr("无法创建差值图"), error);
    }
}

bool MainWindow::CreateDifference(int leftSignal, int rightSignal, QString& errorMessage) {
    if (!seriesProcessing_.AddDifference(leftSignal, rightSignal, errorMessage)) return false;
    seriesProcessing_.RebuildDerived(liveMode_ ? liveSession_.SeriesData() : dataSession_.Series());
    UpdateCharts();
    return true;
}

#ifdef PAT_ENABLE_SERIAL
void MainWindow::SetupSerialDock() {
    auto* dock = new QDockWidget(tr("串口采集"), this);
    dock->setObjectName(QStringLiteral("serialCaptureDock"));
    auto* panel = new QWidget(dock);
    panel->setObjectName(QStringLiteral("serialPanel"));
    auto* form = new QFormLayout(panel);

    portCombo_ = new QComboBox(panel);
    auto* refreshButton = new QPushButton(tr("刷新"), panel);
    auto* portRow = new QWidget(panel);
    auto* portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->addWidget(portCombo_, 1);
    portLayout->addWidget(refreshButton);
    form->addRow(tr("串口"), portRow);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::RefreshSerialPorts);

    baudSpin_ = new QSpinBox(panel);
    baudSpin_->setRange(300, 4000000);
    baudSpin_->setValue(230400);
    form->addRow(tr("波特率"), baudSpin_);

    dataBitsCombo_ = new QComboBox(panel);
    for (int bits : {5, 6, 7, 8}) dataBitsCombo_->addItem(QString::number(bits), bits);
    dataBitsCombo_->setCurrentIndex(3);
    form->addRow(tr("数据位"), dataBitsCombo_);

    parityCombo_ = new QComboBox(panel);
    parityCombo_->addItem(tr("无"), static_cast<int>(QSerialPort::NoParity));
    parityCombo_->addItem(tr("偶校验"), static_cast<int>(QSerialPort::EvenParity));
    parityCombo_->addItem(tr("奇校验"), static_cast<int>(QSerialPort::OddParity));
    form->addRow(tr("校验"), parityCombo_);

    stopBitsCombo_ = new QComboBox(panel);
    stopBitsCombo_->addItem(tr("1 位"), static_cast<int>(QSerialPort::OneStop));
    stopBitsCombo_->addItem(tr("1.5 位"), static_cast<int>(QSerialPort::OneAndHalfStop));
    stopBitsCombo_->addItem(tr("2 位"), static_cast<int>(QSerialPort::TwoStop));
    form->addRow(tr("停止位"), stopBitsCombo_);

    flowControlCombo_ = new QComboBox(panel);
    flowControlCombo_->addItem(tr("无"), static_cast<int>(QSerialPort::NoFlowControl));
    flowControlCombo_->addItem(tr("硬件"), static_cast<int>(QSerialPort::HardwareControl));
    flowControlCombo_->addItem(tr("软件"), static_cast<int>(QSerialPort::SoftwareControl));
    form->addRow(tr("流控"), flowControlCombo_);

    timeoutSpin_ = new QSpinBox(panel);
    timeoutSpin_->setRange(0, 60000);
    timeoutSpin_->setValue(5000);
    timeoutSpin_->setSuffix(tr(" ms"));
    timeoutSpin_->setToolTip(tr("无数据告警时间；0 表示关闭告警"));
    form->addRow(tr("无数据告警"), timeoutSpin_);

    logDirectoryEdit_ = new QLineEdit(QDir::homePath() + QStringLiteral("/PAT Logs"), panel);
    auto* browseButton = new QPushButton(tr("选择"), panel);
    auto* logRow = new QWidget(panel);
    auto* logLayout = new QHBoxLayout(logRow);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->addWidget(logDirectoryEdit_, 1);
    logLayout->addWidget(browseButton);
    form->addRow(tr("日志目录"), logRow);
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getExistingDirectory(this, tr("选择日志目录"),
                                                                logDirectoryEdit_->text());
        if (!path.isEmpty()) logDirectoryEdit_->setText(path);
    });

    startCaptureButton_ = new QPushButton(tr("开始采集"), panel);
    stopCaptureButton_ = new QPushButton(tr("停止采集"), panel);
    pauseDisplayButton_ = new QPushButton(tr("暂停显示"), panel);
    followLatestCheck_ = new QCheckBox(tr("跟随最新采样"), panel);
    followLatestCheck_->setChecked(true);
    stopCaptureButton_->setEnabled(false);
    pauseDisplayButton_->setEnabled(false);
    auto* buttons = new QWidget(panel);
    auto* buttonLayout = new QHBoxLayout(buttons);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addWidget(startCaptureButton_);
    buttonLayout->addWidget(stopCaptureButton_);
    buttonLayout->addWidget(pauseDisplayButton_);
    form->addRow(buttons);
    form->addRow(tr("曲线"), followLatestCheck_);
    liveFrameTable_ = new QTableWidget(panel);
    liveFrameTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    liveFrameTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    liveFrameTable_->setMinimumHeight(170);
    form->addRow(tr("最近帧"), liveFrameTable_);
    connect(startCaptureButton_, &QPushButton::clicked, this, &MainWindow::StartSerialCapture);
    connect(stopCaptureButton_, &QPushButton::clicked, this, &MainWindow::StopSerialCapture);
    connect(pauseDisplayButton_, &QPushButton::clicked, this, [this]() {
        displayPaused_ = !displayPaused_;
        if (trajectoryView_) trajectoryView_->SetPaused(displayPaused_);
        pauseDisplayButton_->setText(displayPaused_ ? tr("恢复显示") : tr("暂停显示"));
        if (!displayPaused_ && liveSession_.HasData() && chartArea_
            && dataViewTabs_ && dataViewTabs_->currentIndex() == 0) {
            liveSession_.UpdateStatistics();
            chartArea_->SetStatistics(liveSession_.Statistics());
            if (!liveChartsInitialized_) {
                UpdateCharts();
                liveChartsInitialized_ = true;
            }
            chartArea_->RefreshLiveData(followLatestCheck_->isChecked());
        }
    });
    connect(followLatestCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled && !displayPaused_ && liveMode_ && liveSession_.HasData() && chartArea_
            && dataViewTabs_ && dataViewTabs_->currentIndex() == 0) {
            chartArea_->RefreshLiveData(true);
        }
    });

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (viewMenu_) {
        viewMenu_->addSeparator();
        viewMenu_->addAction(dock->toggleViewAction());
    }
    dock->hide();
    RefreshSerialPorts();
}

void MainWindow::RefreshSerialPorts() {
    if (!portCombo_) return;
    const QString selected = portCombo_->currentData().toString();
    portCombo_->clear();
    for (const auto& info : QSerialPortInfo::availablePorts()) {
        QString label = info.portName();
        if (!info.description().isEmpty()) label += QStringLiteral(" — %1").arg(info.description());
        portCombo_->addItem(label, info.portName());
    }
    const int index = portCombo_->findData(selected);
    if (index >= 0) portCombo_->setCurrentIndex(index);
}

void MainWindow::StartSerialCapture() {
    if (serialThread_) return;
    if (!formatDocument_.HasFormat() || !formatDocument_.Format().HasFraming()) {
        QMessageBox::information(this, tr("提示"), tr("请先加载带 framing 的协议模板"));
        return;
    }
    if (!portCombo_ || portCombo_->currentIndex() < 0) {
        QMessageBox::information(this, tr("提示"), tr("请连接串口设备并刷新端口列表"));
        return;
    }
    pat::SerialCaptureConfig config;
    config.portName = portCombo_->currentData().toString();
    config.baudRate = baudSpin_->value();
    config.dataBits = static_cast<QSerialPort::DataBits>(dataBitsCombo_->currentData().toInt());
    config.parity = static_cast<QSerialPort::Parity>(parityCombo_->currentData().toInt());
    config.stopBits = static_cast<QSerialPort::StopBits>(stopBitsCombo_->currentData().toInt());
    config.flowControl = static_cast<QSerialPort::FlowControl>(flowControlCombo_->currentData().toInt());
    config.inactivityTimeoutMs = timeoutSpin_->value();
    config.logDirectory = logDirectoryEdit_->text().trimmed();

    liveSession_.Reset(formatDocument_.Format());
    seriesProcessing_.ClearDerivedSamples();
    liveMode_ = true;
    displayPaused_ = false;
    if (trajectoryView_) trajectoryView_->SetPaused(false);
    liveChartsInitialized_ = false;
    pauseDisplayButton_->setText(tr("暂停显示"));
    followLatestCheck_->setChecked(true);
    if (liveFrameTable_) {
        QStringList headers = {tr("序号")};
        for (const auto& signal : formatDocument_.Format().signalFormats) headers.append(signal.name);
        liveFrameTable_->setColumnCount(headers.size());
        liveFrameTable_->setHorizontalHeaderLabels(headers);
        liveFrameTable_->setRowCount(0);
    }
    if (signalTreeController_->CollectCheckedSignalIndices().isEmpty()) {
        QVector<int> defaults;
        const int count = std::min(4, static_cast<int>(formatDocument_.Format().signalFormats.size()));
        for (int i = 0; i < count; ++i) defaults.append(i);
        signalTreeController_->SetSignalsChecked(defaults, true);
    }
    if (chartArea_) {
        chartArea_->ResetSessionView();
        chartArea_->SetSeries(&liveSession_.SeriesData());
        chartArea_->SetAxisTitle(tr("采样序号"));
    }
    UpdateCharts();
    liveChartsInitialized_ = true;

    qRegisterMetaType<QVector<pat::LiveSample>>();
    serialThread_ = new QThread(this);
    serialWorker_ = new pat::SerialCaptureWorker(formatDocument_.Format(), config);
    serialWorker_->moveToThread(serialThread_);
    connect(serialThread_, &QThread::started, serialWorker_, &pat::SerialCaptureWorker::Start);
    connect(serialWorker_, &pat::SerialCaptureWorker::Started, this,
            [this](const QString& raw, const QString& csv) {
                UpdateStatus(tr("串口已打开，等待有效帧；原始日志：%1；解析日志：%2").arg(raw, csv));
            });
    connect(serialWorker_, &pat::SerialCaptureWorker::SamplesReady,
            this, &MainWindow::HandleSerialSamples);
    connect(serialWorker_, &pat::SerialCaptureWorker::Progress, this,
            [this](quint64 bytes, quint64 frames, quint64 skipped) {
                if (captureStatsLabel_) {
                    captureStatsLabel_->setText(tr("已收到 %1 字节；已解析 %2 帧；跳过 %3 字节")
                                                     .arg(bytes).arg(frames).arg(skipped));
                }
            });
    connect(serialWorker_, &pat::SerialCaptureWorker::Status,
            this, [this](const QString& message) { UpdateStatus(message); });
    connect(serialWorker_, &pat::SerialCaptureWorker::Error,
            this, [this](const QString& message) { UpdateStatus(message); });
    connect(serialWorker_, &pat::SerialCaptureWorker::Stopped,
            this, &MainWindow::HandleSerialStopped);
    connect(serialWorker_, &pat::SerialCaptureWorker::Stopped,
            serialThread_, &QThread::quit);
    connect(serialThread_, &QThread::finished, serialWorker_, &QObject::deleteLater);
    QThread* thread = serialThread_;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (serialThread_ == thread) {
            serialThread_ = nullptr;
            serialWorker_ = nullptr;
            startCaptureButton_->setEnabled(true);
        }
        thread->deleteLater();
    });
    captureRunning_ = true;
    if (captureStatsLabel_) captureStatsLabel_->setText(tr("已收到 0 字节；已解析 0 帧；跳过 0 字节"));
    startCaptureButton_->setEnabled(false);
    stopCaptureButton_->setEnabled(true);
    pauseDisplayButton_->setEnabled(true);
    serialThread_->start();
}

void MainWindow::StopSerialCapture() {
    if (!serialWorker_) return;
    stopCaptureButton_->setEnabled(false);
    QMetaObject::invokeMethod(serialWorker_, "Stop", Qt::QueuedConnection);
}

void MainWindow::HandleSerialSamples(const QVector<pat::LiveSample>& samples, quint64 skippedBytes) {
    if (!liveMode_ || samples.isEmpty()) return;
    for (const auto& sample : samples) {
        liveSession_.Append(sample.index, sample.values);
        seriesProcessing_.AppendLive(sample.index, sample.values);
        if (liveFrameTable_) {
            if (liveFrameTable_->rowCount() >= 100) liveFrameTable_->removeRow(0);
            const int row = liveFrameTable_->rowCount();
            liveFrameTable_->insertRow(row);
            liveFrameTable_->setItem(row, 0,
                                     new QTableWidgetItem(QString::number(sample.index)));
            for (qsizetype i = 0; i < sample.values.size(); ++i) {
                liveFrameTable_->setItem(row, static_cast<int>(i + 1),
                                         new QTableWidgetItem(QString::number(sample.values[i], 'g', 12)));
            }
        }
    }
    if (liveFrameTable_) liveFrameTable_->scrollToBottom();
    UpdateStatus(tr("已解析 %1 帧；跳过 %2 字节")
                     .arg(samples.last().index + 1).arg(skippedBytes));
    if (trajectoryView_) trajectoryView_->RequestRefresh();
    if (displayPaused_ || (dataViewTabs_ && dataViewTabs_->currentIndex() != 0)) return;
    liveSession_.UpdateStatistics();
    if (chartArea_) {
        chartArea_->SetStatistics(liveSession_.Statistics());
        if (!liveChartsInitialized_) {
            UpdateCharts();
            liveChartsInitialized_ = true;
        } else {
            chartArea_->RefreshLiveData(followLatestCheck_->isChecked());
        }
    }
}

void MainWindow::HandleSerialStopped() {
    captureRunning_ = false;
    stopCaptureButton_->setEnabled(false);
    pauseDisplayButton_->setEnabled(false);
}
#endif
