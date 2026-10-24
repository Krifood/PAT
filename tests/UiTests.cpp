#include "ui/MainWindow.h"
#include "ui/MixedLoadController.h"
#include <QtEndian>
#include "ui/AboutDialog.h"
#include "ui/AppTheme.h"
#include "ui/ChartArea.h"
#include "ui/DisplayGroupManager.h"
#include "ui/SignalChartView.h"
#include "ui/SignalTreeWidget.h"
#include "ui/TrajectoryView.h"
#include "ui/TimeRangeControl.h"

#include <QComboBox>
#include <QCompleter>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressDialog>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QtCharts/QChart>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <cmath>
#include <QtTest>

#if defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif
#include <unistd.h>

namespace {

double RelativeLuminance(const QColor& color) {
    const auto linear = [](double component) {
        return component <= 0.04045 ? component / 12.92
                                    : std::pow((component + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF())
        + 0.0722 * linear(color.blueF());
}

double ContrastRatio(const QColor& first, const QColor& second) {
    const double a = RelativeLuminance(first);
    const double b = RelativeLuminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

}  // namespace

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void informationMenuShowsVersionAndBundledChangelog();
    void importVmcMenuOpensSourceDialog();
    void vmcScanReportsErrorsAndTailWarnings_data();
    void vmcScanReportsErrorsAndTailWarnings();
    void openFormatMenuLoadsFile();
    void liveChartsAppearBeforeFirstFrame();
    void mixedSignalLabelsUseGroupContext();
    void mergedCursorLabelsAvoidCollisionsAndKeepLegendCompact_data();
    void mergedCursorLabelsAvoidCollisionsAndKeepLegendCompact();
    void chartGroupsGuidesAndAxes();
    void guideShowsAllCrossingsAndKeepsCursor();
    void liveChartsLimitVisibleWork();
    void axisLabelsDistinguishSmallDeltas();
    void trajectoryTabPlotsOfflineCoordinates();
    void spatialPlotsKeepHistoryMergeAndApplyTransforms();
    void signalSearchOperatesOnMainSelection();
    void displayProcessingWorksWithLiveMergedCharts();
    void themesUpdateControlsAndCharts();
    void indexedLoadingCoalescesAndZooms();
    void timeRangeControlSupportsHandlesAndPanning();
    void spatialRangeUsesTimeAndRestoresHistory();
};

void UiTests::mixedSignalLabelsUseGroupContext() {
    MainWindow window;
    QString error;
    QVERIFY2(window.formatDocument_.LoadFromJsonText(QStringLiteral(R"({
      "schema_version":3,
      "template_id":"mixed_labels",
      "storage":{
        "kind":"indexed_stream_chunks",
        "version":1,
        "layout_sha256":"0000000000000000000000000000000000000000000000000000000000000000",
        "data_file":"mixed.patbin"
      },
      "timestamp":{"value_type":"uint64","unit":"s","scale":0.000000001},
      "streams":[{
        "stream_id":231,
        "source_message_id":"0X00E7",
        "name":"ah_nav",
        "payload_length":8,
        "endianness":"big",
        "signals":[
          {"name":"cur_pos_x","byte_offset":0,"value_type":"float32",
           "group":"AH_NAV [0X00E7]","unit":"m"},
          {"name":"cur_pos_y","byte_offset":4,"value_type":"float32",
           "group":"AH_NAV [0X00E7]","unit":"m"}
        ]
      }]
    })"), error), qPrintable(error));
    window.BuildSignalTree();

    const auto& format = window.formatDocument_.Format();
    QCOMPARE(format.signalFormats[0].name, QStringLiteral("ah_nav.cur_pos_x"));
    QCOMPARE(format.signalFormats[0].DisplayName(), QStringLiteral("cur_pos_x"));
    auto* groupItem = window.signalTree_->topLevelItem(0);
    QVERIFY(groupItem);
    auto* signalItem = groupItem->child(0);
    QVERIFY(signalItem);
    QCOMPARE(signalItem->text(0), QStringLiteral("cur_pos_x"));

    DisplayGroupManager manager;
    manager.UpdateGroups({0}, format);
    QCOMPARE(manager.Groups().size(), 1);
    const QString expectedTitle = QStringLiteral("AH_NAV [0X00E7] - cur_pos_x");
    QCOMPARE(manager.Groups().first().title, expectedTitle);

    QVector<pat::Series> source(1);
    source[0].name = expectedTitle;
    source[0].unit = QStringLiteral("m");
    source[0].samples = {{0.0, 1.0}, {1.0, 2.0}};
    SignalChartView view;
    view.resize(900, 300);
    view.Configure(expectedTitle, source[0].unit, QStringLiteral("s"), false,
                   {QColor(Qt::cyan)}, {0}, {source[0].samples}, &source,
                   0.0, 3.0, 0.0, 1.0, 0);
    view.show();
    QTest::qWait(50);

    QGraphicsSimpleTextItem* titleItem = nullptr;
    for (auto* item : view.chart()->scene()->items()) {
        auto* label = dynamic_cast<QGraphicsSimpleTextItem*>(item);
        if (label && label->text() == expectedTitle) {
            titleItem = label;
            break;
        }
    }
    QVERIFY(titleItem);
    const QRectF plotArea = view.chart()->plotArea();
    QVERIFY(qAbs(titleItem->pos().x() - (plotArea.left() + 4.0)) < 0.5);
    QVERIFY(qAbs(titleItem->pos().y() - (plotArea.top() + 2.0)) < 0.5);
}

void UiTests::mergedCursorLabelsAvoidCollisionsAndKeepLegendCompact_data() {
    QTest::addColumn<int>("signalCount");
    QTest::newRow("two") << 2;
    QTest::newRow("three") << 3;
    QTest::newRow("six") << 6;
}

void UiTests::mergedCursorLabelsAvoidCollisionsAndKeepLegendCompact() {
    QFETCH(int, signalCount);
    QVector<pat::Series> source(signalCount);
    const QString group = QStringLiteral("超长混合数据分组 [0X00E7]");
    for (int i = 0; i < source.size(); ++i) {
        source[i].name = QStringLiteral("%1 - signal_%2").arg(group).arg(i + 1);
        source[i].unit = QStringLiteral("m/s");
        const double value = 5.0 + i * 0.002;
        source[i].samples = {{0.0, value}, {1.0, value}};
    }

    SignalChartView view;
    view.resize(760, 260);
    const QVector<QColor> colors = {QColor("#5ac8ff"), QColor("#ff8c00"),
                                    QColor("#78dc78")};
    QVector<int> indices;
    QVector<QVector<QPointF>> samples;
    QVector<const pat::Series*> sources;
    for (int i = 0; i < source.size(); ++i) {
        indices.append(i);
        samples.append(source[i].samples);
        sources.append(&source[i]);
    }
    view.Configure(QStringLiteral("合并信号"), QStringLiteral("m/s"), QStringLiteral("s"), true,
                   colors, indices, samples, &source,
                   0.0, 10.0, 0.0, 1.0, 0);
    view.SetDisplaySources(sources, {});
    view.show();
    QTest::qWait(50);
    view.SetCursorX(0.5);

    QVector<QRectF> valueRects;
    QSet<QRgb> labelColors;
    for (auto* item : view.chart()->scene()->items()) {
        auto* label = dynamic_cast<QGraphicsSimpleTextItem*>(item);
        if (!label || !label->isVisible() || !label->text().startsWith(QStringLiteral("k:"))) {
            continue;
        }
        QVERIFY(!label->text().contains(QLatin1Char('\n')));
        QCOMPARE(label->pen().style(), Qt::NoPen);
        valueRects.append(label->sceneBoundingRect());
        labelColors.insert(label->brush().color().rgb());
    }
    QCOMPARE(valueRects.size(), signalCount);
    QCOMPARE(labelColors, QSet<QRgb>{QColor("#ffff00").rgb()});
    for (int i = 0; i < valueRects.size(); ++i) {
        for (int j = i + 1; j < valueRects.size(); ++j) {
            QVERIFY2(!valueRects[i].intersects(valueRects[j]),
                     "merged cursor value labels must not overlap");
        }
    }

    for (auto* plotted : view.chart()->series()) {
        QVERIFY(!plotted->name().contains(group));
        QVERIFY(plotted->name().startsWith(QStringLiteral("signal_")));
    }
    QGraphicsItem* legend = nullptr;
    for (auto* item : view.chart()->childItems()) {
        if (item->data(0).toString() == QStringLiteral("signalLegend")) legend = item;
    }
    QVERIFY(legend);
    QVERIFY(legend->isVisible());
    const qreal expectedMaxWidth = std::max(
        72.0, std::min<qreal>(240.0, view.chart()->plotArea().width() * 0.3));
    QVERIFY(legend->boundingRect().width() <= expectedMaxWidth + 1.0);
    QVector<QRectF> rows;
    for (auto* item : legend->childItems()) {
        if (!item->isVisible() || item->data(0).toString() != QStringLiteral("legendLabel")) continue;
        const auto* label = dynamic_cast<QGraphicsSimpleTextItem*>(item);
        QVERIFY(label);
        QVERIFY(!label->text().contains(QChar(0x2026)));
        rows.append(item->mapRectToParent(item->boundingRect()));
        QVERIFY(legend->boundingRect().contains(rows.last()));
    }
    QCOMPARE(rows.size(), source.size());
    std::sort(rows.begin(), rows.end(), [](const QRectF& a, const QRectF& b) {
        return a.top() < b.top();
    });
    for (int i = 1; i < rows.size(); ++i) QVERIFY(rows[i].top() > rows[i - 1].bottom());
    const qreal topPadding = rows.first().top() - legend->boundingRect().top();
    const qreal bottomPadding = legend->boundingRect().bottom() - rows.last().bottom();
    QVERIFY(qAbs(topPadding - bottomPadding) < 1.0);
}

void UiTests::importVmcMenuOpensSourceDialog() {
    MainWindow window;
    window.show();
    auto* action = window.findChild<QAction*>(QStringLiteral("importVmcLogAction"));
    QVERIFY(action);
    QVERIFY(action->isEnabled());

    bool dialogObserved = false;
    QTimer::singleShot(0, &window, [&]() {
        auto* dialog = window.findChild<QFileDialog*>(QStringLiteral("vmcSourceDialog"));
        if (!dialog) return;
        dialogObserved = dialog->isVisible()
            && !dialog->testOption(QFileDialog::DontUseNativeDialog)
            && dialog->fileMode() == QFileDialog::ExistingFiles;
        dialog->reject();
    });
    action->trigger();
    QVERIFY(dialogObserved);
}

void UiTests::vmcScanReportsErrorsAndTailWarnings_data() {
    QTest::addColumn<int>("kind");
    QTest::newRow("invalid-file-is-not-user-cancellation") << 0;
    QTest::newRow("normal-file-confirms") << 1;
    QTest::newRow("partial-tail-warning-before-conversion") << 2;
    QTest::newRow("user-cancel-remains-cancel") << 3;
}

void UiTests::vmcScanReportsErrorsAndTailWarnings() {
    QFETCH(int, kind);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QByteArray bytes(84, '\0');
    bytes.append(QByteArray::fromHex("a5012301"));
    QByteArray topic(16, '\0'); topic.replace(0, 6, "sensor");
    bytes.append(topic);
    const QByteArray fields("value@u8");
    bytes.append(char(0)); bytes.append(char(fields.size())); bytes.append(fields);
    bytes.append(QByteArray::fromHex("5a01230100000000000003e80107"));
    if (kind == 3) bytes.append(QByteArray::fromHex("5a01230100000000000003e80107").repeated(2000000));
    if (kind == 2) {
        bytes.append(QByteArray::fromHex("5a01230100000121f148ffffff"));
        bytes.append(QByteArray(53, char(0xFF)));
    }
    if (kind == 0) bytes = "invalid";
    const QString path = dir.filePath("source.bin");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), qint64(bytes.size())); file.close();

    MainWindow window;
    window.show();
    bool selected = false, confirmed = false, timedOut = false, cancelled = false;
    QString errorText, warningText;
    QTimer dialogs;
    connect(&dialogs, &QTimer::timeout, &window, [&] {
        if (auto* picker = window.findChild<QFileDialog*>("vmcSourceDialog")) {
            if (picker->isVisible() && !selected) {
                selected = true;
                picker->setDirectory(dir.path());
                picker->selectFile(path);
                QMetaObject::invokeMethod(picker, "accept", Qt::DirectConnection);
                return;
            }
        }
        if (kind == 3 && !cancelled) {
            if (auto* progress = window.findChild<QProgressDialog*>("vmcScanProgress")) {
                if (progress->isVisible()) {
                    cancelled = true;
                    progress->close(); // Simulate the user's close-window action.
                    return;
                }
            }
        }
        for (auto* message : window.findChildren<QMessageBox*>()) {
            if (message->isVisible()) {
                errorText = message->text();
                message->accept();
                return;
            }
        }
        if (auto* inspection = window.findChild<QDialog*>("vmcInspectionDialog")) {
            if (inspection->isVisible()) {
                confirmed = true;
                if (auto* warning = inspection->findChild<QLabel*>("vmcTailWarning"))
                    warningText = warning->text();
                inspection->reject(); // Read-only test: never choose output files.
            }
        }
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &window, [&] {
        timedOut = true;
        for (auto* dialog : window.findChildren<QDialog*>()) dialog->reject();
    });
    dialogs.start(10);
    timeout.start(5000);
    window.ImportVmcLog();
    dialogs.stop(); timeout.stop();
    QVERIFY(!timedOut);
    QVERIFY(selected);
    if (kind == 3) {
        QVERIFY(cancelled);
        QVERIFY(!confirmed);
        QVERIFY(errorText.isEmpty());
        QVERIFY(window.statusLabel_->text().contains(QStringLiteral("扫描已取消")));
        return;
    }
    QVERIFY(!window.statusLabel_->text().contains(QStringLiteral("扫描已取消")));
    if (kind == 0) {
        QVERIFY2(errorText.contains(QStringLiteral("日志过短")), qPrintable(errorText));
        QVERIFY(!confirmed);
    } else {
        QVERIFY2(errorText.isEmpty(), qPrintable(errorText));
        QVERIFY(confirmed);
        if (kind == 2) {
            QVERIFY(warningText.contains(QStringLiteral("不完整记录 1 条")));
            QVERIFY(warningText.contains(QStringLiteral("66 字节")));
            QVERIFY(warningText.contains(QStringLiteral("偏移 0x")));
        } else QVERIFY(warningText.isEmpty());
    }
    QVERIFY(!QFileInfo::exists(dir.filePath("source.patbin")));
}

void UiTests::indexedLoadingCoalescesAndZooms() {
    QTemporaryDir dir;
    QFile file(dir.filePath("large.bin"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray bytes(800000, '\0');
    for (int i = 0; i < 100000; ++i) {
        qToLittleEndian<quint32>(quint32(i), bytes.data()+i*8);
        qToLittleEndian<quint32>(quint32(i*2), bytes.data()+i*8+4);
    }
    file.write(bytes); file.close();
    MainWindow window;
    QString error;
    QVERIFY2(window.formatDocument_.LoadFromJsonText(QStringLiteral(R"({"record_size":8,"signals":[
        {"name":"x","byte_offset":0,"value_type":"uint32"},
        {"name":"y","byte_offset":4,"value_type":"uint32"}]})"), error), qPrintable(error));
    QVERIFY2(window.mixedDataSession_.Open(file.fileName(), window.formatDocument_.Format(), error), qPrintable(error));
    window.mixedMode_ = true;
    window.BuildSignalTree();
    window.signalTreeController_->SetSignalsChecked({0}, true);
    window.UpdateCharts();
    QTRY_COMPARE_WITH_TIMEOUT(window.mixedDataSession_.Series()[0].samples.size(), 100000, 10000);
    auto charts = window.chartArea_->findChildren<SignalChartView*>();
    QVERIFY(!charts.isEmpty());
    SignalChartView* original = charts.last();
    QVERIFY(QMetaObject::invokeMethod(window.chartArea_, "HandleXRangeRequested", Qt::DirectConnection,
        Q_ARG(double,100.0), Q_ARG(double,110.0)));
    QVERIFY(QMetaObject::invokeMethod(window.chartArea_, "HandleXRangeRequested", Qt::DirectConnection,
        Q_ARG(double,200.0), Q_ARG(double,205.0)));
    QTRY_COMPARE_WITH_TIMEOUT(window.mixedDataSession_.Series()[0].samples.size(), 6, 10000);
    QCOMPARE(window.mixedDataSession_.Series()[0].samples.first(), QPointF(200,200));
    QVERIFY(window.chartArea_->findChildren<SignalChartView*>().contains(original));
    QCOMPARE(window.mixedDataSession_.Statistics().maxX, 99999.0);
    QVERIFY(window.timeRangeControl_->isEnabled());
    auto* rangeStart = window.timeRangeControl_->findChild<QDoubleSpinBox*>("timeRangeStart");
    auto* rangeEnd = window.timeRangeControl_->findChild<QDoubleSpinBox*>("timeRangeEnd");
    QVERIFY(rangeStart && rangeEnd);
    QCOMPARE(rangeStart->value(), 200.0);
    QCOMPARE(rangeEnd->value(), 205.0);
    QVERIFY(window.CreateDifference(0,1,error));
    QTRY_COMPARE_WITH_TIMEOUT(window.seriesProcessing_.DerivedSeries()[0].samples.size(), 6, 10000);
    QCOMPARE(window.seriesProcessing_.DerivedSeries()[0].samples.first(), QPointF(200,-200));
    const auto chartsBeforeTab = window.chartArea_->findChildren<SignalChartView*>();
    window.dataViewTabs_->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(!window.mixedLoader_->Busy(), 10000);
    auto plots = window.trajectoryView_->findChildren<TrajectoryPlotWidget*>();
    QVERIFY(!plots.isEmpty());
    QCOMPARE(plots.first()->HistoryPointCount(), 6);
    // Changing the shared control on the spatial tab must reload only that interval.
    rangeStart->setValue(201);
    QVERIFY(QMetaObject::invokeMethod(rangeStart, "editingFinished", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!window.mixedLoader_->Busy(), 10000);
    QCOMPARE(plots.first()->HistoryPointCount(), 5);
    QCOMPARE(window.mixedDataSession_.Series()[0].samples.first().x(), 201.0);
    QCOMPARE(window.mixedDataSession_.Series()[0].samples.last().x(), 205.0);
    const auto history = plots.first()->HistoryPointCount();
    window.dataViewTabs_->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(!window.mixedLoader_->Busy(), 10000);
    window.trajectoryView_->RefreshNow();
    QCOMPARE(plots.first()->HistoryPointCount(), history);
    QCOMPARE(window.chartArea_->findChildren<SignalChartView*>(), chartsBeforeTab);

    MixedLoadController loader;
    int completed = 0;
    connect(&loader, &MixedLoadController::Loaded, &window, [&](const pat::MixedDataSession&) { ++completed; });
    loader.Request(window.mixedDataSession_, {0}, {});
    loader.Reset();
    QTest::qWait(150);
    QCOMPARE(completed, 0);
}

void UiTests::themesUpdateControlsAndCharts() {
    QCOMPARE(AvailableThemeIds().size(), 3);
    QCOMPARE(ThemeIdFromKey(QStringLiteral("unknown")), AppThemeId::IndoorDark);
    for (AppThemeId id : AvailableThemeIds()) {
        const auto& theme = ThemeFor(id);
        QVERIFY(ContrastRatio(theme.text, theme.windowBackground) >= 7.0);
        QCOMPARE(theme.seriesColors.size(), 8);
        for (const auto& color : theme.seriesColors) {
            QVERIFY(ContrastRatio(color, theme.plotBackgroundEven) >= 4.0);
            QVERIFY(ContrastRatio(color, theme.plotBackgroundOdd) >= 4.0);
        }
    }

    QSettings settings(QStringLiteral("PAT"), QStringLiteral("PAT"));
    const bool hadStoredTheme = settings.contains(QStringLiteral("appearance/theme"));
    const QVariant storedTheme = settings.value(QStringLiteral("appearance/theme"));
    const auto restoreSettings = qScopeGuard([&settings, hadStoredTheme, storedTheme]() {
        if (hadStoredTheme) settings.setValue(QStringLiteral("appearance/theme"), storedTheme);
        else settings.remove(QStringLiteral("appearance/theme"));
        settings.sync();
    });
    Q_UNUSED(restoreSettings)

    MainWindow window;
    QString error;
    const QString root = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(window.formatDocument_.LoadFromFile(
                 root + QStringLiteral("/samples/cp01_b3_navigation.json"), error),
             qPrintable(error));
    window.BuildSignalTree();
    QVERIFY2(window.dataSession_.Load(root + QStringLiteral("/samples/cp01_b3_synthetic.hex"),
                                     window.formatDocument_.Format(), error), qPrintable(error));
    window.signalTreeController_->SetSignalsChecked({0}, true);
    window.UpdateCharts();
    window.show();

    window.ApplyTheme(AppThemeId::OutdoorHighContrast, false);
    const auto& outdoor = ThemeFor(AppThemeId::OutdoorHighContrast);
    QCOMPARE(window.currentThemeId_, AppThemeId::OutdoorHighContrast);
    auto* outdoorAction = window.findChild<QAction*>(
        QStringLiteral("themeAction_outdoor_high_contrast"));
    QVERIFY(outdoorAction);
    QVERIFY(outdoorAction->isChecked());
    QVERIFY(window.styleSheet().contains(outdoor.windowBackground.name()));
    QVERIFY(window.styleSheet().contains(QStringLiteral("QToolButton")));
    window.ApplyTheme(AppThemeId::OutdoorHighContrast, true);
    settings.sync();
    QCOMPARE(settings.value(QStringLiteral("appearance/theme")).toString(),
             ThemeKey(AppThemeId::OutdoorHighContrast));
    MainWindow restoredWindow;
    QCOMPARE(restoredWindow.currentThemeId_, AppThemeId::OutdoorHighContrast);

    auto views = window.chartArea_->findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 1);
    QCOMPARE(views.first()->chart()->backgroundBrush().color(), outdoor.chartBackgroundEven);
    auto* line = qobject_cast<QLineSeries*>(views.first()->chart()->series().first());
    QVERIFY(line);
    QCOMPARE(line->pen().color(), outdoor.seriesColors.first());
    QCOMPARE(line->pen().widthF(), outdoor.seriesLineWidth);
    auto* yAxis = qobject_cast<QValueAxis*>(views.first()->chart()->axes(Qt::Vertical).first());
    QVERIFY(yAxis);
    QCOMPARE(yAxis->labelsColor(), outdoor.axisText);

    auto* trajectoryChart = window.findChild<QChartView*>(QStringLiteral("trajectoryChart"));
    QVERIFY(trajectoryChart);
    QCOMPARE(trajectoryChart->chart()->plotAreaBackgroundBrush().color(),
             outdoor.plotBackgroundEven);

    window.ApplyTheme(AppThemeId::IndoorLight, false);
    const auto& indoorLight = ThemeFor(AppThemeId::IndoorLight);
    views = window.chartArea_->findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 1);
    QCOMPARE(views.first()->chart()->backgroundBrush().color(),
             indoorLight.chartBackgroundEven);
    auto* lightAction = window.findChild<QAction*>(QStringLiteral("themeAction_indoor_light"));
    QVERIFY(lightAction);
    QVERIFY(lightAction->isChecked());
}

void UiTests::displayProcessingWorksWithLiveMergedCharts() {
    MainWindow window;
    QString error;
    const QString root = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(window.formatDocument_.LoadFromFile(
                 root + QStringLiteral("/samples/cp01_b3_navigation.json"), error),
             qPrintable(error));
    window.BuildSignalTree();
    window.liveSession_.Reset(window.formatDocument_.Format());
    window.liveMode_ = true;
    window.signalTreeController_->SetSignalsChecked({0, 1}, true);
    window.displayGroupManager_.MergeSignals({0, 1}, window.formatDocument_.Format(), error);
    window.seriesProcessing_.SetTransform(0, {2.0, 10.0});
    QVERIFY2(window.CreateDifference(0, 1, error), qPrintable(error));
    QVERIFY2(window.CreateSum(0, 1, error), qPrintable(error));

    QVector<double> values(
        static_cast<qsizetype>(window.formatDocument_.Format().signalFormats.size()), 0.0);
    values[0] = 3.0;
    values[1] = 1.0;
    QVERIFY(window.liveSession_.Append(0, values));
    window.seriesProcessing_.AppendLive(0, values);
    values[0] = 5.0;
    values[1] = 2.0;
    QVERIFY(window.liveSession_.Append(1, values));
    window.seriesProcessing_.AppendLive(1, values);
    window.liveSession_.UpdateStatistics();
    window.UpdateCharts();
    window.show();

    auto views = window.chartArea_->findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 3);
    SignalChartView* merged = nullptr;
    SignalChartView* difference = nullptr;
    SignalChartView* sum = nullptr;
    for (auto* view : views) {
        if (view->SeriesIndices() == QVector<int>({0, 1})) merged = view;
        if (view->SeriesIndices() == QVector<int>({-1})) difference = view;
        if (view->SeriesIndices() == QVector<int>({-2})) sum = view;
    }
    QVERIFY(merged);
    QVERIFY(difference);
    QVERIFY(sum);
    const auto mergedLines = merged->chart()->series();
    QCOMPARE(mergedLines.size(), 2);
    auto* transformed = qobject_cast<QLineSeries*>(mergedLines[0]);
    auto* unchanged = qobject_cast<QLineSeries*>(mergedLines[1]);
    QVERIFY(transformed);
    QVERIFY(unchanged);
    QCOMPARE(transformed->points(), (QList<QPointF>{{0, 16}, {1, 20}}));
    QCOMPARE(unchanged->points(), (QList<QPointF>{{0, 1}, {1, 2}}));
    QVERIFY(transformed->name().contains(QStringLiteral("×2")));

    auto* differenceLine = qobject_cast<QLineSeries*>(difference->chart()->series().first());
    QVERIFY(differenceLine);
    QCOMPARE(differenceLine->points(), (QList<QPointF>{{0, 2}, {1, 3}}));
    QVERIFY(differenceLine->name().contains(QStringLiteral("滚转角 - 俯仰角")));
    QVERIFY(differenceLine->name().contains(QStringLiteral("来源")));

    auto* sumLine = qobject_cast<QLineSeries*>(sum->chart()->series().first());
    QVERIFY(sumLine);
    QCOMPARE(sumLine->points(), (QList<QPointF>{{0, 4}, {1, 7}}));
    QVERIFY(sumLine->name().contains(QStringLiteral("滚转角 + 俯仰角")));
    QVERIFY(sumLine->name().contains(QStringLiteral("来源")));

    merged->SetCursorX(1.0);
    bool sawTransformedCursor = false;
    for (auto* item : merged->chart()->scene()->items()) {
        auto* label = dynamic_cast<QGraphicsSimpleTextItem*>(item);
        if (label && label->text().contains(QStringLiteral("v:20.0000"))) {
            sawTransformedCursor = true;
        }
    }
    QVERIFY(sawTransformedCursor);

    values[0] = 9.0;
    values[1] = 4.0;
    QVERIFY(window.liveSession_.Append(2, values));
    window.seriesProcessing_.AppendLive(2, values);
    window.liveSession_.UpdateStatistics();
    window.chartArea_->SetStatistics(window.liveSession_.Statistics());
    window.chartArea_->RefreshLiveData(true);
    QCOMPARE(transformed->points().last(), QPointF(2, 28));
    QCOMPARE(differenceLine->points().last(), QPointF(2, 5));
    QCOMPARE(sumLine->points().last(), QPointF(2, 13));
}

void UiTests::spatialPlotsKeepHistoryMergeAndApplyTransforms() {
    pat::FormatDefinition format;
    QString error;
    const QString root = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(pat::LoadFormatFromJson(root + QStringLiteral("/samples/cp01_b3_navigation.json"),
                                     format, error), qPrintable(error));

    QVector<pat::Series> source(static_cast<qsizetype>(format.signalFormats.size()));
    for (int signal = 0; signal < source.size(); ++signal) {
        source[signal].name = format.signalFormats[static_cast<size_t>(signal)].name;
        source[signal].unit = format.signalFormats[static_cast<size_t>(signal)].unit;
    }

    TrajectoryView view;
    view.resize(1000, 800);
    view.ConfigureSignals(format);
    auto plots = view.findChildren<TrajectoryPlotWidget*>();
    QCOMPARE(plots.size(), 1);
    auto* first2D = plots.first();
    auto* firstRemove = first2D->findChild<QToolButton*>(
        QStringLiteral("removeTrajectoryPlot_%1").arg(first2D->PlotId()));
    QVERIFY(firstRemove);
    QVERIFY(firstRemove->isEnabled());
    QVERIFY(firstRemove->text().contains(QStringLiteral("删除子图")));
    QVERIFY(firstRemove->minimumWidth() >= 100);
    auto* second2D = view.AddPlot(TrajectoryDimension::TwoD);
    auto* plot3D = view.AddPlot(TrajectoryDimension::ThreeD);
    QCOMPARE(view.PlotCount(), 3);
    QCOMPARE(first2D->Dimension(), TrajectoryDimension::TwoD);
    QCOMPARE(plot3D->Dimension(), TrajectoryDimension::ThreeD);
    QVERIFY(plot3D->findChild<QWidget*>(QStringLiteral("trajectory3DCanvas_%1")
                                           .arg(plot3D->PlotId())));

    const auto appendWindow = [&source](int begin) {
        for (int signal = 0; signal < source.size(); ++signal) {
            source[signal].samples.clear();
            source[signal].samples.append(QPointF(begin, signal * 100.0 + begin));
            source[signal].samples.append(QPointF(begin + 1, signal * 100.0 + begin + 1));
        }
    };
    appendWindow(0);
    view.SetDataSource(&source, pat::TrajectoryPairing::SampleIndex);
    QCOMPARE(first2D->HistoryPointCount(), 2);
    QCOMPARE(second2D->HistoryPointCount(), 2);
    QCOMPARE(plot3D->HistoryPointCount(), 2);

    // Simulate the live source ring buffer dropping old samples. Each spatial subplot
    // must retain its own complete session history.
    appendWindow(2);
    view.RequestRefresh();
    QCOMPARE(first2D->HistoryPointCount(), 4);
    QCOMPARE(second2D->HistoryPointCount(), 4);
    QCOMPARE(plot3D->HistoryPointCount(), 4);
    view.SetPaused(true);
    appendWindow(4);
    view.RequestRefresh();
    QCOMPARE(first2D->HistoryPointCount(), 6);
    QCOMPARE(second2D->HistoryPointCount(), 6);
    QCOMPARE(plot3D->HistoryPointCount(), 6);

    const int targetId = first2D->PlotId();
    QVERIFY2(view.MergePlots(second2D->PlotId(), targetId, error), qPrintable(error));
    QCOMPARE(view.PlotCount(), 2);
    QCOMPARE(first2D->TrackCount(), 2);
    QCOMPARE(first2D->HistoryPointCount(0), 6);
    QCOMPARE(first2D->HistoryPointCount(1), 6);
    QVERIFY(!view.MergePlots(plot3D->PlotId(), targetId, error));
    QVERIFY(error.contains(QStringLiteral("不能合并")));
    QCOMPARE(view.PlotCount(), 2);

    auto* xCombo = first2D->findChild<QComboBox*>(QStringLiteral("trajectoryXCombo"));
    QVERIFY(xCombo);
    const int xSignal = xCombo->currentData().toInt();
    QVERIFY(xSignal >= 0);
    QHash<int, pat::DisplayTransform> transforms;
    transforms.insert(xSignal, {2.0, 10.0});
    view.SetDisplayTransforms(transforms);
    auto* chartView = first2D->findChild<QChartView*>();
    QVERIFY(chartView);
    QLineSeries* firstLine = nullptr;
    for (auto* series : chartView->chart()->series()) {
        if ((firstLine = qobject_cast<QLineSeries*>(series))) break;
    }
    QVERIFY(firstLine);
    QCOMPARE(firstLine->points().size(), 6);
    QCOMPARE(firstLine->points().first().x(), xSignal * 200.0 + 10.0);
    QCOMPARE(first2D->HistoryPointCount(), 6);

    auto* remove3D = plot3D->findChild<QToolButton*>(
        QStringLiteral("removeTrajectoryPlot_%1").arg(plot3D->PlotId()));
    QVERIFY(remove3D);
    remove3D->click();
    QCOMPARE(view.PlotCount(), 1);
    firstRemove->click();
    QCOMPARE(view.PlotCount(), 0);
    auto* emptyState = view.findChild<QLabel*>(QStringLiteral("emptyTrajectoryState"));
    QVERIFY(emptyState);
    QVERIFY(!emptyState->isHidden());
    QCOMPARE(view.AddPlot(TrajectoryDimension::ThreeD)->Dimension(),
             TrajectoryDimension::ThreeD);
    QCOMPARE(view.PlotCount(), 1);
}

void UiTests::signalSearchOperatesOnMainSelection() {
    MainWindow window;
    QString error;
    const QString root = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(window.formatDocument_.LoadFromFile(
                 root + QStringLiteral("/samples/cp01_b3_navigation.json"), error),
             qPrintable(error));
    window.BuildSignalTree();

    window.signalSearchEdit_->setText(QStringLiteral("组合 经度"));
    QCOMPARE(window.signalSearchResults_->topLevelItemCount(), 1);
    auto* result = window.signalSearchResults_->topLevelItem(0);
    const int signalIndex = result->data(0, SignalTreeWidget::SIGNAL_INDEX_ROLE).toInt();
    QVERIFY(signalIndex >= 0);
    result->setCheckState(0, Qt::Checked);
    QVERIFY(window.signalTreeController_->CollectCheckedSignalIndices().contains(signalIndex));

    window.seriesProcessing_.SetTransform(signalIndex, {1.0, 25.0});
    window.signalSearchEdit_->clear();
    QCOMPARE(window.signalSearchResults_->topLevelItemCount(), 0);
    QVERIFY(window.signalTreeController_->CollectCheckedSignalIndices().contains(signalIndex));
    QCOMPARE(window.seriesProcessing_.Transform(signalIndex).offset, 25.0);

    window.signalSearchEdit_->setText(QStringLiteral("COMBINED-NAME-NOT-PRESENT"));
    QCOMPARE(window.signalSearchResults_->topLevelItemCount(), 0);
    window.signalSearchEdit_->setText(QStringLiteral("角"));
    QVERIFY(window.signalSearchResults_->topLevelItemCount() > 1);
}

void UiTests::trajectoryTabPlotsOfflineCoordinates() {
    MainWindow window;
    QString error;
    const QString root = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(window.formatDocument_.LoadFromFile(
                 root + QStringLiteral("/samples/cp01_b3_navigation.json"), error),
             qPrintable(error));
    window.BuildSignalTree();
    QVERIFY2(window.dataSession_.Load(root + QStringLiteral("/samples/cp01_b3_synthetic.hex"),
                                      window.formatDocument_.Format(), error), qPrintable(error));
    window.UpdateCharts();
    window.show();

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("dataViewTabs"));
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);
    auto* view = window.findChild<TrajectoryView*>(QStringLiteral("trajectoryView"));
    QVERIFY(view);
    auto* xCombo = view->findChild<QComboBox*>(QStringLiteral("trajectoryXCombo"));
    auto* yCombo = view->findChild<QComboBox*>(QStringLiteral("trajectoryYCombo"));
    auto* groupCombo = view->findChild<QComboBox*>(QStringLiteral("trajectoryGroupCombo"));
    QVERIFY(xCombo);
    QVERIFY(yCombo);
    QVERIFY(groupCombo);
    QVERIFY(groupCombo->isEditable());
    QVERIFY(xCombo->isEditable());
    QCOMPARE(xCombo->currentText(), QStringLiteral("组合经度 (°)"));
    QCOMPARE(yCombo->currentText(), QStringLiteral("组合纬度 (°)"));
    QCOMPARE(groupCombo->currentData().toString(), QStringLiteral("组合导航/位置"));

