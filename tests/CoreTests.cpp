#include "core/CaptureSession.h"
#include "core/FormatDefinition.h"
#include "core/FrameExporter.h"
#include "core/InputReader.h"
#include "core/LiveDataSession.h"
#include "core/RecordDecoder.h"
#include "core/RecordParser.h"
#include "core/SerialFrameAssembler.h"
#include "core/SeriesProcessing.h"
#include "core/TrajectoryData.h"

#include <QFile>
#include <QBuffer>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QtTest>

namespace {

constexpr auto kFramedFormat = R"({
  "schema_version": 2,
  "template_id": "sensor",
  "record_size": 4,
  "endianness": "little",
  "framing": {"kind": "fixed_header", "header_hex": "AA55", "frame_length": 4},
  "signals": [{"name": "temperature", "byte_offset": 2, "value_type": "int16",
               "scale": 0.1, "bias": -40, "unit": "C"}]
})";

}  // namespace

class CoreTests : public QObject {
    Q_OBJECT

private slots:
    void legacyAndEndian();
    void framedAcrossChunks();
    void replayUsesSameDecoder();
    void invalidFraming();
    void invalidBitSelection();
    void cp01B3NavigationChecksum();
    void hexTextAndExport();
    void longContinuousHexText();
    void liveBufferBounded();
    void captureLogsAreIndependentOfDisplay();
    void oneHourSimulatedStream();
    void legacyFileRegression();
    void jsonGuideExampleDecodes();
    void trajectoryPairsByRecordOrSampleIndex();
    void displayTransformAndDifferenceSeries();
};

void CoreTests::displayTransformAndDifferenceSeries() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(R"({
      "record_size":6,"signals":[
        {"name":"A","byte_offset":0,"value_type":"int16","unit":"m/s"},
        {"name":"B","byte_offset":2,"value_type":"int16","unit":"m/s"},
        {"name":"C","byte_offset":4,"value_type":"int16","unit":"°"}
      ]})", format, error), qPrintable(error));

    pat::SeriesProcessing processing;
    processing.Reset(format);
    processing.SetTransform(0, {0.5, 10.0});
    QCOMPARE(processing.Transform(0).Apply(4.0), 12.0);
    QVERIFY(!processing.Transform(0).IsIdentity());

    QVERIFY2(processing.AddDifference(0, 1, error), qPrintable(error));
    QVERIFY(!processing.AddDifference(0, 2, error));
    QVERIFY(error.contains(QStringLiteral("单位不同")));

    QVector<pat::Series> source(3);
    source[0].samples = {{0, 10}, {1, 15}, {2, 18}};
    source[1].samples = {{0, 3}, {2, 8}};
    processing.RebuildDerived(source);
    QCOMPARE(processing.DerivedSeries().size(), 1);
    QCOMPARE(processing.DerivedSeries()[0].samples,
             (QVector<QPointF>{{0, 7}, {2, 10}}));

    processing.ClearDerivedSamples();
    processing.AppendLive(7, {20, 6, 0}, 2);
    processing.AppendLive(8, {24, 5, 0}, 2);
    processing.AppendLive(9, {30, 8, 0}, 2);
    QCOMPARE(processing.DerivedSeries()[0].samples,
             (QVector<QPointF>{{8, 19}, {9, 22}}));
    processing.ResetTransform(0);
    QVERIFY(processing.Transform(0).IsIdentity());
}

void CoreTests::trajectoryPairsByRecordOrSampleIndex() {
    pat::Series longitude, latitude;
    longitude.samples = {{0, 116.0}, {1, 116.1}, {2, 116.2}};
    latitude.samples = {{0, 39.0}, {2, 39.2}};

    const auto live = pat::PairTrajectorySamples(longitude, latitude,
                                                pat::TrajectoryPairing::SampleIndex);
    QCOMPARE(live, (QVector<QPointF>{{116.0, 39.0}, {116.2, 39.2}}));

    latitude.samples = {{0, 39.0}, {0.5, 39.1}, {1.0, 39.2}};
    const auto offline = pat::PairTrajectorySamples(longitude, latitude,
                                                   pat::TrajectoryPairing::RecordOrder);
    QCOMPARE(offline, (QVector<QPointF>{{116.0, 39.0}, {116.1, 39.1}, {116.2, 39.2}}));

    QVector<QPointF> path;
    for (int i = 0; i < 100; ++i) path.append({double(i), i == 50 ? 1000.0 : 0.0});
    const auto reduced = pat::DecimateTrajectory(path, 18);
    QVERIFY(reduced.size() <= 18);
    QCOMPARE(reduced.first(), path.first());
    QCOMPARE(reduced.last(), path.last());
    QVERIFY(reduced.contains(path[50]));
}

