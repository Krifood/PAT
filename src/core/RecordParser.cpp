#include "core/RecordParser.h"

#include "core/RecordDecoder.h"
#include "core/SerialFrameAssembler.h"
#include "core/InputReader.h"

#include <QFile>

#include <utility>

namespace pat {

RecordParser::RecordParser(FormatDefinition format) : format_(std::move(format)) {}

bool RecordParser::ParseFile(const QString& path, QVector<Series>& outSeries, QString& errorMessage) const {
    if (format_.signalFormats.empty() || format_.recordSize <= 0) {
        errorMessage = QStringLiteral("格式缺少有效记录或信号定义");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法打开数据文件：%1").arg(path);
        return false;
    }

    QVector<Series> parsed(static_cast<qsizetype>(format_.signalFormats.size()));
    for (qsizetype i = 0; i < parsed.size(); ++i) {
        parsed[i].name = format_.signalFormats[static_cast<size_t>(i)].name;
        parsed[i].unit = format_.signalFormats[static_cast<size_t>(i)].unit;
    }

    RecordDecoder decoder(format_);
    SerialFrameAssembler assembler(format_.HasFraming() ? QVector<FormatDefinition>{format_}
                                                        : QVector<FormatDefinition>{});
    if (format_.HasFraming() && !assembler.ConfigurationError().isEmpty()) {
        errorMessage = assembler.ConfigurationError();
        return false;
    }
    QByteArray pending;
    qint64 recordIndex = 0;

    const auto appendRecord = [&](QByteArrayView record) -> bool {
        QVector<double> values;
        if (!decoder.Decode(record, values, errorMessage)) return false;
        for (qsizetype i = 0; i < values.size(); ++i) {
            const double x = format_.HasFraming()
                                 ? static_cast<double>(recordIndex)
                                 : static_cast<double>(recordIndex)
                                       * format_.signalFormats[static_cast<size_t>(i)].timeScale;
            parsed[i].samples.append(QPointF(x, values[i]));
        }
        ++recordIndex;
        return true;
    };

    const auto consume = [&](const QByteArray& chunk) -> bool {
        if (format_.HasFraming()) {
            for (const auto& frame : assembler.Feed(chunk)) {
                if (!appendRecord(frame.bytes)) return false;
            }
        } else {
            pending.append(chunk);
            qsizetype offset = 0;
            while (pending.size() - offset >= format_.recordSize) {
                if (!appendRecord(QByteArrayView(pending.constData() + offset, format_.recordSize))) {
                    return false;
                }
                offset += format_.recordSize;
            }
            if (offset > 0) pending.remove(0, offset);
        }
        return true;
    };
    InputReadStats readStats;
    if (!ReadInputBytes(file, IsHexTextPath(path), consume, readStats, errorMessage)) return false;

    if (recordIndex == 0) {
        errorMessage = QStringLiteral("数据文件未包含完整记录");
        return false;
    }
    outSeries = std::move(parsed);
    return true;
}

}  // namespace pat
