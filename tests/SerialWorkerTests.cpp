#include "serial/SerialCaptureWorker.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#if defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif
#include <unistd.h>

class SerialWorkerTests : public QObject {
    Q_OBJECT

private slots:
    void pseudoTerminalCapture();
    void cp01B3PseudoTerminalCapture();
};

void SerialWorkerTests::pseudoTerminalCapture() {
    int master = -1;
    int slave = -1;
    char path[256]{};
    QCOMPARE(openpty(&master, &slave, path, nullptr, nullptr), 0);
    ::close(slave);

    pat::FormatDefinition format;
    QString error;
    constexpr auto templateJson = R"({
      "schema_version":2,"template_id":"pty","record_size":4,"endianness":"little",
      "framing":{"kind":"fixed_header","header_hex":"AA55","frame_length":4},
      "signals":[{"name":"value","byte_offset":2,"value_type":"uint16"}]
    })";
    QVERIFY2(pat::LoadFormatFromJsonData(templateJson, format, error), qPrintable(error));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    pat::SerialCaptureConfig config;
    config.portName = QString::fromLocal8Bit(path);
    config.logDirectory = dir.path();
    config.inactivityTimeoutMs = 0;
    pat::SerialCaptureWorker worker(format, config);
    qRegisterMetaType<QVector<pat::LiveSample>>();
    QSignalSpy started(&worker, &pat::SerialCaptureWorker::Started);
    QSignalSpy batches(&worker, &pat::SerialCaptureWorker::SamplesReady);
    QSignalSpy errors(&worker, &pat::SerialCaptureWorker::Error);
    worker.Start();
    QVERIFY2(started.size() == 1,
             errors.isEmpty() ? "serial worker did not start" : qPrintable(errors.first().first().toString()));

    const QByteArray input = QByteArray::fromHex("ffaa559001aa559101");
    QCOMPARE(::write(master, input.constData(), static_cast<size_t>(input.size())), ssize_t(input.size()));
    QTRY_VERIFY_WITH_TIMEOUT(!batches.isEmpty(), 3000);
    worker.Stop();
    ::close(master);
    QCOMPARE(errors.size(), 0);
    const auto samples = qvariant_cast<QVector<pat::LiveSample>>(batches.first().at(0));
    QCOMPARE(samples.size(), 2);
    QCOMPARE(samples[0].values[0], 400.0);
    QCOMPARE(samples[1].values[0], 401.0);
    QFile raw(started.first().at(0).toString());
    QVERIFY(raw.open(QIODevice::ReadOnly));
    QCOMPARE(raw.readAll(), input);
    QFile csv(started.first().at(1).toString());
    QVERIFY(csv.open(QIODevice::ReadOnly));
    QCOMPARE(csv.readAll().count('\n'), qsizetype(3));
}

void SerialWorkerTests::cp01B3PseudoTerminalCapture() {
    int master = -1;
    int slave = -1;
    char path[256]{};
    QCOMPARE(openpty(&master, &slave, path, nullptr, nullptr), 0);
    ::close(slave);

    pat::FormatDefinition format;
    QString error;
    const QString sourceDir = QString::fromUtf8(PAT_SOURCE_DIR);
    QVERIFY2(pat::LoadFormatFromJson(sourceDir + QStringLiteral("/samples/cp01_b3_navigation.json"),
                                     format, error), qPrintable(error));
    QFile sample(sourceDir + QStringLiteral("/samples/cp01_b3_synthetic.hex"));
    QVERIFY(sample.open(QIODevice::ReadOnly));
    const QByteArray valid = QByteArray::fromHex(sample.readAll());
    QCOMPARE(valid.size(), qsizetype(86));
    QByteArray invalid = valid;
    invalid[24] = static_cast<char>(invalid[24] ^ 1);
    const QByteArray input = valid + invalid + valid;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    pat::SerialCaptureConfig config;
    config.portName = QString::fromLocal8Bit(path);
    config.logDirectory = dir.path();
    config.inactivityTimeoutMs = 0;
    pat::SerialCaptureWorker worker(format, config);
    qRegisterMetaType<QVector<pat::LiveSample>>();
    QSignalSpy started(&worker, &pat::SerialCaptureWorker::Started);
    QSignalSpy batches(&worker, &pat::SerialCaptureWorker::SamplesReady);
    QSignalSpy errors(&worker, &pat::SerialCaptureWorker::Error);
    worker.Start();
    QVERIFY2(started.size() == 1,
             errors.isEmpty() ? "serial worker did not start" : qPrintable(errors.first().first().toString()));
    QCOMPARE(::write(master, input.constData(), static_cast<size_t>(input.size())), ssize_t(input.size()));
    QTRY_VERIFY_WITH_TIMEOUT(!batches.isEmpty(), 3000);
    worker.Stop();
    ::close(master);
    QCOMPARE(errors.size(), 0);
    QVector<pat::LiveSample> samples;
    for (const auto& batch : batches) {
        samples += qvariant_cast<QVector<pat::LiveSample>>(batch.at(0));
    }
    QCOMPARE(samples.size(), 2);
    QCOMPARE(samples[0].values.size(), 82);
    QCOMPARE(samples[0].values[0], -12.34);
    QCOMPARE(samples[1].index, quint64(1));
    QFile raw(started.first().at(0).toString());
    QVERIFY(raw.open(QIODevice::ReadOnly));
    QCOMPARE(raw.readAll(), input);
    QFile csv(started.first().at(1).toString());
    QVERIFY(csv.open(QIODevice::ReadOnly));
    QCOMPARE(csv.readAll().count('\n'), qsizetype(3));
}

QTEST_GUILESS_MAIN(SerialWorkerTests)
#include "SerialWorkerTests.moc"