void CoreTests::jsonGuideExampleDecodes() {
    QFile guide(QString::fromUtf8(PAT_SOURCE_DIR)
                + QStringLiteral("/docs/协议模板JSON编辑指南.md"));
    QVERIFY(guide.open(QIODevice::ReadOnly));
    const QByteArray content = guide.readAll();
    const qsizetype start = content.indexOf("```json\n");
    QVERIFY(start >= 0);
    const qsizetype jsonStart = start + qsizetype(sizeof("```json\n") - 1);
    const qsizetype end = content.indexOf("\n```", jsonStart);
    QVERIFY(end > jsonStart);

    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(content.mid(jsonStart, end - jsonStart), format, error),
             qPrintable(error));
    pat::SerialFrameAssembler assembler({format});
    const QByteArray frame = QByteArray::fromHex("aa5590010596");
    const auto frames = assembler.Feed(frame);
    QCOMPARE(frames.size(), 1);
    pat::RecordDecoder decoder(format);
    QVector<double> values;
    QVERIFY2(decoder.Decode(frames.first().bytes, values, error), qPrintable(error));
    QCOMPARE(values.size(), 3);
    QCOMPARE(values[0], 0.0);
    QCOMPARE(values[1], 5.0);
    QCOMPARE(values[2], 1.0);
}

void CoreTests::legacyAndEndian() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(R"({"record_size":4,"endianness":"big",
        "signals":[{"name":"signed","byte_offset":0,"value_type":"int16"},
                   {"name":"small","byte_offset":2,"value_type":"uint8"}]})",
        format, error), qPrintable(error));
    QVERIFY(!format.HasFraming());
    pat::RecordDecoder decoder(format);
    QVector<double> values;
    QVERIFY2(decoder.Decode(QByteArray::fromHex("ff9cfe00"), values, error), qPrintable(error));
    QCOMPARE(values.size(), 2);
    QCOMPARE(values[0], -100.0);
    QCOMPARE(values[1], 254.0);
}

void CoreTests::legacyFileRegression() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(R"({
      "record_size":8,"endianness":"little","time_unit":"s",
      "signals":[{"name":"sine","byte_offset":0,"value_type":"float32","time_scale":0.02},
                 {"name":"cosine","byte_offset":4,"value_type":"float32","time_scale":0.02}]
    })", format, error), qPrintable(error));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("legacy.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray record = QByteArray::fromHex("0000803f000000bf");
    QCOMPARE(file.write(record + record), qint64(16));
    file.close();
    pat::RecordParser parser(format);
    QVector<pat::Series> series;
    QVERIFY2(parser.ParseFile(path, series, error), qPrintable(error));
    QCOMPARE(series.size(), 2);
    QCOMPARE(series[0].samples.size(), 2);
    QCOMPARE(series[0].samples[0], QPointF(0.0, 1.0));
    QCOMPARE(series[1].samples[0], QPointF(0.0, -0.5));
    QCOMPARE(series[0].samples[1].x(), 0.02);
}

void CoreTests::framedAcrossChunks() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(kFramedFormat, format, error), qPrintable(error));
    pat::SerialFrameAssembler assembler({format});
    QVERIFY(assembler.ConfigurationError().isEmpty());
    QVERIFY(assembler.Feed(QByteArray::fromHex("99aa")).isEmpty());
    const auto frames = assembler.Feed(QByteArray::fromHex("559001aa559101"));
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames[0].bytes.toHex(), QByteArray("aa559001"));
    QCOMPARE(frames[1].bytes.toHex(), QByteArray("aa559101"));
    QCOMPARE(assembler.SkippedBytes(), quint64(1));
    QCOMPARE(assembler.PendingBytes(), qsizetype(0));

    pat::RecordDecoder decoder(format);
    QVector<double> values;
    QVERIFY2(decoder.Decode(frames[0].bytes, values, error), qPrintable(error));
    QCOMPARE(values[0], 0.0);
    QVERIFY2(decoder.Decode(frames[1].bytes, values, error), qPrintable(error));
    QVERIFY(qAbs(values[0] - 0.1) < 1e-9);
}

