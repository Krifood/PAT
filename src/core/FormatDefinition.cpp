#include "core/FormatDefinition.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>
#include <limits>
#include <utility>

namespace pat {
namespace {

int TypeSize(const QString& type) {
    const auto t = type.toLower();
    if (t == "int8" || t == "uint8") return 1;
    if (t == "int16" || t == "uint16") return 2;
    if (t == "int32" || t == "uint32" || t == "float32") return 4;
    if (t == "int64" || t == "uint64" || t == "float64") return 8;
    return 0;
}

bool NormalizeTimeUnit(const QString& raw, QString& outUnit, double& outFactor) {
    const QString u = raw.trimmed().toLower();
    if (u.isEmpty() || u == "s" || u == "sec" || u == "secs" || u == "second" || u == "seconds") {
        outUnit = QStringLiteral("s");
        outFactor = 1.0;
        return true;
    }
    if (u == "ms" || u == "msec" || u == "msecs" || u == "millisecond" || u == "milliseconds") {
        outUnit = QStringLiteral("ms");
        outFactor = 1e-3;
        return true;
    }
    if (u == "us" || u == "usec" || u == "usecs" || u == "microsecond" || u == "microseconds") {
        outUnit = QStringLiteral("us");
        outFactor = 1e-6;
        return true;
    }
    if (u == "ns" || u == "nsec" || u == "nsecs" || u == "nanosecond" || u == "nanoseconds") {
        outUnit = QStringLiteral("ns");
        outFactor = 1e-9;
        return true;
    }
    return false;
}

bool ParseSignal(const QJsonObject& obj,
                 SignalFormat& outSignal,
                 int recordSize,
                 double axisUnitSeconds,
                 const QString& axisUnitLabel,
                 QString& errorMessage) {
    outSignal.name = obj.value(QStringLiteral("name")).toString();
    if (outSignal.name.isEmpty()) {
        errorMessage = QStringLiteral("signal.name 缺失");
        return false;
    }
    outSignal.displayName = outSignal.name;

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

    const bool hasBitOffset = obj.contains(QStringLiteral("bit_offset"));
    const bool hasBitWidth = obj.contains(QStringLiteral("bit_width"));
    if (hasBitOffset != hasBitWidth) {
        errorMessage = QStringLiteral("signal '%1' 的 bit_offset 和 bit_width 必须同时填写").arg(outSignal.name);
        return false;
    }
    if (hasBitOffset) {
        outSignal.bitOffset = obj.value(QStringLiteral("bit_offset")).toInt(-1);
        outSignal.bitWidth = obj.value(QStringLiteral("bit_width")).toInt(-1);
        const int bits = size * 8;
        if (!(outSignal.valueType == QStringLiteral("uint8")
              || outSignal.valueType == QStringLiteral("uint16")
              || outSignal.valueType == QStringLiteral("uint32"))
            || outSignal.bitOffset < 0 || outSignal.bitOffset >= bits
            || outSignal.bitWidth <= 0 || outSignal.bitWidth > bits - outSignal.bitOffset) {
            errorMessage = QStringLiteral("signal '%1' 的位段范围非法或类型不支持").arg(outSignal.name);
            return false;
        }
    }

    outSignal.scale = obj.value(QStringLiteral("scale")).toDouble(1.0);
    outSignal.bias = obj.value(QStringLiteral("bias")).toDouble(0.0);
    double timeScale = obj.value(QStringLiteral("time_scale")).toDouble(1.0);
    if (timeScale <= 0.0) timeScale = 1.0;
    double signalUnitSeconds = axisUnitSeconds;
    const QString signalUnitRaw = obj.value(QStringLiteral("time_unit")).toString();
    if (!signalUnitRaw.trimmed().isEmpty()) {
        double normalizedSeconds = 1.0;
        QString normalizedLabel;
        if (NormalizeTimeUnit(signalUnitRaw, normalizedLabel, normalizedSeconds)) {
            signalUnitSeconds = normalizedSeconds;
        }
    }
    if (axisUnitSeconds <= 0.0) axisUnitSeconds = 1.0;
    const double timeScaleSeconds = timeScale * signalUnitSeconds;
    outSignal.timeScale = timeScaleSeconds / axisUnitSeconds;
    outSignal.timeUnit = axisUnitLabel;
    outSignal.unit = obj.value(QStringLiteral("unit")).toString();
    outSignal.description = obj.value(QStringLiteral("description")).toString();
    outSignal.groupPath = obj.value(QStringLiteral("group")).toString();
    return true;
}

bool IsEndiannessSupported(const QString& endianness) {
    const auto e = endianness.toLower();
    return e == "little" || e == "big";
}

bool ParseMixedFormat(const QJsonObject& root,
                      FormatDefinition& outFormat,
                      QString& errorMessage) {
    const auto storageValue = root.value(QStringLiteral("storage"));
    if (!storageValue.isObject()) {
        errorMessage = QStringLiteral("schema_version 3 缺少 storage 对象");
        return false;
    }
    const auto storage = storageValue.toObject();
    outFormat.storageKind = storage.value(QStringLiteral("kind")).toString();
    outFormat.storageVersion = storage.value(QStringLiteral("version")).toInt(0);
    outFormat.layoutSha256 = storage.value(QStringLiteral("layout_sha256")).toString().toLower();
    outFormat.dataFile = storage.value(QStringLiteral("data_file")).toString();
    if (outFormat.storageKind != QStringLiteral("indexed_stream_chunks")
        || outFormat.storageVersion != 1) {
        errorMessage = QStringLiteral("storage.kind/version 不支持：%1/%2")
                           .arg(outFormat.storageKind).arg(outFormat.storageVersion);
        return false;
    }
    if (outFormat.layoutSha256.size() != 64) {
        errorMessage = QStringLiteral("storage.layout_sha256 必须为 64 位十六进制字符串");
        return false;
    }
    for (const QChar ch : outFormat.layoutSha256) {
        if (!(ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            && !(ch >= QLatin1Char('a') && ch <= QLatin1Char('f'))) {
            errorMessage = QStringLiteral("storage.layout_sha256 含非法字符");
            return false;
        }
    }

    const auto timestampValue = root.value(QStringLiteral("timestamp"));
    if (!timestampValue.isObject()) {
        errorMessage = QStringLiteral("schema_version 3 缺少 timestamp 对象");
        return false;
    }
    const auto timestamp = timestampValue.toObject();
    outFormat.timestampValueType = timestamp.value(QStringLiteral("value_type")).toString().toLower();
    if (outFormat.timestampValueType != QStringLiteral("uint64")) {
        errorMessage = QStringLiteral("timestamp.value_type 当前仅支持 uint64");
        return false;
    }
    outFormat.timestampUnit = timestamp.value(QStringLiteral("unit")).toString(QStringLiteral("tick"));
    outFormat.timestampScaleKnown = timestamp.value(QStringLiteral("scale")).isDouble();
    outFormat.timestampScale = outFormat.timestampScaleKnown
        ? timestamp.value(QStringLiteral("scale")).toDouble() : 1.0;
    if (outFormat.timestampScale <= 0.0 || !std::isfinite(outFormat.timestampScale)) {
        errorMessage = QStringLiteral("timestamp.scale 必须为正数或 null");
        return false;
    }
    outFormat.timeAxisUnit = outFormat.timestampUnit.isEmpty()
        ? QStringLiteral("tick") : outFormat.timestampUnit;

    const auto streamsValue = root.value(QStringLiteral("streams"));
    if (!streamsValue.isArray() || streamsValue.toArray().isEmpty()) {
        errorMessage = QStringLiteral("streams 应为非空数组");
        return false;
    }
    QSet<quint32> streamIds;
    QSet<QString> flatNames;
    for (const auto& streamValue : streamsValue.toArray()) {
        if (!streamValue.isObject()) {
            errorMessage = QStringLiteral("streams 内元素应为对象");
            return false;
        }
        const auto streamObject = streamValue.toObject();
        StreamFormat stream;
        const qint64 id = streamObject.value(QStringLiteral("stream_id")).toVariant().toLongLong();
        if (id <= 0 || id > std::numeric_limits<quint32>::max() || streamIds.contains(quint32(id))) {
            errorMessage = QStringLiteral("stream_id 缺失、非法或重复：%1").arg(id);
            return false;
        }
        stream.streamId = quint32(id);
        streamIds.insert(stream.streamId);
        stream.sourceMessageId = streamObject.value(QStringLiteral("source_message_id")).toString();
        stream.name = streamObject.value(QStringLiteral("name")).toString().trimmed();
        stream.payloadLength = streamObject.value(QStringLiteral("payload_length")).toInt(0);
        stream.endianness = streamObject.value(QStringLiteral("endianness")).toString(QStringLiteral("big")).toLower();
        if (stream.name.isEmpty() || stream.payloadLength <= 0
            || !IsEndiannessSupported(stream.endianness)) {
            errorMessage = QStringLiteral("stream %1 的名称、负载长度或字节序非法").arg(stream.streamId);
            return false;
        }
        const auto signalsValue = streamObject.value(QStringLiteral("signals"));
        if (!signalsValue.isArray() || signalsValue.toArray().isEmpty()) {
            errorMessage = QStringLiteral("stream %1 的 signals 为空").arg(stream.streamId);
            return false;
        }
        stream.firstSignalIndex = static_cast<int>(outFormat.signalFormats.size());
        QSet<QString> localNames;
        for (const auto& signalValue : signalsValue.toArray()) {
            if (!signalValue.isObject()) {
                errorMessage = QStringLiteral("stream %1 的 signals 内元素应为对象").arg(stream.streamId);
                return false;
            }
            SignalFormat signal;
            if (!ParseSignal(signalValue.toObject(), signal, stream.payloadLength,
                             1.0, outFormat.timeAxisUnit, errorMessage)) return false;
            if (localNames.contains(signal.name)) {
                errorMessage = QStringLiteral("stream %1 的 signal.name 重复：%2")
                                   .arg(stream.streamId).arg(signal.name);
                return false;
            }
            localNames.insert(signal.name);
            const QString localName = signal.name;
            signal.name = QStringLiteral("%1.%2").arg(stream.name, localName);
            if (flatNames.contains(signal.name)) {
                signal.name = QStringLiteral("%1[%2].%3")
                                  .arg(stream.name).arg(stream.streamId).arg(localName);
            }
            flatNames.insert(signal.name);
            if (signal.groupPath.trimmed().isEmpty()) {
                signal.groupPath = QStringLiteral("%1 [%2]").arg(stream.name, stream.sourceMessageId);
            }
            signal.timeScale = outFormat.timestampScale;
            signal.timeUnit = outFormat.timeAxisUnit;
            outFormat.signalFormats.push_back(std::move(signal));
        }
        stream.signalCount = static_cast<int>(outFormat.signalFormats.size()) - stream.firstSignalIndex;
        outFormat.streamFormats.push_back(std::move(stream));
    }
    outFormat.templateId = root.value(QStringLiteral("template_id")).toString().trimmed();
    if (outFormat.templateId.isEmpty()) {
        errorMessage = QStringLiteral("template_id 缺失");
        return false;
    }
    return true;
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

    outFormat = FormatDefinition{};
    outFormat.schemaVersion = root.value(QStringLiteral("schema_version")).toInt(1);
    if (outFormat.schemaVersion == 3) {
        return ParseMixedFormat(root, outFormat, errorMessage);
    }
    if (outFormat.schemaVersion != 1 && outFormat.schemaVersion != 2) {
        errorMessage = QStringLiteral("schema_version 不支持：%1").arg(outFormat.schemaVersion);
        return false;
    }
    if (root.contains(QStringLiteral("framing"))) {
        if (outFormat.schemaVersion != 2 || !root.value(QStringLiteral("framing")).isObject()) {
            errorMessage = QStringLiteral("framing 仅支持 schema_version 2 的对象格式");
            return false;
        }
        const auto framing = root.value(QStringLiteral("framing")).toObject();
        if (framing.value(QStringLiteral("kind")).toString() != QStringLiteral("fixed_header")) {
            errorMessage = QStringLiteral("framing.kind 必须为 fixed_header");
            return false;
        }
        const QString headerHex = framing.value(QStringLiteral("header_hex")).toString().trimmed();
        for (const QChar ch : headerHex) {
            const bool decimal = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
            const bool hexLetter = ch.toLower() >= QLatin1Char('a') && ch.toLower() <= QLatin1Char('f');
            if (!decimal && !hexLetter) {
                errorMessage = QStringLiteral("framing.header_hex 必须为连续十六进制字符");
                return false;
            }
        }
        if (headerHex.isEmpty() || headerHex.size() % 2 != 0) {
            errorMessage = QStringLiteral("framing.header_hex 长度必须为偶数且非空");
            return false;
        }
        outFormat.frameHeader = QByteArray::fromHex(headerHex.toLatin1());
        outFormat.templateId = root.value(QStringLiteral("template_id")).toString().trimmed();
        if (outFormat.templateId.isEmpty()) {
            errorMessage = QStringLiteral("template_id 缺失");
            return false;
        }
    }

    outFormat.recordSize = root.value(QStringLiteral("record_size")).toInt(0);
    if (outFormat.recordSize <= 0) {
        errorMessage = QStringLiteral("record_size 缺失或非法");
        return false;
    }
    if (outFormat.HasFraming()) {
        const auto framing = root.value(QStringLiteral("framing")).toObject();
        const int frameLength = framing.value(QStringLiteral("frame_length")).toInt(0);
        if (frameLength != outFormat.recordSize || frameLength < outFormat.frameHeader.size()) {
            errorMessage = QStringLiteral("framing.frame_length 必须等于 record_size 且不小于帧头长度");
            return false;
        }
        if (framing.contains(QStringLiteral("checksum"))) {
            if (!framing.value(QStringLiteral("checksum")).isObject()) {
                errorMessage = QStringLiteral("framing.checksum 必须为对象");
                return false;
            }
            const auto checksum = framing.value(QStringLiteral("checksum")).toObject();
            if (checksum.value(QStringLiteral("kind")).toString() != QStringLiteral("sum8")) {
                errorMessage = QStringLiteral("framing.checksum.kind 仅支持 sum8");
                return false;
            }
            outFormat.checksumStartByte = checksum.value(QStringLiteral("start_byte")).toInt(-1);
            outFormat.checksumEndByte = checksum.value(QStringLiteral("end_byte")).toInt(-1);
            outFormat.checksumByteOffset = checksum.value(QStringLiteral("byte_offset")).toInt(-1);
            if (outFormat.checksumStartByte < 0
                || outFormat.checksumEndByte < outFormat.checksumStartByte
                || outFormat.checksumEndByte >= outFormat.recordSize
                || outFormat.checksumByteOffset < 0
                || outFormat.checksumByteOffset >= outFormat.recordSize
                || (outFormat.checksumByteOffset >= outFormat.checksumStartByte
                    && outFormat.checksumByteOffset <= outFormat.checksumEndByte)) {
                errorMessage = QStringLiteral("framing.checksum 字节范围非法或包含校验字节");
                return false;
            }
        }
    }

    outFormat.endianness = root.value(QStringLiteral("endianness")).toString(QStringLiteral("little")).toLower();
    if (!IsEndiannessSupported(outFormat.endianness)) {
        errorMessage = QStringLiteral("endianness 不支持：%1").arg(outFormat.endianness);
        return false;
    }

    outFormat.timeAxisUnit = QStringLiteral("s");
    double axisUnitSeconds = 1.0;
    const QString axisUnitRaw = root.value(QStringLiteral("time_unit")).toString();
    NormalizeTimeUnit(axisUnitRaw, outFormat.timeAxisUnit, axisUnitSeconds);

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
    QSet<QString> signalNames;
    for (const auto& sigVal : signalsArray) {
        if (!sigVal.isObject()) {
            errorMessage = QStringLiteral("signals 内元素应为对象");
            return false;
        }
        SignalFormat sig;
        if (!ParseSignal(sigVal.toObject(),
                         sig,
                         outFormat.recordSize,
                         axisUnitSeconds,
                         outFormat.timeAxisUnit,
                         errorMessage)) {
            return false;
        }
        if (outFormat.schemaVersion == 2) {
            if (signalNames.contains(sig.name)) {
                errorMessage = QStringLiteral("signal.name 重复：%1").arg(sig.name);
                return false;
            }
            if (!std::isfinite(sig.scale) || !std::isfinite(sig.bias)) {
                errorMessage = QStringLiteral("signal '%1' 的 scale 或 bias 非法").arg(sig.name);
                return false;
            }
            signalNames.insert(sig.name);
        }
        outFormat.signalFormats.push_back(std::move(sig));
    }

    outFormat.groupDescriptions.clear();
    const auto groupsValue = root.value(QStringLiteral("groups"));
    if (groupsValue.isArray()) {
        const auto groupsArray = groupsValue.toArray();
        for (const auto& groupValue : groupsArray) {
            if (!groupValue.isObject()) continue;
            const auto groupObj = groupValue.toObject();
            const QString path = groupObj.value(QStringLiteral("path")).toString().trimmed();
            if (path.isEmpty()) continue;
            const QString description = groupObj.value(QStringLiteral("description")).toString();
            outFormat.groupDescriptions.insert(path, description);
        }
    }

    return true;
}

}  // namespace pat
