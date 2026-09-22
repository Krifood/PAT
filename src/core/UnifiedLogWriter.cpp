#include "core/UnifiedLogWriter.h"

#include "core/VmcLogReader.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QUuid>
#include <QtEndian>

#include <algorithm>
#include <limits>

namespace pat {
namespace {

constexpr int kFileHeaderSize = 64;
constexpr int kChunkHeaderSize = 56;
constexpr int kIndexEntrySize = 64;

quint32 Crc32(QByteArrayView data) {
    quint32 crc = 0xFFFFFFFFu;
    for (char byte : data) {
        crc ^= quint8(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

template <typename T>
void AppendLittle(QByteArray& bytes, T value) {
    const qsizetype oldSize = bytes.size();
    bytes.resize(oldSize + qsizetype(sizeof(T)));
    qToLittleEndian<T>(value, reinterpret_cast<uchar*>(bytes.data() + oldSize));
}

template <typename T>
void PutLittle(QByteArray& bytes, qsizetype offset, T value) {
    qToLittleEndian<T>(value, reinterpret_cast<uchar*>(bytes.data() + offset));
}

bool WriteAll(QIODevice& output, QByteArrayView bytes, QString& errorMessage) {
    const qint64 written = output.write(bytes.data(), bytes.size());
    if (written != bytes.size()) {
        errorMessage = QStringLiteral("写入统一日志失败：%1").arg(output.errorString());
        return false;
    }
    return true;
}

struct UnifiedStream {
    quint32 streamId = 0;
    VmcStreamDescriptor descriptor;
};

struct PendingChunk {
    quint32 streamId = 0;
    quint32 segmentId = 0;
    quint32 payloadSize = 0;
    quint64 firstTimestamp = 0;
    quint64 lastTimestamp = 0;
    quint64 firstSequence = 0;
    quint32 recordCount = 0;
    quint64 lastTouched = 0;
    QByteArray data;
};

struct ChunkIndex {
    quint64 chunkOffset = 0;
    quint32 streamId = 0;
    quint32 segmentId = 0;
    quint64 firstTimestamp = 0;
    quint64 lastTimestamp = 0;
    quint64 firstSequence = 0;
    quint32 recordCount = 0;
    quint32 payloadSize = 0;
    quint32 dataSize = 0;
    quint32 flags = 0;
    quint32 crc32 = 0;
};

QByteArray LayoutBytes(const QVector<UnifiedStream>& streams) {
    QVector<const UnifiedStream*> ordered;
    ordered.reserve(streams.size());
    for (const auto& stream : streams) ordered.append(&stream);
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return left->streamId < right->streamId;
    });
    QByteArray bytes("PAT-MIXED-LAYOUT-1\n");
    for (const auto* stream : ordered) {
        bytes.append(QByteArray::number(stream->streamId));
        bytes.append(':');
        bytes.append(stream->descriptor.structureSha256.toHex());
        bytes.append('\n');
    }
    return bytes;
}

QJsonObject SignalJson(const VmcFieldDefinition& field,
                       const VmcStreamDescriptor& descriptor) {
    QJsonObject signal;
    signal.insert(QStringLiteral("name"), field.name);
    signal.insert(QStringLiteral("source_name"), field.sourceName);
    signal.insert(QStringLiteral("byte_offset"), field.byteOffset);
    signal.insert(QStringLiteral("value_type"), field.valueType);
    signal.insert(QStringLiteral("source_type"), field.sourceType);
    signal.insert(QStringLiteral("scale"), 1.0);
    signal.insert(QStringLiteral("bias"), 0.0);
    signal.insert(QStringLiteral("unit"), QString());
    signal.insert(QStringLiteral("description"), QString());
    signal.insert(QStringLiteral("group"), QStringLiteral("%1 [0x%2]")
                      .arg(descriptor.topic)
                      .arg(descriptor.messageId, 4, 16, QLatin1Char('0')).toUpper());
    return signal;
}

}  // namespace

bool UnifiedLogWriter::ConvertVmcFiles(const QStringList& sourcePaths,
                                       const QString& binaryPath,
                                       const QString& jsonPath,
                                       const UnifiedLogWriteOptions& options,
                                       UnifiedLogWriteResult& outResult,
                                       QString& errorMessage) const {
    if (sourcePaths.isEmpty() || binaryPath.isEmpty() || jsonPath.isEmpty()) {
        errorMessage = QStringLiteral("源文件和输出路径不能为空");
        return false;
    }
    if (QFileInfo(binaryPath).absoluteFilePath() == QFileInfo(jsonPath).absoluteFilePath()) {
        errorMessage = QStringLiteral("BIN 和 JSON 输出路径不能相同");
        return false;
    }
    const qsizetype targetChunkBytes = std::max<qsizetype>(64 * 1024, options.targetChunkBytes);
    const qsizetype maxBufferedBytes = std::max(targetChunkBytes, options.maxBufferedBytes);
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString binaryStage = binaryPath + QStringLiteral(".pat-tmp-") + token;
    const QString jsonStage = jsonPath + QStringLiteral(".pat-tmp-") + token;
    const auto removeStages = qScopeGuard([&]() {
        QFile::remove(binaryStage);
        QFile::remove(jsonStage);
    });

    VmcLogReader reader;
    QVector<VmcSourceHeader> headers;
    QVector<UnifiedStream> streams;
    QHash<QByteArray, int> streamBySignature;
    QSet<quint32> assignedIds;
    for (const QString& sourcePath : sourcePaths) {
        VmcSourceHeader header;
        if (!reader.ReadHeader(sourcePath, header, errorMessage)) return false;
        for (const auto& descriptor : header.descriptors) {
            if (streamBySignature.contains(descriptor.structureSha256)) continue;
            quint32 streamId = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar*>(descriptor.structureSha256.constData()));
            if (streamId == 0) streamId = 1;
            while (assignedIds.contains(streamId)) ++streamId;
            assignedIds.insert(streamId);
            streamBySignature.insert(descriptor.structureSha256, streams.size());
            streams.append({streamId, descriptor});
        }
        headers.append(std::move(header));
    }
    const QByteArray layoutHash = QCryptographicHash::hash(LayoutBytes(streams),
                                                           QCryptographicHash::Sha256);