void CoreTests::replayUsesSameDecoder() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(kFramedFormat, format, error), qPrintable(error));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("raw.bin"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray::fromHex("00aa559001aa559101aa")), qint64(10));
    file.close();
    pat::RecordParser parser(format);
    QVector<pat::Series> series;
    QVERIFY2(parser.ParseFile(path, series, error), qPrintable(error));
    QCOMPARE(series.size(), 1);
    QCOMPARE(series[0].samples.size(), 2);
    QCOMPARE(series[0].samples[0], QPointF(0.0, 0.0));
    QCOMPARE(series[0].samples[1].x(), 1.0);
    QVERIFY(qAbs(series[0].samples[1].y() - 0.1) < 1e-9);
}

void CoreTests::invalidFraming() {
    pat::FormatDefinition format;
    QString error;
    const QByteArray invalid = QByteArray(kFramedFormat).replace("\"frame_length\": 4",
                                                              "\"frame_length\": 5");
    QVERIFY(!pat::LoadFormatFromJsonData(invalid, format, error));
    QVERIFY(error.contains(QStringLiteral("frame_length")));
}

void CoreTests::invalidBitSelection() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY(!pat::LoadFormatFromJsonData(R"({"record_size":1,"signals":[
      {"name":"bad","byte_offset":0,"value_type":"uint8","bit_offset":7,"bit_width":2}]})",
      format, error));
    QVERIFY(error.contains(QStringLiteral("位段")));
    QVERIFY(!pat::LoadFormatFromJsonData(R"({"record_size":1,"signals":[
      {"name":"bad","byte_offset":0,"value_type":"int8","bit_offset":0,"bit_width":1}]})",
      format, error));
    QVERIFY(error.contains(QStringLiteral("位段")));
    QVERIFY2(pat::LoadFormatFromJsonData(R"({"record_size":2,"endianness":"big","signals":[
      {"name":"slice","byte_offset":0,"value_type":"uint16","bit_offset":4,"bit_width":4}]})",
      format, error), qPrintable(error));
    pat::RecordDecoder decoder(format);
    QVector<double> values;
    QVERIFY2(decoder.Decode(QByteArray::fromHex("1234"), values, error), qPrintable(error));
    QCOMPARE(values[0], 3.0);
}

