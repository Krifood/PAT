#include "ui/MainWindow.h"
#include "ui/ChartArea.h"
#include "ui/SignalChartView.h"

#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QTemporaryDir>
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
    void liveChartsAppearBeforeFirstFrame();
};

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
    QCOMPARE(window.chartArea_->findChildren<SignalChartView*>().size(), 1);
    QCOMPARE(window.liveSession_.SeriesData()[0].samples.size(), 1);

    window.StopSerialCapture();
    QTRY_VERIFY_WITH_TIMEOUT(!window.captureRunning_, 3000);
    ::close(master);
}

QTEST_MAIN(UiTests)
#include "UiTests.moc"
