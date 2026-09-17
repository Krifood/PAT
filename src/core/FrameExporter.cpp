#include "core/FrameExporter.h"

#include "core/InputReader.h"
#include "core/RecordDecoder.h"
#include "core/SerialFrameAssembler.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace pat {
namespace {

QByteArray CsvCell(QByteArray value) {
    if (value.contains(',') || value.contains('"') || value.contains('\n')) {
        value.replace("\"", "\"\"");
        value.prepend('"');
        value.append('"');
    }
    return value;
}

QByteArray Category(const MatchedFrame& frame,
                    const FormatDefinition& format,
                    const FrameExportOptions& options) {
    switch (options.category) {
    case FrameCategory::Template:
        return format.templateId.toUtf8();
    case FrameCategory::FullFrame:
        return frame.bytes.toHex().toUpper();
    case FrameCategory::ByteIndex:
        return frame.bytes.mid(options.byteIndex, 1).toHex().toUpper();
    case FrameCategory::None:
    case FrameCategory::SignalValue:
        return {};
    }
    return {};
}

}  // namespace

bool ExportFrames(const QString& inputPath,
                  const QString& outputPath,
                  const FormatDefinition& format,
                  const FrameExportOptions& options,
                  FrameExportResult& result,
                  QString& errorMessage) {
    result = {};
    if (!format.HasFraming()) {
        errorMessage = QStringLiteral("当前模板没有 framing 分帧规则");
        return false;
    }
    if (QFileInfo(inputPath).absoluteFilePath() == QFileInfo(outputPath).absoluteFilePath()) {
        errorMessage = QStringLiteral("导出路径不能覆盖输入文件");
        return false;
    }
    if (options.category == FrameCategory::ByteIndex
        && (options.byteIndex < 0 || options.byteIndex >= format.recordSize)) {
        errorMessage = QStringLiteral("分类字节位置超出帧范围");
        return false;
    }
    if (options.category == FrameCategory::SignalValue
        && (options.signalIndex < 0
            || options.signalIndex >= static_cast<int>(format.signalFormats.size()))) {
        errorMessage = QStringLiteral("分类字段位置超出信号范围");
        return false;
    }
    QFile input(inputPath);
    if (!input.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法打开输入：%1").arg(input.errorString());
        return false;
    }
    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        errorMessage = QStringLiteral("无法创建导出文件：%1").arg(output.errorString());
        return false;
    }
    QByteArray header;
    if (options.format == FrameExportFormat::Text) {
        header = options.category == FrameCategory::None ? "帧数据\n" : "分类\t帧数据\n";
    } else {
        if (options.category != FrameCategory::None) header = "分类,";
        for (int i = 0; i < format.recordSize; ++i) {
            if (i > 0) header.append(',');
            header.append(QStringLiteral("字节%1").arg(i + 1).toUtf8());
        }
        header.append('\n');
    }
    if (output.write(header) != header.size()) {
        errorMessage = QStringLiteral("写入表头失败：%1").arg(output.errorString());
        return false;
    }

    SerialFrameAssembler assembler({format});
    RecordDecoder decoder(format);
    if (!assembler.ConfigurationError().isEmpty()) {
        errorMessage = assembler.ConfigurationError();
        return false;
    }
    const auto consume = [&](const QByteArray& bytes) -> bool {
        for (const auto& frame : assembler.Feed(bytes)) {
            QByteArray row;
            if (options.category != FrameCategory::None) {
                QByteArray category;
                if (options.category == FrameCategory::SignalValue) {
                    QVector<double> values;
                    if (!decoder.Decode(frame.bytes, values, errorMessage)) return false;
                    category = QByteArray::number(values[options.signalIndex], 'g', 17);
                } else {
                    category = Category(frame, format, options);
                }
                row.append(options.format == FrameExportFormat::Csv ? CsvCell(category) : category);
                row.append(options.format == FrameExportFormat::Csv ? ',' : '\t');
            }
            if (options.format == FrameExportFormat::Text) {
                row.append(frame.bytes.toHex(' ').toUpper());
            } else {
                for (qsizetype i = 0; i < frame.bytes.size(); ++i) {
                    if (i > 0) row.append(',');
                    row.append(frame.bytes.mid(i, 1).toHex().toUpper());
                }
            }
            row.append('\n');
            if (output.write(row) != row.size()) {
                errorMessage = QStringLiteral("写入帧失败：%1").arg(output.errorString());
                return false;
            }
            ++result.frameCount;
        }
        return true;
    };
    InputReadStats stats;
    if (!ReadInputBytes(input, IsHexTextPath(inputPath), consume, stats, errorMessage)) return false;
    result.skippedBytes = assembler.SkippedBytes();
    result.skippedTextTokens = stats.skippedTokens;
    if (result.frameCount == 0) {
        errorMessage = QStringLiteral("输入中没有完整的数据帧");
        return false;
    }
    if (!output.commit()) {
        errorMessage = QStringLiteral("保存导出文件失败：%1").arg(output.errorString());
        return false;
    }
    return true;
}

}  // namespace pat