    QSaveFile output(binaryStage);
    if (!output.open(QIODevice::WriteOnly)) {
        errorMessage = QStringLiteral("无法创建统一 BIN：%1").arg(output.errorString());
        return false;
    }
    QByteArray fileHeader(kFileHeaderSize, '\0');
    fileHeader.replace(0, 8, QByteArrayLiteral("PATMBIN1"));
    PutLittle<quint16>(fileHeader, 8, 1);
    PutLittle<quint16>(fileHeader, 10, kFileHeaderSize);
    fileHeader.replace(16, 32, layoutHash);
    if (!WriteAll(output, fileHeader, errorMessage)) return false;

    QHash<quint64, PendingChunk> pending;
    QVector<ChunkIndex> index;
    qsizetype bufferedBytes = 0;
    qsizetype peakBufferedBytes = 0;
    quint64 touchCounter = 0;

    const auto flushChunk = [&](quint64 key, QString& writeError) -> bool {
        auto it = pending.find(key);
        if (it == pending.end() || it->recordCount == 0) return true;
        PendingChunk chunk = std::move(it.value());
        pending.erase(it);
        bufferedBytes -= chunk.data.size();
        if (chunk.data.size() > std::numeric_limits<quint32>::max()) {
            writeError = QStringLiteral("单个数据块超过 4 GiB");
            return false;
        }
        ChunkIndex entry;
        entry.chunkOffset = static_cast<quint64>(output.pos());
        entry.streamId = chunk.streamId;
        entry.segmentId = chunk.segmentId;
        entry.firstTimestamp = chunk.firstTimestamp;
        entry.lastTimestamp = chunk.lastTimestamp;
        entry.firstSequence = chunk.firstSequence;
        entry.recordCount = chunk.recordCount;
        entry.payloadSize = chunk.payloadSize;
        entry.dataSize = static_cast<quint32>(chunk.data.size());
        entry.crc32 = Crc32(chunk.data);
        QByteArray header(kChunkHeaderSize, '\0');
        header.replace(0, 4, QByteArrayLiteral("PCHK"));
        PutLittle<quint16>(header, 4, kChunkHeaderSize);
        PutLittle<quint32>(header, 8, entry.streamId);
        PutLittle<quint32>(header, 12, entry.segmentId);
        PutLittle<quint32>(header, 16, entry.recordCount);
        PutLittle<quint32>(header, 20, entry.payloadSize);
        PutLittle<quint64>(header, 24, entry.firstTimestamp);
        PutLittle<quint64>(header, 32, entry.lastTimestamp);
        PutLittle<quint64>(header, 40, entry.firstSequence);
        PutLittle<quint32>(header, 48, entry.dataSize);
        PutLittle<quint32>(header, 52, entry.crc32);
        if (!WriteAll(output, header, writeError) || !WriteAll(output, chunk.data, writeError)) return false;
        index.append(entry);
        return true;
    };

