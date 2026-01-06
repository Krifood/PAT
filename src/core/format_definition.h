#pragma once

#include <QString>
#include <vector>

namespace pat {

struct SignalFormat {
  QString name;
  int byteOffset = 0;
  QString valueType;  // int16, uint16, int32, uint32, float32, float64
  double scale = 1.0;
  double bias = 0.0;
  QString unit;
};

struct FormatDefinition {
  int recordSize = 0;
  QString endianness = QStringLiteral("little");
  std::vector<SignalFormat> signalFormats;
};

bool LoadFormatFromJson(const QString& path, FormatDefinition& outFormat, QString& errorMessage);

}  // namespace pat
