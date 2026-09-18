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
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
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
        QCOMPARE(yAxis->labelFormat(), QStringLiteral("%.1f"));
        QCOMPARE(xAxis->labelsVisible(), view->ViewIndex() == 1);
    }
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

QTEST_MAIN(UiTests)
#include "UiTests.moc"