    QJsonArray sourceSegments;
    UnifiedLogWriteResult result;
    result.binaryPath = binaryPath;
    result.jsonPath = jsonPath;
    for (int segmentIndex = 0; segmentIndex < sourcePaths.size(); ++segmentIndex) {
        const auto& sourcePath = sourcePaths[segmentIndex];
        const auto& sourceHeader = headers[segmentIndex];
        QJsonArray metadataFrames;
        VmcReadSummary summary;
        QString writerError;
        const bool visited = reader.VisitRecords(
            sourcePath, sourceHeader,
            [&](const VmcRecord& record, const VmcStreamDescriptor* descriptor) {
                if (options.isCancelled && options.isCancelled()) {
                    writerError = QStringLiteral("转换已取消");
                    return false;
                }
                if (!descriptor) {
                    QJsonObject metadata;
                    metadata.insert(QStringLiteral("source_sequence"), static_cast<qint64>(record.sourceSequence));
                    metadata.insert(QStringLiteral("message_id"),
                                    QStringLiteral("0x%1").arg(record.messageId, 4, 16, QLatin1Char('0')).toUpper());
                    metadata.insert(QStringLiteral("timestamp_raw"), QString::number(record.timestamp));
                    metadata.insert(QStringLiteral("source_control"), int(record.sourceControl));
                    metadata.insert(QStringLiteral("payload_base64"),
                                    QString::fromLatin1(record.payload.toBase64()));
                    metadataFrames.append(metadata);
                    return true;
                }
                const int streamIndex = streamBySignature.value(descriptor->structureSha256, -1);
                if (streamIndex < 0) {
                    writerError = QStringLiteral("内部流路由丢失");
                    return false;
                }
                const quint32 streamId = streams[streamIndex].streamId;
                const quint64 key = (quint64(quint32(segmentIndex)) << 32) | streamId;
                auto& chunk = pending[key];
                if (chunk.recordCount == 0) {
                    chunk.streamId = streamId;
                    chunk.segmentId = quint32(segmentIndex);
                    chunk.payloadSize = quint32(descriptor->payloadLength);
                    chunk.firstTimestamp = record.timestamp;
                    chunk.firstSequence = record.sourceSequence;
                }
                if (record.sourceSequence - chunk.firstSequence > std::numeric_limits<quint32>::max()) {
                    if (!flushChunk(key, writerError)) return false;
                    auto& fresh = pending[key];
                    fresh.streamId = streamId;
                    fresh.segmentId = quint32(segmentIndex);
                    fresh.payloadSize = quint32(descriptor->payloadLength);
                    fresh.firstTimestamp = record.timestamp;
                    fresh.firstSequence = record.sourceSequence;
                }
                auto& target = pending[key];
                const qsizetype before = target.data.size();
                AppendLittle<quint64>(target.data, record.timestamp);
                AppendLittle<quint32>(target.data, quint32(record.sourceSequence - target.firstSequence));
                AppendLittle<quint16>(target.data, 0);
                target.data.append(char(record.sourceControl));
                target.data.append(record.payload);
                target.firstTimestamp = std::min(target.firstTimestamp, record.timestamp);
                target.lastTimestamp = std::max(target.lastTimestamp, record.timestamp);
                ++target.recordCount;
                target.lastTouched = ++touchCounter;
                bufferedBytes += target.data.size() - before;
                peakBufferedBytes = std::max(peakBufferedBytes, bufferedBytes);
                if (target.data.size() >= targetChunkBytes && !flushChunk(key, writerError)) return false;
                while (bufferedBytes > maxBufferedBytes && !pending.isEmpty()) {
                    auto oldest = pending.begin();
                    for (auto it = pending.begin(); it != pending.end(); ++it) {
                        if (it->lastTouched < oldest->lastTouched) oldest = it;
                    }
                    if (!flushChunk(oldest.key(), writerError)) return false;
                }
                return true;
            }, summary, errorMessage);
        if (!visited) {
            if (!writerError.isEmpty()) errorMessage = writerError;
            return false;
        }
        const auto keys = pending.keys();
        for (quint64 key : keys) {
            if (quint32(key >> 32) == quint32(segmentIndex)
                && !flushChunk(key, errorMessage)) return false;
        }
        result.sourceRecordCount += summary.recordCount;
        result.routedRecordCount += summary.routedRecordCount;
        result.metadataRecordCount += summary.metadataRecordCount;
        QJsonObject segment;
        segment.insert(QStringLiteral("segment_id"), segmentIndex);
        segment.insert(QStringLiteral("source_file"), QFileInfo(sourcePath).fileName());
        segment.insert(QStringLiteral("global_header_base64"),
                       QString::fromLatin1(sourceHeader.globalHeader.toBase64()));
        segment.insert(QStringLiteral("record_count"), static_cast<qint64>(summary.recordCount));
        segment.insert(QStringLiteral("routed_record_count"), static_cast<qint64>(summary.routedRecordCount));
        segment.insert(QStringLiteral("metadata_record_count"), static_cast<qint64>(summary.metadataRecordCount));
        segment.insert(QStringLiteral("trailing_fill_bytes"), static_cast<qint64>(summary.trailingFillBytes));
        segment.insert(QStringLiteral("metadata_frames"), metadataFrames);
        sourceSegments.append(segment);
    }

