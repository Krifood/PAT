#pragma once
#include "core/IndexedMixedReader.h"
#include "core/LogCrc32.h"
#include <QFile>
#include <QtEndian>
namespace pat {
inline bool ReadMixedChunk(QFile& file, const MixedChunkIndex& entry,
                           QByteArray& data, QString& error) {
    if (!file.seek(qint64(entry.chunkOffset))) { error = file.errorString(); return false; }
    // Virtual fixed-record entries exist only in memory; disk PATMBIN1 rejects this flag.
    if (entry.flags == 0x80000000u) {
        data = file.read(entry.dataSize);
        if (data.size() == entry.dataSize) return true;
        error = QStringLiteral("固定记录文件读取不完整"); return false;
    }
    const QByteArray header = file.read(56);
    const auto u32 = [&](int offset) { return qFromLittleEndian<quint32>(header.constData() + offset); };
    const auto u64 = [&](int offset) { return qFromLittleEndian<quint64>(header.constData() + offset); };
    if (header.size() != 56 || header.first(4) != "PCHK"
        || qFromLittleEndian<quint16>(header.constData() + 4) != 56
        || u32(8) != entry.streamId || u32(12) != entry.segmentId
        || u32(16) != entry.recordCount || u32(20) != entry.payloadSize
        || u64(24) != entry.firstTimestamp || u64(32) != entry.lastTimestamp
        || u64(40) != entry.firstSequence || u32(48) != entry.dataSize
        || u32(52) != entry.crc32) {
        error = QStringLiteral("数据块头与索引不一致，偏移 %1").arg(entry.chunkOffset); return false;
    }
    data = file.read(entry.dataSize);
    if (data.size() != entry.dataSize || LogCrc32(data) != entry.crc32) {
        error = QStringLiteral("数据块校验失败，偏移 %1").arg(entry.chunkOffset); return false;
    }
    return true;
}
}