    auto* chartView = view->findChild<QChartView*>(QStringLiteral("trajectoryChart"));
    QVERIFY(chartView);
    auto* path = qobject_cast<QLineSeries*>(chartView->chart()->series().first());
    QVERIFY(path);
    QTRY_COMPARE(path->points().size(), 1);
    const int xIndex = xCombo->currentData().toInt();
    const int yIndex = yCombo->currentData().toInt();
    QCOMPARE(path->points().first().x(), window.dataSession_.Series()[xIndex].samples.first().y());
    QCOMPARE(path->points().first().y(), window.dataSession_.Series()[yIndex].samples.first().y());

    const int gnssGroup = groupCombo->findData(QStringLiteral("GNSS/位置"));
    QVERIFY(gnssGroup >= 0);
    groupCombo->setCurrentIndex(gnssGroup);
    QCOMPARE(xCombo->count(), 5);  // Placeholder plus four position signals.
    xCombo->completer()->setCompletionPrefix(QStringLiteral("经度"));
    QVERIFY(xCombo->completer()->completionCount() >= 1);
    const int gnssX = xCombo->findText(QStringLiteral("GNSS经度 (°)"));
    const int gnssY = yCombo->findText(QStringLiteral("GNSS纬度 (°)"));
    QVERIFY(gnssX >= 0);
    QVERIFY(gnssY >= 0);
    xCombo->setCurrentIndex(gnssX);
    yCombo->setCurrentIndex(gnssY);
    QCOMPARE(path->points().first().x(),
             window.dataSession_.Series()[xCombo->currentData().toInt()].samples.first().y());
    QCOMPARE(path->points().first().y(),
             window.dataSession_.Series()[yCombo->currentData().toInt()].samples.first().y());
}

