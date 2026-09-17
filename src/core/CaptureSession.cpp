#include "core/CaptureSession.h"

#include <QDateTime>
#include <QDir>
#include <QUuid>

#include <utility>

namespace pat {
namespace {

QByteArray CsvCell(const QString& text) {
    QByteArray cell = text.toUtf8();
    if (cell.contains(',') || cell.contains('"') || cell.contains('\n') || cell.contains('\r')) {
        cell.replace("\"", "\"\"");
        cell.prepend('"');
        cell.append('"');
    }
    return cell;
}

bool WriteExact(QFile& file, const QByteArray& bytes, const QString& label, QString& errorMessage) {
    if (file.write(bytes) == bytes.size()) return true;
    errorMessage = QStringLiteral("%1写入失败：%2").arg(label, file.errorString());
    return false;
}

}  // namespace

CaptureSession::CaptureSession(FormatDefinition format)
    : format_(std::move(format)), decoder_(format_), assembler_({format_}) {}

bool CaptureSession::Start(const QString& logDirectory, QString& errorMessage) {
    if (started_) {
        errorMessage = QStringLiteral("采集会话已启动");
        return false;
    }
    if (!assembler_.ConfigurationError().isEmpty()) {
        errorMessage = assembler_.ConfigurationError();
        return false;
    }
    if (logDirectory.isEmpty()) {
        errorMessage = QStringLiteral("日志目录为空");
        return false;
    }
    if (!QDir(logDirectory).exists() && !QDir().mkpath(logDirectory)) {
        errorMessage = QStringLiteral("无法创建日志目录：%1").arg(logDirectory);
        return false;
    }
    QDir directory(logDirectory);
    const QString basename = QStringLiteral("pat_%1_%2")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")),
                                      QUuid::createUuid().toString(QUuid::WithoutBraces).left(8));
    rawPath_ = directory.filePath(basename + QStringLiteral(".bin"));
    csvPath_ = directory.filePath(basename + QStringLiteral(".csv"));
    rawLog_ = std::make_unique<QFile>(rawPath_);
    csvLog_ = std::make_unique<QFile>(csvPath_);
    if (!rawLog_->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        errorMessage = QStringLiteral("无法创建原始日志：%1").arg(rawLog_->errorString());
        return false;
    }
    if (!csvLog_->open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        errorMessage = QStringLiteral("无法创建解析日志：%1").arg(csvLog_->errorString());
        rawLog_->close();
        return false;
    }
    QByteArray header = "timestamp";
    for (const auto& signal : format_.signalFormats) {
        header.append(',');
        header.append(CsvCell(signal.name));
    }
    header.append('\n');
    if (!WriteExact(*csvLog_, header, QStringLiteral("解析日志"), errorMessage)) {
        rawLog_->close();
        csvLog_->close();
        return false;
    }
    started_ = true;
    return true;
}

bool CaptureSession::Feed(const QByteArray& bytes,
                          QVector<LiveSample>& samples,
                          QString& errorMessage) {
    samples.clear();
    if (!started_ || !rawLog_ || !csvLog_) {
        errorMessage = QStringLiteral("采集会话尚未启动");
        return false;
    }
    if (!WriteExact(*rawLog_, bytes, QStringLiteral("原始日志"), errorMessage)) return false;
    for (const auto& frame : assembler_.Feed(bytes)) {
        QVector<double> values;
        if (!decoder_.Decode(frame.bytes, values, errorMessage)) return false;
        QByteArray row = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8();
        for (double value : values) {
            row.append(',');
            row.append(QByteArray::number(value, 'g', 17));
        }
        row.append('\n');
        if (!WriteExact(*csvLog_, row, QStringLiteral("解析日志"), errorMessage)) return false;
        samples.append({nextSampleIndex_, std::move(values)});
        ++nextSampleIndex_;
    }
    return true;
}

bool CaptureSession::Stop(QString& errorMessage) {
    if (!started_) return true;
    started_ = false;
    bool success = true;
    if (rawLog_) {
        if (!rawLog_->flush()) {
            errorMessage = QStringLiteral("原始日志刷新失败：%1").arg(rawLog_->errorString());
            success = false;
        }
        rawLog_->close();
    }
    if (csvLog_) {
        if (!csvLog_->flush()) {
            errorMessage = QStringLiteral("解析日志刷新失败：%1").arg(csvLog_->errorString());
            success = false;
        }
        csvLog_->close();
    }
    return success;
}

}  // namespace pat
