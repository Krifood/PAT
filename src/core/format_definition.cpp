#include "core/format_definition.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace pat {
namespace {

int typeSize(const QString& type) {
  const auto t = type.toLower();
  if (t == "int16" || t == "uint16") return 2;
  if (t == "int32" || t == "uint32" || t == "float32") return 4;
  if (t == "float64") return 8;
  return 0;
}

bool parseSignal(const QJsonObject& obj, SignalFormat& outSignal, int recordSize, QString& errorMessage) {
  outSignal.name = obj.value(QStringLiteral("name")).toString();
  if (outSignal.name.isEmpty()) {
    errorMessage = QStringLiteral("signal.name 缺失");
    return false;
  }

  outSignal.valueType = obj.value(QStringLiteral("value_type")).toString().toLower();
  const int sz = typeSize(outSignal.valueType);
  if (sz == 0) {
    errorMessage = QStringLiteral("signal '%1' 的 value_type 不支持：%2").arg(outSignal.name, outSignal.valueType);
    return false;
  }

  outSignal.byteOffset = obj.value(QStringLiteral("byte_offset")).toInt(-1);
  if (outSignal.byteOffset < 0) {
    errorMessage = QStringLiteral("signal '%1' 的 byte_offset 缺失或非法").arg(outSignal.name);
    return false;
  }

  if (outSignal.byteOffset + sz > recordSize) {
    errorMessage = QStringLiteral("signal '%1' 超出 record_size 边界").arg(outSignal.name);
    return false;
  }

  outSignal.scale = obj.value(QStringLiteral("scale")).toDouble(1.0);
  outSignal.bias = obj.value(QStringLiteral("bias")).toDouble(0.0);
  outSignal.unit = obj.value(QStringLiteral("unit")).toString();
  return true;
}

bool isEndiannessSupported(const QString& endianness) {
  const auto e = endianness.toLower();
  return e == "little" || e == "big";
}

}  // namespace

bool LoadFormatFromJson(const QString& path, FormatDefinition& outFormat, QString& errorMessage) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    errorMessage = QStringLiteral("无法打开格式文件：%1").arg(path);
    return false;
  }

  QJsonParseError parseError{};
  const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    errorMessage = QStringLiteral("JSON 解析失败：%1").arg(parseError.errorString());
    return false;
  }

  const auto root = doc.object();

  outFormat.recordSize = root.value(QStringLiteral("record_size")).toInt(0);
  if (outFormat.recordSize <= 0) {
    errorMessage = QStringLiteral("record_size 缺失或非法");
    return false;
  }

  outFormat.endianness = root.value(QStringLiteral("endianness")).toString(QStringLiteral("little")).toLower();
  if (!isEndiannessSupported(outFormat.endianness)) {
    errorMessage = QStringLiteral("endianness 不支持：%1").arg(outFormat.endianness);
    return false;
  }

  const auto signalsValue = root.value(QStringLiteral("signals"));
  if (!signalsValue.isArray()) {
    errorMessage = QStringLiteral("signals 应为数组");
    return false;
  }

  const auto signalsArray = signalsValue.toArray();
  if (signalsArray.isEmpty()) {
    errorMessage = QStringLiteral("signals 为空");
    return false;
  }

  outFormat.signalFormats.clear();
  outFormat.signalFormats.reserve(signalsArray.size());
  for (const auto& sigVal : signalsArray) {
    if (!sigVal.isObject()) {
      errorMessage = QStringLiteral("signals 内元素应为对象");
      return false;
    }
    SignalFormat sig;
    if (!parseSignal(sigVal.toObject(), sig, outFormat.recordSize, errorMessage)) {
      return false;
    }
    outFormat.signalFormats.push_back(std::move(sig));
  }

  return true;
}

}  // namespace pat