void UiTests::informationMenuShowsVersionAndBundledChangelog() {
    MainWindow window;
    QVERIFY(window.findChild<QAction*>(QStringLiteral("importVmcLogAction")));
    window.show();

    QMenu* helpMenu = nullptr;
    for (auto* action : window.menuBar()->actions()) {
        if (action->text() == QStringLiteral("帮助")) helpMenu = action->menu();
    }
    QVERIFY(helpMenu);

    QAction* versionAction = nullptr;
    QAction* changelogAction = nullptr;
    for (auto* action : helpMenu->actions()) {
        if (action->text().startsWith(QStringLiteral("版本信息"))) versionAction = action;
        if (action->text().startsWith(QStringLiteral("更新日志"))) changelogAction = action;
    }
    QVERIFY(versionAction);
    QVERIFY(changelogAction);

    versionAction->trigger();
    auto* dialog = window.findChild<AboutDialog*>(QStringLiteral("applicationInfoDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    auto* tabs = dialog->findChild<QTabWidget*>(QStringLiteral("infoTabs"));
    QVERIFY(tabs);
    QCOMPARE(tabs->currentIndex(), 0);
    auto* version = dialog->findChild<QLabel*>(QStringLiteral("versionValue"));
    QVERIFY(version);
    QCOMPARE(version->text(), QStringLiteral(PAT_APP_VERSION));

    changelogAction->trigger();
    QCOMPARE(window.findChildren<AboutDialog*>().size(), 1);
    QCOMPARE(tabs->currentIndex(), 1);
    auto* changelog = dialog->findChild<QTextBrowser*>(QStringLiteral("changelogView"));
    QVERIFY(changelog);
    QVERIFY(changelog->toPlainText().contains(QStringLiteral("PAT 更新日志")));
    QVERIFY(changelog->toPlainText().contains(QStringLiteral(PAT_APP_VERSION)));
    QVERIFY(changelog->toPlainText().contains(QStringLiteral("2026-09-21")));
}

void UiTests::openFormatMenuLoadsFile() {
    MainWindow window;
    window.show();
    QAction* openAction = nullptr;
    for (auto* menuAction : window.menuBar()->actions()) {
        if (!menuAction->menu()) continue;
        for (auto* action : menuAction->menu()->actions()) {
            if (action->text().startsWith(QStringLiteral("打开格式"))) openAction = action;
        }
    }
    QVERIFY(openAction);

    const QString formatPath = QString::fromUtf8(PAT_SOURCE_DIR)
                               + QStringLiteral("/samples/cp01_b3_navigation.json");
    bool dialogSeen = false;
    QTimer poll;
    poll.setInterval(20);
    QObject::connect(&poll, &QTimer::timeout, &window, [&]() {
        for (auto* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            dialogSeen = true;
            poll.stop();
            QTimer::singleShot(100, dialog, [dialog, formatPath]() {
                auto* fileNameEdit = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
                if (!fileNameEdit) return;
                fileNameEdit->setText(formatPath);
                QMetaObject::invokeMethod(dialog, "accept");
            });
            return;
        }
    });
    poll.start();
    QTimer::singleShot(5000, &window, [&]() {
        poll.stop();
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* dialog = qobject_cast<QDialog*>(widget)) dialog->reject();
        }
    });
    openAction->trigger();
    QVERIFY(dialogSeen);
    QVERIFY(window.formatDocument_.HasFormat());
    QCOMPARE(window.formatDocument_.Format().templateId, QStringLiteral("cp01_b3_navigation_output_v2"));
    QVERIFY(window.statusLabel_->text().contains(QStringLiteral("格式已加载")));
}