void CoreTests::cp01B3NavigationChecksum() {
    pat::FormatDefinition format;
    QString error;
    const QString templatePath = QString::fromUtf8(PAT_SOURCE_DIR)
                                 + QStringLiteral("/samples/cp01_b3_navigation.json");
    QVERIFY2(pat::LoadFormatFromJson(templatePath, format, error), qPrintable(error));
    QCOMPARE(format.recordSize, 86);
    QCOMPARE(format.signalFormats.size(), size_t(82));
    QCOMPARE(format.frameHeader.toHex(), QByteArray("eb901a56"));
    QVERIFY(format.HasSum8Checksum());
    QFile templateFile(templatePath);
    QVERIFY(templateFile.open(QIODevice::ReadOnly));
    const QByteArray originalTemplate = templateFile.readAll();
    QByteArray invalidTemplate = originalTemplate;
    QVERIFY(invalidTemplate.replace("\"byte_offset\": 85", "\"byte_offset\": 84")
                != originalTemplate);
    pat::FormatDefinition rejected;
    QVERIFY(!pat::LoadFormatFromJsonData(invalidTemplate, rejected, error));
    QVERIFY(error.contains(QStringLiteral("checksum")));

    QByteArray frame(86, '\0');
    frame.replace(0, 4, QByteArray::fromHex("eb901a56"));
    auto put16 = [&](int offset, quint16 value) {
        frame[offset] = static_cast<char>(value & 0xff);
        frame[offset + 1] = static_cast<char>(value >> 8);
    };
    auto put32 = [&](int offset, quint32 value) {
        for (int i = 0; i < 4; ++i) frame[offset + i] = static_cast<char>((value >> (8 * i)) & 0xff);
    };
    put16(4, 2300);                          // GPS week
    put32(6, 123456789);                     // Raw GPS week seconds
    frame[10] = static_cast<char>(0x4B);     // System status 1: bits 0, 1, 3, 6
    frame[11] = static_cast<char>(0x45);     // System status 2: navigation 5, positioning 4
    put32(16, 0x00004001);                  // Alarm bits 0 and 14
    put32(20, 0x00000004);                  // Fault bit 2
    put16(24, static_cast<quint16>(-1234));  // Roll = -12.34 degrees
    put16(26, 250);                          // Pitch = 2.50 degrees
    put16(28, 9000);                         // Yaw = 90.00 degrees
    put32(30, 1163971287);                  // GNSS longitude = 116.3971287 degrees
    put32(34, 399165278);                   // GNSS latitude = 39.9165278 degrees
    put32(74, 12345);                       // Combined altitude = 123.45 m
    frame[84] = 7;
    quint8 sum = 0;
    for (int i = 2; i <= 84; ++i) sum = static_cast<quint8>(sum + static_cast<quint8>(frame[i]));
    frame[85] = static_cast<char>(sum);
    QFile sample(QString::fromUtf8(PAT_SOURCE_DIR)
                 + QStringLiteral("/samples/cp01_b3_synthetic.hex"));
    QVERIFY(sample.open(QIODevice::ReadOnly));
    QCOMPARE(QByteArray::fromHex(sample.readAll().trimmed()), frame);

    pat::RecordDecoder decoder(format);
    QVector<double> values;
    QVERIFY2(decoder.Decode(frame, values, error), qPrintable(error));
    QCOMPARE(values[0], -12.34);
    QCOMPARE(values[1], 2.5);
    QCOMPARE(values[2], 90.0);
    QCOMPARE(values[3], 123.45);
    const auto valueFor = [&](const QString& name) {
        for (size_t i = 0; i < format.signalFormats.size(); ++i) {
            if (format.signalFormats[i].name == name) return values[static_cast<qsizetype>(i)];
        }
        return qQNaN();
    };
    QCOMPARE(valueFor(QStringLiteral("GPS周")), 2300.0);
    QCOMPARE(valueFor(QStringLiteral("GPS周秒")), 123456789.0);
    QCOMPARE(valueFor(QStringLiteral("系统状态1_导航运行")), 1.0);
    QCOMPARE(valueFor(QStringLiteral("系统状态1_GNSS航迹角有效")), 0.0);
    QCOMPARE(valueFor(QStringLiteral("系统状态2_导航状态码")), 5.0);
    QCOMPARE(valueFor(QStringLiteral("系统状态2_定位状态码")), 4.0);
    QCOMPARE(valueFor(QStringLiteral("告警字_滚转角")), 1.0);
    QCOMPARE(valueFor(QStringLiteral("告警字_北向速度")), 1.0);
    QCOMPARE(valueFor(QStringLiteral("故障字_偏航角")), 1.0);
    QCOMPARE(valueFor(QStringLiteral("故障字_滚转角")), 0.0);
    QVERIFY(qAbs(valueFor(QStringLiteral("GNSS经度")) - 116.3971287) < 1e-9);
    QVERIFY(qAbs(valueFor(QStringLiteral("GNSS纬度")) - 39.9165278) < 1e-9);
    QCOMPARE(valueFor(QStringLiteral("校验")), static_cast<double>(sum));

    QByteArray corrupt = frame;
    corrupt[24] = static_cast<char>(corrupt[24] ^ 1);
    pat::SerialFrameAssembler assembler({format});
    QVERIFY(assembler.ConfigurationError().isEmpty());
    QVERIFY(assembler.Feed(frame.left(30)).isEmpty());
    const auto first = assembler.Feed(frame.mid(30) + corrupt + frame);
    QCOMPARE(first.size(), 2);
    QCOMPARE(first[0].bytes, frame);
    QCOMPARE(first[1].bytes, frame);
    QCOMPARE(assembler.SkippedBytes(), quint64(86));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    pat::CaptureSession capture(format);
    QVERIFY2(capture.Start(dir.path(), error), qPrintable(error));
    QVector<pat::LiveSample> liveSamples;
    QVERIFY2(capture.Feed(frame + corrupt + frame, liveSamples, error), qPrintable(error));
    QCOMPARE(liveSamples.size(), 2);
    QCOMPARE(liveSamples[0].values[0], -12.34);
    QCOMPARE(liveSamples[1].index, quint64(1));
    QCOMPARE(capture.SkippedBytes(), quint64(86));
    QVERIFY2(capture.Stop(error), qPrintable(error));
    QFile capturedRaw(capture.RawPath());
    QVERIFY(capturedRaw.open(QIODevice::ReadOnly));
    QCOMPARE(capturedRaw.readAll(), frame + corrupt + frame);
    QFile capturedCsv(capture.CsvPath());
    QVERIFY(capturedCsv.open(QIODevice::ReadOnly));
    QCOMPARE(capturedCsv.readAll().count('\n'), qsizetype(3));

    const QString rawPath = dir.filePath(QStringLiteral("cp01.bin"));
    QFile raw(rawPath);
    QVERIFY(raw.open(QIODevice::WriteOnly));
    QCOMPARE(raw.write(frame + corrupt + frame), qint64(258));
    raw.close();
    pat::RecordParser parser(format);
    QVector<pat::Series> series;
    QVERIFY2(parser.ParseFile(rawPath, series, error), qPrintable(error));
    QCOMPARE(series.size(), 82);
    QCOMPARE(series[0].samples.size(), 2);
    QCOMPARE(series[0].samples[0].y(), -12.34);
    QCOMPARE(series[0].samples[1].x(), 1.0);
}

