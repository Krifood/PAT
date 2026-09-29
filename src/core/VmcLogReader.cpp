#include "core/VmcLogReader.h"

#include <QCryptographicHash>
#include <QFile>
#include <QSet>
#include <QtEndian>

namespace pat {
namespace {

constexpr qint64 kGlobalHeaderSize = 84;
constexpr int kDescriptorFixedSize = 22;
constexpr int kRecordFixedSize = 13;

bool ReadExact(QFile& file, qint64 size, QByteArray& output, QString& errorMessage) {
    output = file.read(size);
    if (output.size() != size) {
        errorMessage = QStringLiteral("日志在偏移 0x%1 处截断：需要 %2 字节，实际 %3 字节")
                           .arg(file.pos() - output.size(), 0, 16).arg(size).arg(output.size());
        return false;
    }
    return true;
}

int SourceTypeSize(const QString& sourceType, QString& valueType) {
    const QString type = sourceType.toLower();
    if (type == QStringLiteral("u8")) { valueType = QStringLiteral("uint8"); return 1; }
    if (type == QStringLiteral("i8")) { valueType = QStringLiteral("int8"); return 1; }
    if (type == QStringLiteral("u16")) { valueType = QStringLiteral("uint16"); return 2; }
    if (type == QStringLiteral("i16")) { valueType = QStringLiteral("int16"); return 2; }
    if (type == QStringLiteral("u32")) { valueType = QStringLiteral("uint32"); return 4; }
    if (type == QStringLiteral("i32")) { valueType = QStringLiteral("int32"); return 4; }
    if (type == QStringLiteral("u64")) { valueType = QStringLiteral("uint64"); return 8; }
    if (type == QStringLiteral("i64")) { valueType = QStringLiteral("int64"); return 8; }
    // The source format uses r16 and r32 for IEEE-754 single precision.
    if (type == QStringLiteral("r16") || type == QStringLiteral("r32")) {
        valueType = QStringLiteral("float32");
        return 4;
    }
    if (type == QStringLiteral("r64")) { valueType = QStringLiteral("float64"); return 8; }
    return 0;
}

bool ParseDescriptor(const QByteArray& fixed, const QByteArray& sourceFormat,
                     VmcStreamDescriptor& descriptor, QString& errorMessage) {
    descriptor.sourceControl = quint8(fixed[1]);
    descriptor.messageId = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(fixed.constData() + 2));
    descriptor.topic = QString::fromLatin1(fixed.mid(4, 16).split('\0').first()).trimmed();
    descriptor.sourceFormat = sourceFormat;
    if (descriptor.topic.isEmpty()) {
        errorMessage = QStringLiteral("消息 0x%1 的 topic 为空")
                           .arg(descriptor.messageId, 4, 16, QLatin1Char('0'));
        return false;
    }

    QHash<QString, int> nameCounts;
    int byteOffset = 0;
    const auto fields = QString::fromLatin1(sourceFormat).split(',', Qt::SkipEmptyParts);
    for (const QString& text : fields) {
        const int separator = text.lastIndexOf('@');
        if (separator <= 0 || separator == text.size() - 1) {
            errorMessage = QStringLiteral("消息 0x%1 的字段描述非法：%2")
                               .arg(descriptor.messageId, 4, 16, QLatin1Char('0')).arg(text);
            return false;
        }
        VmcFieldDefinition field;
        field.sourceName = text.left(separator).trimmed();
        field.sourceType = text.mid(separator + 1).trimmed().toLower();
        field.byteSize = SourceTypeSize(field.sourceType, field.valueType);
        if (field.sourceName.isEmpty() || field.byteSize == 0) {
            errorMessage = QStringLiteral("消息 0x%1 的字段或类型不支持：%2")
                               .arg(descriptor.messageId, 4, 16, QLatin1Char('0')).arg(text);
            return false;
        }
        const int occurrence = ++nameCounts[field.sourceName];
        field.name = occurrence == 1 ? field.sourceName
                                     : QStringLiteral("%1_%2").arg(field.sourceName).arg(occurrence);
        field.byteOffset = byteOffset;
        byteOffset += field.byteSize;
        descriptor.fields.append(field);
    }
    if (descriptor.fields.isEmpty() || byteOffset > 255) {
        errorMessage = QStringLiteral("消息 0x%1 的字段为空或负载超过 255 字节")
                           .arg(descriptor.messageId, 4, 16, QLatin1Char('0'));
        return false;
    }
    descriptor.payloadLength = byteOffset;

    QByteArray signature;
    signature.append(char(descriptor.sourceControl));
    signature.append(char(descriptor.messageId & 0xff));
    signature.append(char(descriptor.messageId >> 8));
    signature.append(descriptor.topic.toUtf8());
    signature.append('\0');
    signature.append(sourceFormat);
    signature.append(char(descriptor.payloadLength));
    descriptor.structureSha256 = QCryptographicHash::hash(signature, QCryptographicHash::Sha256);
    return true;
}

}  // namespace

