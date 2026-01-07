#include "core/RecordParser.h"

#include <QFile>
#include <QtEndian>

#include <cstring>

namespace pat {
namespace {

int TypeSize(const QString& type) {
    const auto t = type.toLower();
    if (t == "int16" || t == "uint16") return 2;
    if (t == "int32" || t == "uint32" || t == "float32") return 4;
    if (t == "float64") return 8;
    return 0;
}

template <typename T>
T ReadLittle(const char* data) {
    T value{};
    std::memcpy(&value, data, sizeof(T));
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    if constexpr (sizeof(T) == 2) {
        value = static_cast<T>(qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data)));
    } else if constexpr (sizeof(T) == 4) {
        value = static_cast<T>(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data)));
    } else if constexpr (sizeof(T) == 8) {
        value = static_cast<T>(qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(data)));
    }
#endif
    return value;
}

bool DecodeValue(const SignalFormat& sig, const char* recordBase, double& outValue) {
    const char* ptr = recordBase + sig.byteOffset;
    const auto t = sig.valueType.toLower();

    if (t == "int16") {
        const auto v = ReadLittle<qint16>(ptr);
        outValue = static_cast<double>(v) * sig.scale + sig.bias;
        return true;
    }
    if (t == "uint16") {
        const auto v = ReadLittle<quint16>(ptr);
        outValue = static_cast<double>(v) * sig.scale + sig.bias;
        return true;
    }
    if (t == "int32") {
        const auto v = ReadLittle<qint32>(ptr);
        outValue = static_cast<double>(v) * sig.scale + sig.bias;
        return true;
    }
    if (t == "uint32") {
        const auto v = ReadLittle<quint32>(ptr);
        outValue = static_cast<double>(v) * sig.scale + sig.bias;
        return true;
    }
    if (t == "float32") {
        quint32 raw = ReadLittle<quint32>(ptr);
        float v{};
        std::memcpy(&v, &raw, sizeof(float));
        outValue = static_cast<double>(v) * sig.scale + sig.bias;
        return true;
    }
    if (t == "float64") {
        quint64 raw = ReadLittle<quint64>(ptr);
        double v{};
        std::memcpy(&v, &raw, sizeof(double));
        outValue = v * sig.scale + sig.bias;
        return true;
    }
    return false;
}

}  // namespace

RecordParser::RecordParser(FormatDefinition format) : format_(std::move(format)) {}

bool RecordParser::ParseFile(const QString& path, QVector<Series>& outSeries, QString& errorMessage) const {
    if (format_.signalFormats.empty()) {
        errorMessage = QStringLiteral("格式未包含信号定义");
        return false;
    }
    if (format_.recordSize <= 0) {
        errorMessage = QStringLiteral("record_size 非法");
        return false;
    }
    if (format_.endianness != QStringLiteral("little")) {
        errorMessage = QStringLiteral("当前仅支持 little-endian");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法打开数据文件：%1").arg(path);
        return false;
    }

    const QByteArray buffer = file.readAll();
    if (buffer.size() < format_.recordSize) {
        errorMessage = QStringLiteral("数据长度不足一个记录");
        return false;
    }

    const int recordCount = buffer.size() / format_.recordSize;
    const int signalCount = static_cast<int>(format_.signalFormats.size());

    outSeries.clear();
    outSeries.resize(signalCount);
    for (int i = 0; i < signalCount; ++i) {
        outSeries[i].name = format_.signalFormats[i].name;
        outSeries[i].unit = format_.signalFormats[i].unit;
    }

    for (int recordIndex = 0; recordIndex < recordCount; ++recordIndex) {
        const char* recordBase = buffer.constData() + recordIndex * format_.recordSize;
        for (int s = 0; s < signalCount; ++s) {
            const auto& sig = format_.signalFormats[s];
            const int size = TypeSize(sig.valueType);
            if (sig.byteOffset + size > format_.recordSize) {
                errorMessage = QStringLiteral("信号 '%1' 超出记录长度").arg(sig.name);
                return false;
            }
            double value{};
            if (!DecodeValue(sig, recordBase, value)) {
                errorMessage = QStringLiteral("信号 '%1' 类型不支持：%2").arg(sig.name, sig.valueType);
                return false;
            }
            outSeries[s].samples.append(QPointF(static_cast<double>(recordIndex), value));
        }
    }

    return true;
}

}  // namespace pat