void CoreTests::hexTextAndExport() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(kFramedFormat, format, error), qPrintable(error));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString inputPath = dir.filePath(QStringLiteral("mixed.txt"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    const QByteArray text = "\xe6\x8e\xa5\xe6\x94\xb6: AA 55 90 01 \xe6\x97\xa0\xe6\x95\x88\n"
                            "0xAA, 0x55, 0x91, 0x01\n";
    QCOMPARE(input.write(text), qint64(text.size()));
    input.close();

    pat::RecordParser parser(format);
    QVector<pat::Series> series;
    QVERIFY2(parser.ParseFile(inputPath, series, error), qPrintable(error));
    QCOMPARE(series[0].samples.size(), 2);

    pat::FrameExportOptions options;
    options.category = pat::FrameCategory::ByteIndex;
    options.byteIndex = 2;
    pat::FrameExportResult result;
    const QString outputPath = dir.filePath(QStringLiteral("frames.txt"));
    QVERIFY2(pat::ExportFrames(inputPath, outputPath, format, options, result, error), qPrintable(error));
    QCOMPARE(result.frameCount, quint64(2));
    QCOMPARE(result.skippedTextTokens, quint64(2));
    QFile output(outputPath);
    QVERIFY(output.open(QIODevice::ReadOnly));
    const QByteArray exported = output.readAll();
    QVERIFY(exported.startsWith("\xe5\x88\x86\xe7\xb1\xbb\t\xe5\xb8\xa7\xe6\x95\xb0\xe6\x8d\xae\n"));
    QVERIFY(exported.contains("90\tAA 55 90 01\n"));
    QVERIFY(exported.contains("91\tAA 55 91 01\n"));

    options.format = pat::FrameExportFormat::Csv;
    options.category = pat::FrameCategory::SignalValue;
    options.signalIndex = 0;
    const QString csvPath = dir.filePath(QStringLiteral("frames.csv"));
    QVERIFY2(pat::ExportFrames(inputPath, csvPath, format, options, result, error), qPrintable(error));
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const QList<QByteArray> csvLines = csv.readAll().trimmed().split('\n');
    QCOMPARE(csvLines.size(), 3);
    QVERIFY(csvLines[0].startsWith("\xe5\x88\x86\xe7\xb1\xbb,"));
    QVERIFY(qAbs(csvLines[1].split(',').first().toDouble()) < 1e-9);
    QVERIFY(qAbs(csvLines[2].split(',').first().toDouble() - 0.1) < 1e-9);
}

void CoreTests::longContinuousHexText() {
    QByteArray text(1200000, 'A');
    QBuffer input(&text);
    QVERIFY(input.open(QIODevice::ReadOnly));
    pat::InputReadStats stats;
    QString error;
    quint64 consumed = 0;
    const auto consume = [&](const QByteArray& bytes) {
        consumed += static_cast<quint64>(bytes.size());
        return bytes.count(char(0xAA)) == bytes.size();
    };
    QVERIFY2(pat::ReadInputBytes(input, true, consume, stats, error), qPrintable(error));
    QCOMPARE(consumed, quint64(600000));
    QCOMPARE(stats.validBytes, consumed);
}

void CoreTests::liveBufferBounded() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(kFramedFormat, format, error), qPrintable(error));
    pat::LiveDataSession session;
    session.Reset(format, 2);
    QVERIFY(session.Append(0, {1.0}));
    QVERIFY(session.Append(1, {2.0}));
    QVERIFY(session.Append(2, {3.0}));
    session.UpdateStatistics();
    QCOMPARE(session.SeriesData()[0].samples.size(), 2);
    QCOMPARE(session.SeriesData()[0].samples[0], QPointF(1.0, 2.0));
    QCOMPARE(session.Statistics().maxX, 2.0);
    QCOMPARE(session.Statistics().minY, 2.0);
    QCOMPARE(session.Statistics().maxY, 3.0);
}