void UiTests::liveChartsAppearBeforeFirstFrame() {
    int master = -1;
    int slave = -1;
    char path[256]{};
    QCOMPARE(openpty(&master, &slave, path, nullptr, nullptr), 0);
    ::close(slave);

    MainWindow window;
    QString error;
    const QString sourceDir = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(window.formatDocument_.LoadFromFile(
                 sourceDir + QStringLiteral("/samples/cp01_b3_navigation.json"), error),
             qPrintable(error));
    window.BuildSignalTree();
    window.signalTreeController_->SetSignalsChecked({0}, true);
    window.UpdateCharts();
    window.show();
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("serialCaptureDock"));
    QVERIFY(dock);
    QVERIFY(!dock->isVisible());
    dock->toggleViewAction()->trigger();
    QTRY_VERIFY(dock->isVisible());
    QCOMPARE(window.chartArea_->findChildren<SignalChartView*>().size(), 1);
    QVERIFY(!window.liveSession_.HasData());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    window.portCombo_->addItem(QStringLiteral("测试串口"), QString::fromLocal8Bit(path));
    window.portCombo_->setCurrentIndex(window.portCombo_->count() - 1);
    window.logDirectoryEdit_->setText(dir.path());
    window.StartSerialCapture();
    QTRY_VERIFY_WITH_TIMEOUT(window.statusLabel_->text().contains(QStringLiteral("串口已打开")), 3000);
    QCOMPARE(window.chartArea_->findChildren<SignalChartView*>().size(), 1);
    QVERIFY(!window.liveSession_.HasData());

    QFile sample(sourceDir + QStringLiteral("/samples/cp01_b3_synthetic.hex"));
    QVERIFY(sample.open(QIODevice::ReadOnly));
    const QByteArray frame = QByteArray::fromHex(sample.readAll());
    QCOMPARE(::write(master, frame.constData(), static_cast<size_t>(frame.size())), ssize_t(frame.size()));
    QTRY_VERIFY_WITH_TIMEOUT(window.liveSession_.HasData(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(window.captureStatsLabel_->text().contains(QStringLiteral("已解析 1 帧")), 3000);
    QCOMPARE(window.chartArea_->findChildren<SignalChartView*>().size(), 1);
    QCOMPARE(window.liveSession_.SeriesData()[0].samples.size(), 1);
    const auto& signalFormats = window.formatDocument_.Format().signalFormats;
    const auto findSignal = [&signalFormats](const QString& name) {
        for (int i = 0; i < static_cast<int>(signalFormats.size()); ++i) {
            if (signalFormats[static_cast<size_t>(i)].name == name) return i;
        }
        return -1;
    };
    const int activeBit = findSignal(QStringLiteral("系统状态1_导航运行"));
    const int inactiveBit = findSignal(QStringLiteral("系统状态1_GNSS航迹角有效"));
    QVERIFY(activeBit >= 0);
    QVERIFY(inactiveBit >= 0);
    QCOMPARE(window.liveSession_.SeriesData()[activeBit].samples.first().y(), 1.0);
    QCOMPARE(window.liveSession_.SeriesData()[inactiveBit].samples.first().y(), 0.0);

    auto* view = window.chartArea_->findChild<SignalChartView*>();
    QVERIFY(view);
    auto* plotted = qobject_cast<QLineSeries*>(view->chart()->series().first());
    QVERIFY(plotted);
    QTRY_COMPARE(plotted->points().size(), 1);
    window.pauseDisplayButton_->click();
    QCOMPARE(::write(master, frame.constData(), static_cast<size_t>(frame.size())), ssize_t(frame.size()));
    QTRY_VERIFY_WITH_TIMEOUT(window.captureStatsLabel_->text().contains(QStringLiteral("已解析 2 帧")), 3000);
    QCOMPARE(window.liveSession_.SeriesData()[0].samples.size(), 2);
    QCOMPARE(plotted->points().size(), 1);
    window.pauseDisplayButton_->click();
    QTRY_COMPARE(plotted->points().size(), 2);

    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("dataViewTabs"));
    QVERIFY(tabs);
    tabs->setCurrentIndex(1);
    auto* trajectory = window.findChild<TrajectoryView*>(QStringLiteral("trajectoryView"));
    QVERIFY(trajectory);
    auto* trajectoryChart = trajectory->findChild<QChartView*>(QStringLiteral("trajectoryChart"));
    QVERIFY(trajectoryChart);
    auto* track = qobject_cast<QLineSeries*>(trajectoryChart->chart()->series().first());
    QVERIFY(track);
    QTRY_COMPARE(track->points().size(), 2);
    QCOMPARE(::write(master, frame.constData(), static_cast<size_t>(frame.size())), ssize_t(frame.size()));
    QTRY_COMPARE(track->points().size(), 3);
    window.pauseDisplayButton_->click();
    QCOMPARE(::write(master, frame.constData(), static_cast<size_t>(frame.size())), ssize_t(frame.size()));
    QTRY_VERIFY_WITH_TIMEOUT(window.captureStatsLabel_->text().contains(QStringLiteral("已解析 4 帧")), 3000);
    QCOMPARE(track->points().size(), 3);
    window.pauseDisplayButton_->click();
    QTRY_COMPARE(track->points().size(), 4);
    tabs->setCurrentIndex(0);
    QTRY_COMPARE(plotted->points().size(), 4);

    window.StopSerialCapture();
    QTRY_VERIFY_WITH_TIMEOUT(!window.captureRunning_, 3000);
    ::close(master);
}

