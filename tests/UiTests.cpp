#include "ui/MainWindow.h"
#include "ui/ChartArea.h"
#include "ui/SignalChartView.h"

#include <QComboBox>
#include <QAction>
#include <QApplication>
#include <QDockWidget>
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
#include <QPushButton>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolTip>
#include <QtCharts/QChart>
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

class UiTests : public QObject {
    Q_OBJECT

private slots:
    void openFormatMenuLoadsFile();
    void liveChartsAppearBeforeFirstFrame();
    void chartGroupsGuidesAndAxes();
    void guideShowsAllCrossingsAndKeepsCursor();
    void liveChartsLimitVisibleWork();
    void axisLabelsDistinguishSmallDeltas();
};

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
        if (!label || !label->text().startsWith(QStringLiteral("key="))) continue;
        QVERIFY(label->text().contains(QStringLiteral("\nval=")));
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
    const QString tooltip = QToolTip::text();
    QVERIFY2(tooltip.contains(QStringLiteral("key=0.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("key=1.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("key=2.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("key=3.5")), qPrintable(tooltip));
    QVERIFY2(tooltip.contains(QStringLiteral("斜线")), qPrintable(tooltip));
    QToolTip::hideText();
}

void UiTests::liveChartsLimitVisibleWork() {
    QVector<pat::Series> source(3);
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
    QCOMPARE(views.size(), 3);
    for (auto* view : views) {
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