void CoreTests::captureLogsAreIndependentOfDisplay() {
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(kFramedFormat, format, error), qPrintable(error));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    pat::CaptureSession capture(format);
    QVERIFY2(capture.Start(dir.path(), error), qPrintable(error));
    QVector<pat::LiveSample> samples;
    QVERIFY2(capture.Feed(QByteArray::fromHex("ffaa"), samples, error), qPrintable(error));
    QVERIFY(samples.isEmpty());
    QVERIFY2(capture.Feed(QByteArray::fromHex("559001aa559101"), samples, error), qPrintable(error));
    QCOMPARE(samples.size(), 2);
    QCOMPARE(samples[0].index, quint64(0));
    QCOMPARE(samples[1].index, quint64(1));
    QCOMPARE(capture.FrameCount(), quint64(2));
    QCOMPARE(capture.SkippedBytes(), quint64(1));
    QVERIFY2(capture.Stop(error), qPrintable(error));
    QFile raw(capture.RawPath());
    QVERIFY(raw.open(QIODevice::ReadOnly));
    QCOMPARE(raw.readAll(), QByteArray::fromHex("ffaa559001aa559101"));
    QFile csv(capture.CsvPath());
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const QList<QByteArray> lines = csv.readAll().trimmed().split('\n');
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines[0], QByteArray("timestamp,temperature"));
    QVERIFY(lines[1].split(',').first().endsWith('Z'));
    QVERIFY(qAbs(lines[1].split(',').last().toDouble() - 0.0) < 1e-9);
    QVERIFY(qAbs(lines[2].split(',').last().toDouble() - 0.1) < 1e-9);
}

void CoreTests::oneHourSimulatedStream() {
    constexpr auto formatJson = R"({
      "schema_version":2,"template_id":"load_test","record_size":100,
      "endianness":"little",
      "framing":{"kind":"fixed_header","header_hex":"AA55","frame_length":100},
      "signals":[{"name":"counter","byte_offset":2,"value_type":"uint16"}]
    })";
    pat::FormatDefinition format;
    QString error;
    QVERIFY2(pat::LoadFormatFromJsonData(formatJson, format, error), qPrintable(error));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    pat::CaptureSession capture(format);
    QVERIFY2(capture.Start(dir.path(), error), qPrintable(error));
    QCryptographicHash expectedHash(QCryptographicHash::Sha256);
    QVector<pat::LiveSample> samples;
    for (int second = 0; second < 3600; ++second) {
        QByteArray block;
        block.reserve(50 * 100);
        for (int frameNumber = 0; frameNumber < 50; ++frameNumber) {
            const int index = second * 50 + frameNumber;
            QByteArray frame(100, '\0');
            frame[0] = static_cast<char>(0xAA);
            frame[1] = static_cast<char>(0x55);
            frame[2] = static_cast<char>(index & 0xFF);
            frame[3] = static_cast<char>((index >> 8) & 0xFF);
            block.append(frame);
        }
        expectedHash.addData(block);
        QVERIFY2(capture.Feed(block, samples, error), qPrintable(error));
        QCOMPARE(samples.size(), 50);
        QCOMPARE(samples.last().index, quint64(second * 50 + 49));
    }
    QVERIFY2(capture.Stop(error), qPrintable(error));
    QCOMPARE(capture.FrameCount(), quint64(180000));
    QCOMPARE(capture.SkippedBytes(), quint64(0));
    QFile raw(capture.RawPath());
    QVERIFY(raw.open(QIODevice::ReadOnly));
    QCOMPARE(raw.size(), qint64(18000000));
    QCOMPARE(QCryptographicHash::hash(raw.readAll(), QCryptographicHash::Sha256), expectedHash.result());
    QFile csv(capture.CsvPath());
    QVERIFY(csv.open(QIODevice::ReadOnly));
    QCOMPARE(csv.readAll().count('\n'), qsizetype(180001));
}

QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