void UiTests::chartGroupsGuidesAndAxes() {
    MainWindow window;
    QString error;
    const QString root = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(window.formatDocument_.LoadFromFile(root + QStringLiteral("/samples/cp01_b3_navigation.json"), error),
             qPrintable(error));
    QVERIFY2(window.dataSession_.Load(root + QStringLiteral("/samples/cp01_b3_synthetic.hex"),
                                      window.formatDocument_.Format(), error), qPrintable(error));
    window.BuildSignalTree();
    window.signalTreeController_->SetSignalsChecked({0, 7}, true);
    window.UpdateCharts();
    window.show();
    QTest::qWait(100);
    auto* mainSplitter = window.centralWidget()->findChild<QSplitter*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(mainSplitter);
    QVERIFY(mainSplitter->sizes().first() < mainSplitter->sizes().last() / 2);
    auto views = window.chartArea_->findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 2);
    QVERIFY(std::abs(views[0]->chart()->plotArea().left() - views[1]->chart()->plotArea().left()) < 2.0);
    for (auto* view : views) {
        auto* yAxis = qobject_cast<QValueAxis*>(view->chart()->axes(Qt::Vertical).first());
        auto* xAxis = qobject_cast<QValueAxis*>(view->chart()->axes(Qt::Horizontal).first());
        QVERIFY(yAxis);
        QVERIFY(xAxis);
        if (view->ViewIndex() == 0) QCOMPARE(yAxis->labelFormat(), QStringLiteral("%.1f"));
        else QVERIFY(yAxis->labelFormat().endsWith(QLatin1Char('e')));
        QCOMPARE(xAxis->isVisible(), view->ViewIndex() == 1);
        QVERIFY(!xAxis->isTitleVisible());
    }
    QVERIFY(views[0]->chart()->backgroundBrush().color() != views[1]->chart()->backgroundBrush().color());
    QVERIFY(views[0]->minimumHeight() >= 150);
    views[0]->SetCursorX(0.0);
    bool sawTwoLineValue = false;
    for (auto* item : views[0]->chart()->scene()->items()) {
        auto* label = dynamic_cast<QGraphicsSimpleTextItem*>(item);
        if (!label || !label->text().startsWith(QStringLiteral("k:"))) continue;
        QVERIFY(label->text().contains(QStringLiteral("\nv:")));
        QVERIFY(label->boundingRect().height() > QFontMetricsF(label->font()).height() * 1.5);
        sawTwoLineValue = true;
    }
    QVERIFY(sawTwoLineValue);

    views[0]->SetGuideLines({{GuideOrientation::Horizontal, -12.34},
                             {GuideOrientation::Vertical, 0.0}});
    QCOMPARE(views[0]->GuideLines().size(), 2);
    window.UpdateCharts();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    views = window.chartArea_->findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 2);
    QCOMPARE(views[0]->GuideLines().size(), 2);

    window.HandleMergeRequested({0, 7});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    views = window.chartArea_->findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 1);
    QCOMPARE(views[0]->SeriesIndices().size(), 2);
    window.HandleSplitRequested({0, 7});
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    views = window.chartArea_->findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 2);
    QVERIFY(window.signalTreeController_->CollectCheckedSignalIndices().contains(0));
    QVERIFY(window.signalTreeController_->CollectCheckedSignalIndices().contains(7));

    auto* bottomView = views[1];
    QTest::qWait(20);
    const QRectF plot = bottomView->chart()->plotArea();
    const QPoint from = bottomView->mapFromScene(bottomView->chart()->mapToScene(
        QPointF(plot.center().x(), plot.bottom() + 12.0)));
    const QPoint to = bottomView->mapFromScene(bottomView->chart()->mapToScene(plot.center()));
    QTest::mousePress(bottomView->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent drag(QEvent::MouseMove, QPointF(to), QPointF(bottomView->viewport()->mapToGlobal(to)),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bottomView->viewport(), &drag);
    QTest::mouseRelease(bottomView->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(bottomView->GuideLines().size(), 1);
    QCOMPARE(bottomView->GuideLines().first().orientation, GuideOrientation::Horizontal);
    auto* bottomYAxis = qobject_cast<QValueAxis*>(bottomView->chart()->axes(Qt::Vertical).first());
    QVERIFY(bottomYAxis);
    QVERIFY(bottomView->GuideLines().first().value > bottomYAxis->min());

    const QPoint verticalFrom = bottomView->mapFromScene(bottomView->chart()->mapToScene(
        QPointF(plot.left() - 12.0, plot.center().y())));
    QTest::mousePress(bottomView->viewport(), Qt::LeftButton, Qt::NoModifier, verticalFrom);
    QMouseEvent verticalDrag(QEvent::MouseMove, QPointF(to),
                             QPointF(bottomView->viewport()->mapToGlobal(to)),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(bottomView->viewport(), &verticalDrag);
    QTest::mouseRelease(bottomView->viewport(), Qt::LeftButton, Qt::NoModifier, to);
    QCOMPARE(bottomView->GuideLines().size(), 2);
    QCOMPARE(bottomView->GuideLines().last().orientation, GuideOrientation::Vertical);
    bottomView->SetGuideLines({});
    QVERIFY(bottomView->GuideLines().isEmpty());
}

void UiTests::guideShowsAllCrossingsAndKeepsCursor() {
    QVector<pat::Series> source(2);
    source[0].name = QStringLiteral("波形");
    source[0].unit = QStringLiteral("V");
    source[0].samples = {{0, 0}, {1, 2}, {2, 0}, {3, 2}, {4, 0}};
    source[1].name = QStringLiteral("斜线");
    source[1].samples = {{0, 2}, {4, 0}};

    SignalChartView view;
    view.resize(800, 300);
    view.Configure(QStringLiteral("测试"), QString(), QStringLiteral("采样序号"), true,
                   {QColor(Qt::cyan), QColor(Qt::green)}, {0, 1},
                   {source[0].samples, source[1].samples}, &source,
                   -1.0, 3.0, 0.0, 4.0, 0);
    view.SetRangeContext({0.0, 4.0, 0.0, 4.0, 0.01, true});
    view.SetGuideLines({{GuideOrientation::Horizontal, 1.0}});
    view.show();
    QTest::qWait(50);

    QGraphicsLineItem* guideItem = nullptr;
    for (auto* item : view.chart()->scene()->items()) {
        auto* line = dynamic_cast<QGraphicsLineItem*>(item);
        if (line && line->pen().color() == QColor(255, 227, 90)) guideItem = line;
    }
    QVERIFY(guideItem);
    QVERIFY(guideItem->pen().widthF() >= 2.5);
    const QPointF chartPoint(view.chart()->plotArea().center().x(), guideItem->line().y1());
    const QPoint viewPoint = view.mapFromScene(view.chart()->mapToScene(chartPoint));
    QSignalSpy moved(&view, &SignalChartView::CursorMoved);
    QSignalSpy left(&view, &SignalChartView::CursorLeft);
    QMouseEvent move(QEvent::MouseMove, QPointF(viewPoint),
                     QPointF(view.viewport()->mapToGlobal(viewPoint)),
                     Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &move);
    QVERIFY(moved.count() > 0);
    QCOMPARE(left.count(), 0);
    auto* popup = view.findChild<QLabel*>(QStringLiteral("guideHoverPopup"));
    QVERIFY(popup);
    QVERIFY(popup->isVisible());
    const QString tooltip = popup->text();
    QVERIFY2(tooltip.contains(QStringLiteral("key=0.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("key=1.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("key=2.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("key=3.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("斜线")), qPrintable(tooltip));
    // Keep the mouse stationary beyond the normal short-tooltip timeout.
    QTest::qWait(11000);
    QVERIFY(popup->isVisible());
    QCOMPARE(left.count(), 0);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&view, &leave);
    QVERIFY(!popup->isVisible());
    QCoreApplication::sendEvent(view.viewport(), &move);
    QVERIFY(popup->isVisible());
    view.SetGuideLines({});
    QVERIFY(!popup->isVisible());
}

void UiTests::timeRangeControlSupportsHandlesAndPanning() {
    TimeRangeControl control;
    control.resize(800, 90);
    QVERIFY(!control.isEnabled());
    control.SetExtent(100.0, QStringLiteral("s"), true);
    control.SetSelection(0.0, 100.0);
    control.show();
    QTest::qWait(30);
    auto* bar = control.findChild<QWidget*>("timeRangeBar");
    auto* start = control.findChild<QDoubleSpinBox*>("timeRangeStart");
    auto* end = control.findChild<QDoubleSpinBox*>("timeRangeEnd");
    auto* reset = control.findChild<QPushButton*>("timeRangeReset");
    QVERIFY(bar && start && end && reset);
    QSignalSpy selected(&control, &TimeRangeControl::RangeSelected);
    const auto at = [bar](double fraction) {
        return QPoint(qRound(10 + fraction * (bar->width() - 20)), bar->height() / 2);
    };
    const auto drag = [&](double from, double to) {
        const int previousCount = selected.count();
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, at(from));
        QMouseEvent move(QEvent::MouseMove, QPointF(at(to)),
                         QPointF(bar->mapToGlobal(at(to))),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(bar, &move);
        QCOMPARE(selected.count(), previousCount); // No data reload while dragging.
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, at(to));
        QCOMPARE(selected.count(), previousCount + 1);
    };
    drag(0.0, 0.2);
    QVERIFY(std::abs(start->value() - 20.0) < 0.2);
    drag(1.0, 0.6);
    QVERIFY(std::abs(end->value() - 60.0) < 0.2);
    const double span = end->value() - start->value();
    drag(0.4, 0.6);
    QVERIFY(std::abs(start->value() - 40.0) < 0.3);
    QVERIFY(std::abs(end->value() - start->value() - span) < 1e-6);
    reset->click();
    QCOMPARE(start->value(), 0.0);
    QCOMPARE(end->value(), 100.0);
    auto* next = control.findChild<QPushButton*>("timeRangeNext");
    auto* previous = control.findChild<QPushButton*>("timeRangePrevious");
    QVERIFY(next && previous);
    control.SetSelection(20, 40);
    next->click();
    QCOMPARE(start->value(), 40.0);
    QCOMPARE(end->value(), 60.0);
    previous->click();
    QCOMPARE(start->value(), 20.0);
    QCOMPARE(end->value(), 40.0);
    start->setValue(80);
    QVERIFY(QMetaObject::invokeMethod(start, "editingFinished", Qt::DirectConnection));
    QVERIFY(start->value() >= 79.99);
    QVERIFY(end->value() > start->value());
    control.SetExtent(100, QStringLiteral("s"), false);
    QVERIFY(!control.isEnabled());
}

void UiTests::spatialRangeUsesTimeAndRestoresHistory() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJson(QString::fromUtf8(PAT_SOURCE_DIR)
                                     + "/samples/cp01_b3_navigation.json", format, error),
             qPrintable(error));
    QVector<pat::Series> source(static_cast<qsizetype>(format.signalFormats.size()));
    for (int signal = 0; signal < source.size(); ++signal) {
        for (int i = 0; i < 10; ++i)
            source[signal].samples.append(QPointF(i * 0.25, signal * 100.0 + i));
    }
    TrajectoryView view;
    view.ConfigureSignals(format);
    auto* plot = view.findChild<TrajectoryPlotWidget*>();
    QVERIFY(plot);
    auto* plot3D = view.AddPlot(TrajectoryDimension::ThreeD);
    view.SetDataSource(&source, pat::TrajectoryPairing::RecordOrder);
    view.SetTimeWindow(0.5, 1.0);
    auto* chart = plot->findChild<QChartView*>();
    QVERIFY(chart);
    QLineSeries* line = nullptr;
    for (auto* series : chart->chart()->series()) {
        if ((line = qobject_cast<QLineSeries*>(series))) break;
    }
    QVERIFY(line);
    auto* x = plot->findChild<QComboBox*>("trajectoryXCombo");
    QVERIFY(x);
    QCOMPARE(line->count(), 3);
    QCOMPARE(line->points().first().x(), x->currentData().toInt() * 100.0 + 2);
    QCOMPARE(line->points().last().x(), x->currentData().toInt() * 100.0 + 4);
    auto* status3D = plot3D->findChild<QLabel*>(
        QStringLiteral("trajectoryStatus_%1").arg(plot3D->PlotId()));
    QVERIFY(status3D);
    QVERIFY(status3D->text().contains(QStringLiteral("区间点 3")));
    QCOMPARE(plot->HistoryPointCount(), 10); // Filtering must not erase source history.
    view.SetTimeWindow(2.3, 2.4);
    QCOMPARE(line->count(), 0);
    view.SetTimeWindow(0, 2.25);
    QCOMPARE(line->count(), 10);
    QCOMPARE(source.first().samples.size(), 10);
    // Timestamp-based mixed logs use the same interval semantics.
    view.SetDataSource(&source, pat::TrajectoryPairing::OfflineTimestamp);
    view.SetTimeWindow(0.5, 1.0);
    QCOMPARE(line->count(), 3);
}

