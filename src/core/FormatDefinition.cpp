#include "core/FormatDefinition.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace pat {
namespace {

int TypeSize(const QString& type) {
    const auto t = type.toLower();
    if (t == "int16" || t == "uint16") return 2;
    if (t == "int32" || t == "uint32" || t == "float32") return 4;
    if (t == "float64") return 8;
    return 0;
}

bool ParseSignal(const QJsonObject& obj, SignalFormat& outSignal, int recordSize, QString& errorMessage) {
    outSignal.name = obj.value(QStringLiteral("name")).toString();
    if (outSignal.name.isEmpty()) {
        errorMessage = QStringLiteral("signal.name 缺失");
        return false;
    }

    outSignal.valueType = obj.value(QStringLiteral("value_type")).toString().toLower();
    const int size = TypeSize(outSignal.valueType);
    if (size == 0) {
        errorMessage = QStringLiteral("signal '%1' 的 value_type 不支持：%2").arg(outSignal.name, outSignal.valueType);
        return false;
    }

    outSignal.byteOffset = obj.value(QStringLiteral("byte_offset")).toInt(-1);
    if (outSignal.byteOffset < 0) {
        errorMessage = QStringLiteral("signal '%1' 的 byte_offset 缺失或非法").arg(outSignal.name);
        return false;
    }

    if (outSignal.byteOffset + size > recordSize) {
        errorMessage = QStringLiteral("signal '%1' 超出 record_size 边界").arg(outSignal.name);
        return false;
    }

    outSignal.scale = obj.value(QStringLiteral("scale")).toDouble(1.0);
    outSignal.bias = obj.value(QStringLiteral("bias")).toDouble(0.0);
    outSignal.timeScale = obj.value(QStringLiteral("time_scale")).toDouble(1.0);
    if (outSignal.timeScale <= 0.0) outSignal.timeScale = 1.0;
    outSignal.unit = obj.value(QStringLiteral("unit")).toString();
    outSignal.groupPath = obj.value(QStringLiteral("group")).toString();
    return true;
}

bool IsEndiannessSupported(const QString& endianness) {
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

    return LoadFormatFromJsonData(file.readAll(), outFormat, errorMessage);
}

bool LoadFormatFromJsonData(const QByteArray& data, FormatDefinition& outFormat, QString& errorMessage) {
    QJsonParseError parseError{};
    const auto doc = QJsonDocument::fromJson(data, &parseError);
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
    if (!IsEndiannessSupported(outFormat.endianness)) {
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
    outFormat.signalFormats.reserve(static_cast<size_t>(signalsArray.size()));
    for (const auto& sigVal : signalsArray) {
        if (!sigVal.isObject()) {
            errorMessage = QStringLiteral("signals 内元素应为对象");
            return false;
        }
        SignalFormat sig;
        if (!ParseSignal(sigVal.toObject(), sig, outFormat.recordSize, errorMessage)) {
            return false;
        }
        outFormat.signalFormats.push_back(std::move(sig));
    }

    return true;
}

}  // namespace pat