    const quint64 indexOffset = static_cast<quint64>(output.pos());
    QByteArray indexBytes;
    indexBytes.append(QByteArrayLiteral("PIDX"));
    AppendLittle<quint16>(indexBytes, 1);
    AppendLittle<quint16>(indexBytes, kIndexEntrySize);
    AppendLittle<quint32>(indexBytes, quint32(index.size()));
    AppendLittle<quint32>(indexBytes, 0);
    for (const auto& entry : index) {
        AppendLittle<quint64>(indexBytes, entry.chunkOffset);
        AppendLittle<quint32>(indexBytes, entry.streamId);
        AppendLittle<quint32>(indexBytes, entry.segmentId);
        AppendLittle<quint64>(indexBytes, entry.firstTimestamp);
        AppendLittle<quint64>(indexBytes, entry.lastTimestamp);
        AppendLittle<quint64>(indexBytes, entry.firstSequence);
        AppendLittle<quint32>(indexBytes, entry.recordCount);
        AppendLittle<quint32>(indexBytes, entry.payloadSize);
        AppendLittle<quint32>(indexBytes, entry.dataSize);
        AppendLittle<quint32>(indexBytes, entry.flags);
        AppendLittle<quint32>(indexBytes, entry.crc32);
        AppendLittle<quint32>(indexBytes, 0);
    }
    const quint32 indexCrc = Crc32(indexBytes);
    if (!WriteAll(output, indexBytes, errorMessage)) return false;
    QByteArray footer;
    footer.append(QByteArrayLiteral("PEND"));
    AppendLittle<quint64>(footer, indexOffset);
    AppendLittle<quint32>(footer, indexCrc);
    if (!WriteAll(output, footer, errorMessage)) return false;
    const quint64 outputSize = static_cast<quint64>(output.pos());