void UiTests::liveChartsLimitVisibleWork() {
    QVector<pat::Series> source(9);
    QVector<DisplayGroup> groups;
    for (int signal = 0; signal < source.size(); ++signal) {
        source[signal].name = QStringLiteral("信号%1").arg(signal);
        groups.append({{signal}, source[signal].name, QString(), false});
        for (int sample = 0; sample < 4000; ++sample) {
            const double value = signal == 0 && sample == 0 ? 1e7 : double((sample + signal) % 20);
            source[signal].samples.append(QPointF(sample, value));
        }
    }
    ChartArea area;
    area.resize(800, 600);
    area.SetSeries(&source);
    area.SetDisplayGroups(groups);
    pat::SeriesStatistics stats;
    stats.maxX = 3999;
    stats.minStep = 1;
    stats.hasRange = true;
    area.SetStatistics(stats);
    area.RefreshCharts();
    area.show();
    QTest::qWait(30);
    area.RefreshLiveData(true);

    const auto views = area.findChildren<SignalChartView*>();
    QCOMPARE(views.size(), 9);
    for (auto* view : views) {
        QVERIFY(view->renderHints().testFlag(QPainter::Antialiasing));
        auto* yAxis = qobject_cast<QValueAxis*>(view->chart()->axes(Qt::Vertical).first());
        QVERIFY(yAxis);
        QVERIFY(yAxis->max() < 100.0);
        auto* line = qobject_cast<QLineSeries*>(view->chart()->series().first());
        QVERIFY(line);
        const int widthLimit = std::max(200, int(view->chart()->plotArea().width() * 1.5));
        QVERIFY(line->points().size() <= widthLimit + 2);
    }
}

void UiTests::axisLabelsDistinguishSmallDeltas() {
    QVector<pat::Series> source(1);
    source[0].name = QStringLiteral("精密值");
    source[0].samples = {{0, 1000000.0}, {1, 1000000.1}};
    SignalChartView view;
    view.Configure(QStringLiteral("精密值"), QString(), QStringLiteral("采样序号"), false,
                   {QColor(Qt::cyan)}, {0}, {source[0].samples}, &source,
                   1000000.0, 1000000.1, 0.0, 1.0, 0);
    auto* axis = qobject_cast<QValueAxis*>(view.chart()->axes(Qt::Vertical).first());
    QVERIFY(axis);
    const QByteArray format = axis->labelFormat().toLatin1();
    QVERIFY(QString::asprintf(format.constData(), axis->min())
            != QString::asprintf(format.constData(), axis->max()));
    view.SetYAxisRange(-1e-8, 1e-8);
    const QByteArray tinyFormat = axis->labelFormat().toLatin1();
    QVERIFY(QString::asprintf(tinyFormat.constData(), axis->min())
            != QString::asprintf(tinyFormat.constData(), axis->max()));
}

QTEST_MAIN(UiTests)
#include "UiTests.moc"
