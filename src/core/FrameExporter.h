#pragma once

#include "core/FormatDefinition.h"

namespace pat {

enum class FrameExportFormat { Text, Csv };
enum class FrameCategory { None, Template, FullFrame, ByteIndex, SignalValue };

struct FrameExportOptions {
    FrameExportFormat format = FrameExportFormat::Text;
    FrameCategory category = FrameCategory::None;
    int byteIndex = 0;
    int signalIndex = 0;
};

struct FrameExportResult {
    quint64 frameCount = 0;
    quint64 skippedBytes = 0;
    quint64 skippedTextTokens = 0;
};

bool ExportFrames(const QString& inputPath,
                  const QString& outputPath,
                  const FormatDefinition& format,
                  const FrameExportOptions& options,
                  FrameExportResult& result,
                  QString& errorMessage);

}  // namespace pat
