#include "core/RecordDecoder.h"

#include <QtEndian>

#include <bit>
#include <cstdint>
#include <utility>

namespace pat {
namespace {

template <typename Unsigned>
Unsigned ReadUnsigned(const uchar* data, bool little = true) {
    if constexpr (sizeof(Unsigned) == 1) {
        return *data;
    } else {
        return little ? qFromLittleEndian<Unsigned>(data) : qFromBigEndian<Unsigned>(data);
    }
}

bool DecodeValue(const SignalFormat& signal, const uchar* data, bool little, double& value) {
    const auto type = signal.valueType.toLower();
    if (signal.bitWidth > 0) {
        quint64 raw = 0;
        if (type == "uint8") raw = ReadUnsigned<quint8>(data);
        else if (type == "uint16") raw = ReadUnsigned<quint16>(data, little);
        else if (type == "uint32") raw = ReadUnsigned<quint32>(data, little);
        else return false;
        const quint64 mask = (quint64{1} << signal.bitWidth) - 1;
        value = static_cast<double>((raw >> signal.bitOffset) & mask);
    } else if (type == "int8") value = std::bit_cast<qint8>(ReadUnsigned<quint8>(data));
    else if (type == "uint8") value = ReadUnsigned<quint8>(data);
    else if (type == "int16") value = std::bit_cast<qint16>(ReadUnsigned<quint16>(data, little));
    else if (type == "uint16") value = ReadUnsigned<quint16>(data, little);
    else if (type == "int32") value = std::bit_cast<qint32>(ReadUnsigned<quint32>(data, little));
    else if (type == "uint32") value = ReadUnsigned<quint32>(data, little);
    else if (type == "float32") value = std::bit_cast<float>(ReadUnsigned<quint32>(data, little));
    else if (type == "float64") value = std::bit_cast<double>(ReadUnsigned<quint64>(data, little));
    else return false;
    value = value * signal.scale + signal.bias;
    return true;
}

int ValueSize(const QString& type) {
    if (type == "int8" || type == "uint8") return 1;
    if (type == "int16" || type == "uint16") return 2;
    if (type == "int32" || type == "uint32" || type == "float32") return 4;
    if (type == "float64") return 8;
    return 0;
}

}  // namespace

RecordDecoder::RecordDecoder(FormatDefinition format) : format_(std::move(format)) {}

bool RecordDecoder::Decode(QByteArrayView record, QVector<double>& values, QString& errorMessage) const {
    if (record.size() != format_.recordSize) {
        errorMessage = QStringLiteral("记录长度不匹配：期望 %1，实际 %2")
                           .arg(format_.recordSize).arg(record.size());
        return false;
    }
    values.clear();
    values.reserve(static_cast<qsizetype>(format_.signalFormats.size()));
    const bool little = format_.endianness == QStringLiteral("little");
    for (const auto& signal : format_.signalFormats) {
        const int size = ValueSize(signal.valueType);
        if (size == 0 || signal.byteOffset < 0 || signal.byteOffset > record.size() - size) {
            errorMessage = QStringLiteral("信号 '%1' 超出记录边界或类型不支持").arg(signal.name);
            return false;
        }
        double value = 0.0;
        if (!DecodeValue(signal,
                         reinterpret_cast<const uchar*>(record.constData() + signal.byteOffset),
                         little,
                         value)) {
            errorMessage = QStringLiteral("信号 '%1' 类型不支持：%2").arg(signal.name, signal.valueType);
            return false;
        }
        values.append(value);
    }
    return true;
}

}  // namespace pat