bool VmcLogReader::ReadHeader(const QString& path, VmcSourceHeader& outHeader,
                              QString& errorMessage) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法打开 VMC 日志：%1").arg(path);
        return false;
    }
    if (file.size() < kGlobalHeaderSize + kDescriptorFixedSize) {
        errorMessage = QStringLiteral("VMC 日志过短：%1").arg(path);
        return false;
    }

    VmcSourceHeader parsed;
    if (!ReadExact(file, kGlobalHeaderSize, parsed.globalHeader, errorMessage)) return false;
    while (true) {
        const QByteArray marker = file.peek(1);
        if (marker.isEmpty() || quint8(marker[0]) != 0xA5) break;
        QByteArray fixed;
        if (!ReadExact(file, kDescriptorFixedSize, fixed, errorMessage)) return false;
        const quint16 formatLength = qFromBigEndian<quint16>(
            reinterpret_cast<const uchar*>(fixed.constData() + 20));
        if (formatLength == 0 || formatLength > 16384) {
            errorMessage = QStringLiteral("描述表 format_length 非法：%1").arg(formatLength);
            return false;
        }
        QByteArray sourceFormat;
        if (!ReadExact(file, formatLength, sourceFormat, errorMessage)) return false;
        VmcStreamDescriptor descriptor;
        if (!ParseDescriptor(fixed, sourceFormat, descriptor, errorMessage)) return false;
        if (parsed.descriptorByMessageId.contains(descriptor.messageId)) {
            errorMessage = QStringLiteral("描述表 message_id 重复：0x%1")
                               .arg(descriptor.messageId, 4, 16, QLatin1Char('0'));
            return false;
        }
        parsed.descriptorByMessageId.insert(descriptor.messageId, parsed.descriptors.size());
        parsed.descriptors.append(std::move(descriptor));
    }
    if (parsed.descriptors.isEmpty()) {
        errorMessage = QStringLiteral("未找到 A5 描述表");
        return false;
    }
    parsed.dataOffset = file.pos();
    if (file.peek(1).isEmpty() || quint8(file.peek(1)[0]) != 0x5A) {
        errorMessage = QStringLiteral("描述表后未找到 5A 数据帧");
        return false;
    }
    outHeader = std::move(parsed);
    return true;
}

bool VmcLogReader::VisitRecords(const QString& path, const VmcSourceHeader& header,
                                const RecordVisitor& visitor, VmcReadSummary& outSummary,
                                QString& errorMessage) const {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(header.dataOffset)) {
        errorMessage = QStringLiteral("无法定位 VMC 数据区：%1").arg(path);
        return false;
    }
    VmcReadSummary summary;
    while (!file.atEnd()) {
        const QByteArray marker = file.peek(1);
        if (marker.isEmpty()) break;
        if (quint8(marker[0]) == 0xFF) {
            while (!file.atEnd()) {
                const QByteArray tail = file.read(64 * 1024);
                if (tail.isEmpty() && file.error() != QFileDevice::NoError) {
                    errorMessage = file.errorString(); return false;
                }
                for (char byte : tail) {
                    if (quint8(byte) != 0xFF) {
                        errorMessage = QStringLiteral("数据尾部包含非 FF 字节，偏移 0x%1")
                                           .arg(file.pos() - tail.size(), 0, 16);
                        return false;
                    }
                }
                summary.trailingFillBytes += static_cast<quint64>(tail.size());
            }
            break;
        }
        const qint64 recordOffset = file.pos();
        QByteArray fixed;
        if (!ReadExact(file, kRecordFixedSize, fixed, errorMessage)) return false;
        if (quint8(fixed[0]) != 0x5A) {
            errorMessage = QStringLiteral("数据帧同步字非法，偏移 0x%1").arg(recordOffset, 0, 16);
            return false;
        }
        VmcRecord record;
        record.sourceControl = quint8(fixed[1]);
        record.messageId = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(fixed.constData() + 2));
        record.timestamp = qFromBigEndian<quint64>(
            reinterpret_cast<const uchar*>(fixed.constData() + 4));
        record.sourceSequence = summary.recordCount;
        const int payloadLength = quint8(fixed[12]);
        if (!ReadExact(file, payloadLength, record.payload, errorMessage)) return false;

        const VmcStreamDescriptor* descriptor = nullptr;
        const auto descriptorIt = header.descriptorByMessageId.constFind(record.messageId);
        if (descriptorIt != header.descriptorByMessageId.constEnd()) {
            descriptor = &header.descriptors[*descriptorIt];
            if (descriptor->payloadLength != payloadLength) {
                errorMessage = QStringLiteral("消息 0x%1 在源序号 %2 的负载长度为 %3，描述表要求 %4")
                                   .arg(record.messageId, 4, 16, QLatin1Char('0'))
                                   .arg(record.sourceSequence).arg(payloadLength)
                                   .arg(descriptor->payloadLength);
                return false;
            }
            ++summary.routedRecordCount;
        } else {
            ++summary.metadataRecordCount;
        }
        ++summary.recordCount;
        if (visitor && !visitor(record, descriptor)) {
            errorMessage = QStringLiteral("VMC 记录处理被终止，源序号 %1").arg(record.sourceSequence);
            return false;
        }
    }
    outSummary = summary;
    return true;
}

}  // namespace pat