    PutLittle<quint64>(fileHeader, 48, indexOffset);
    PutLittle<quint32>(fileHeader, 56, quint32(index.size()));
    PutLittle<quint32>(fileHeader, 60, quint32(streams.size()));
    if (!output.seek(0) || !WriteAll(output, fileHeader, errorMessage)) return false;
    if (!output.commit()) {
        errorMessage = QStringLiteral("提交统一 BIN 失败：%1").arg(output.errorString());
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema_version"), 3);
    root.insert(QStringLiteral("template_id"),
                QStringLiteral("vmc_unified_%1").arg(layoutHash.toHex().left(12)));
    QJsonObject storage;
    storage.insert(QStringLiteral("kind"), QStringLiteral("indexed_stream_chunks"));
    storage.insert(QStringLiteral("version"), 1);
    storage.insert(QStringLiteral("layout_sha256"), QString::fromLatin1(layoutHash.toHex()));
    storage.insert(QStringLiteral("data_file"), QFileInfo(binaryPath).fileName());
    root.insert(QStringLiteral("storage"), storage);
    QJsonObject timestamp;
    timestamp.insert(QStringLiteral("value_type"), QStringLiteral("uint64"));
    timestamp.insert(QStringLiteral("unit"), QStringLiteral("tick"));
    timestamp.insert(QStringLiteral("scale"), QJsonValue::Null);
    root.insert(QStringLiteral("timestamp"), timestamp);
    QJsonArray streamArray;
    for (const auto& stream : streams) {
        QJsonObject item;
        item.insert(QStringLiteral("stream_id"), static_cast<qint64>(stream.streamId));
        item.insert(QStringLiteral("source_message_id"),
                    QStringLiteral("0x%1").arg(stream.descriptor.messageId, 4, 16, QLatin1Char('0')).toUpper());
        item.insert(QStringLiteral("name"), stream.descriptor.topic);
        item.insert(QStringLiteral("source_control"), int(stream.descriptor.sourceControl));
        item.insert(QStringLiteral("source_format"), QString::fromLatin1(stream.descriptor.sourceFormat));
        item.insert(QStringLiteral("payload_length"), stream.descriptor.payloadLength);
        item.insert(QStringLiteral("endianness"), QStringLiteral("big"));
        item.insert(QStringLiteral("structure_sha256"),
                    QString::fromLatin1(stream.descriptor.structureSha256.toHex()));
        QJsonArray signalArray;
        for (const auto& field : stream.descriptor.fields) {
            signalArray.append(SignalJson(field, stream.descriptor));
        }
        item.insert(QStringLiteral("signals"), signalArray);
        streamArray.append(item);
    }
    root.insert(QStringLiteral("streams"), streamArray);
    root.insert(QStringLiteral("source_segments"), sourceSegments);

    QSaveFile jsonOutput(jsonStage);
    if (!jsonOutput.open(QIODevice::WriteOnly)) {
        errorMessage = QStringLiteral("无法创建统一 JSON：%1").arg(jsonOutput.errorString());
        return false;
    }
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!WriteAll(jsonOutput, json, errorMessage) || !jsonOutput.commit()) {
        if (errorMessage.isEmpty()) {
            errorMessage = QStringLiteral("提交统一 JSON 失败：%1").arg(jsonOutput.errorString());
        }
        return false;
    }

    const QString binaryBackup = binaryPath + QStringLiteral(".pat-backup-") + token;
    const QString jsonBackup = jsonPath + QStringLiteral(".pat-backup-") + token;
    const bool hadBinary = QFileInfo::exists(binaryPath);
    const bool hadJson = QFileInfo::exists(jsonPath);
    if ((hadBinary && !QFile::rename(binaryPath, binaryBackup))
        || (hadJson && !QFile::rename(jsonPath, jsonBackup))) {
        if (QFileInfo::exists(binaryBackup)) QFile::rename(binaryBackup, binaryPath);
        if (QFileInfo::exists(jsonBackup)) QFile::rename(jsonBackup, jsonPath);
        errorMessage = QStringLiteral("无法为现有输出创建安全备份");
        return false;
    }
    if (!QFile::rename(binaryStage, binaryPath)) {
        if (hadBinary) QFile::rename(binaryBackup, binaryPath);
        if (hadJson) QFile::rename(jsonBackup, jsonPath);
        errorMessage = QStringLiteral("无法发布统一 BIN：%1").arg(binaryPath);
        return false;
    }
    if (!QFile::rename(jsonStage, jsonPath)) {
        QFile::remove(binaryPath);
        if (hadBinary) QFile::rename(binaryBackup, binaryPath);
        if (hadJson) QFile::rename(jsonBackup, jsonPath);
        errorMessage = QStringLiteral("无法发布统一 JSON：%1").arg(jsonPath);
        return false;
    }
    QFile::remove(binaryBackup);
    QFile::remove(jsonBackup);

    result.chunkCount = static_cast<quint64>(index.size());
    result.outputBytes = outputSize;
    result.peakBufferedBytes = peakBufferedBytes;
    result.streamCount = streams.size();
    outResult = result;
    return true;
}

}  // namespace pat
