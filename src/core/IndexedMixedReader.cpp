#include "core/IndexedMixedReader.h"

#include <QFile>
#include <QHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace pat {
namespace {

constexpr int kFileHeaderSize = 64;
constexpr int kChunkHeaderSize = 56;
constexpr int kIndexHeaderSize = 16;
constexpr int kIndexEntrySize = 64;
constexpr int kFooterSize = 16;

template <typename T>
T ReadLittle(const char* data) {
    return qFromLittleEndian<T>(reinterpret_cast<const uchar*>(data));
}

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

bool ReadAt(QFile& file, quint64 offset, qint64 size, QByteArray& output,
            QString& errorMessage) {
    if (offset > quint64(std::numeric_limits<qint64>::max())
        || !file.seek(static_cast<qint64>(offset))) {
        errorMessage = QStringLiteral("无法定位统一日志偏移 0x%1").arg(offset, 0, 16);
        return false;
    }
    output = file.read(size);
    if (output.size() != size) {
        errorMessage = QStringLiteral("统一日志在偏移 0x%1 处截断").arg(offset, 0, 16);
        return false;
    }
    return true;
}

template <typename T>
T ReadNumber(const char* data, bool little) {
    using U = std::make_unsigned_t<T>;
    const U raw = little
        ? qFromLittleEndian<U>(reinterpret_cast<const uchar*>(data))
        : qFromBigEndian<U>(reinterpret_cast<const uchar*>(data));
    return std::bit_cast<T>(raw);
}

bool DecodeField(const SignalFormat& signal, const char* payload,
                 bool little, double& value) {
    const QString type = signal.valueType.toLower();
    const char* data = payload + signal.byteOffset;
    quint64 unsignedValue = 0;
    if (type == QStringLiteral("uint8")) unsignedValue = quint8(*data);
    else if (type == QStringLiteral("uint16")) unsignedValue = little
        ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data))
        : qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(data));
    else if (type == QStringLiteral("uint32")) unsignedValue = little
        ? qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data))
        : qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data));
    else if (type == QStringLiteral("uint64")) unsignedValue = little
        ? qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(data))
        : qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(data));

    if (signal.bitWidth > 0) {
        const quint64 mask = signal.bitWidth == 64
            ? std::numeric_limits<quint64>::max()
            : ((quint64{1} << signal.bitWidth) - 1);
        value = static_cast<double>((unsignedValue >> signal.bitOffset) & mask);
    } else if (type.startsWith(QStringLiteral("uint"))) {
        value = static_cast<double>(unsignedValue);
    } else if (type == QStringLiteral("int8")) {
        value = static_cast<double>(std::bit_cast<qint8>(quint8(*data)));
    } else if (type == QStringLiteral("int16")) {
        value = static_cast<double>(ReadNumber<qint16>(data, little));
    } else if (type == QStringLiteral("int32")) {
        value = static_cast<double>(ReadNumber<qint32>(data, little));
    } else if (type == QStringLiteral("int64")) {
        value = static_cast<double>(ReadNumber<qint64>(data, little));
    } else if (type == QStringLiteral("float32")) {
        const quint32 raw = little
            ? qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data))
            : qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data));
        value = std::bit_cast<float>(raw);
    } else if (type == QStringLiteral("float64")) {
        const quint64 raw = little
            ? qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(data))
            : qFromBigEndian<quint64>(reinterpret_cast<const uchar*>(data));
        value = std::bit_cast<double>(raw);
    } else {
        return false;
    }
    value = value * signal.scale + signal.bias;
    return true;
}

struct Decimator {
    Series* series = nullptr;
    quint64 bucketWidth = 1;
    quint64 seen = 0;
    quint64 bucket = std::numeric_limits<quint64>::max();
    QPointF minPoint;
    QPointF maxPoint;
    quint64 minOrder = 0;
    quint64 maxOrder = 0;
    bool hasBucket = false;

    void Flush() {
        if (!hasBucket || !series) return;
        if (minOrder <= maxOrder) {
            series->samples.append(minPoint);
            if (maxOrder != minOrder) series->samples.append(maxPoint);
        } else {
            series->samples.append(maxPoint);
            series->samples.append(minPoint);
        }
        hasBucket = false;
    }

    void Add(const QPointF& point) {
        const quint64 nextBucket = seen / bucketWidth;
        if (hasBucket && nextBucket != bucket) Flush();
        if (!hasBucket) {
            bucket = nextBucket;
            minPoint = maxPoint = point;
            minOrder = maxOrder = seen;
            hasBucket = true;
        } else {
            if (point.y() < minPoint.y()) { minPoint = point; minOrder = seen; }
            if (point.y() > maxPoint.y()) { maxPoint = point; maxOrder = seen; }
        }
        ++seen;
    }
};

}  // namespace

bool IndexedMixedReader::Open(const QString& path, const FormatDefinition& format,
                              QString& errorMessage) {
    Close();
    if (!format.IsIndexedMixed()) {
        errorMessage = QStringLiteral("格式不是 indexed_stream_chunks schema 3");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < kFileHeaderSize + kFooterSize) {
        errorMessage = QStringLiteral("无法打开或统一 BIN 过短：%1").arg(path);
        return false;
    }
    QByteArray header;
    if (!ReadAt(file, 0, kFileHeaderSize, header, errorMessage)) return false;
    if (header.left(8) != QByteArrayLiteral("PATMBIN1")
        || ReadLittle<quint16>(header.constData() + 8) != 1
        || ReadLittle<quint16>(header.constData() + 10) != kFileHeaderSize) {
        errorMessage = QStringLiteral("统一 BIN 文件头或版本不支持");
        return false;
    }
    const QByteArray layoutHash = header.mid(16, 32);
    if (layoutHash.toHex() != format.layoutSha256.toLatin1()) {
        errorMessage = QStringLiteral("BIN 与 JSON 的 layout_sha256 不一致");
        return false;
    }
    if (ReadLittle<quint32>(header.constData() + 60) != format.streamFormats.size()) {
        errorMessage = QStringLiteral("BIN 与 JSON 的流数量不一致");
        return false;
    }
    QByteArray footer;
    if (!ReadAt(file, quint64(file.size() - kFooterSize), kFooterSize, footer, errorMessage)) return false;
    if (footer.left(4) != QByteArrayLiteral("PEND")) {
        errorMessage = QStringLiteral("统一 BIN 缺少文件尾索引标记");
        return false;
    }
    const quint64 indexOffset = ReadLittle<quint64>(footer.constData() + 4);
    const quint32 expectedIndexCrc = ReadLittle<quint32>(footer.constData() + 12);
    if (indexOffset != ReadLittle<quint64>(header.constData() + 48)
        || indexOffset >= quint64(file.size() - kFooterSize)) {
        errorMessage = QStringLiteral("统一 BIN 索引偏移非法");
        return false;
    }
    const qint64 indexSize = file.size() - kFooterSize - static_cast<qint64>(indexOffset);
    QByteArray indexBytes;
    if (!ReadAt(file, indexOffset, indexSize, indexBytes, errorMessage)) return false;
    if (Crc32(indexBytes) != expectedIndexCrc
        || indexBytes.size() < kIndexHeaderSize
        || indexBytes.left(4) != QByteArrayLiteral("PIDX")
        || ReadLittle<quint16>(indexBytes.constData() + 4) != 1
        || ReadLittle<quint16>(indexBytes.constData() + 6) != kIndexEntrySize) {
        errorMessage = QStringLiteral("统一 BIN 索引校验或版本错误");
        return false;
    }
    const quint32 entryCount = ReadLittle<quint32>(indexBytes.constData() + 8);
    if (indexBytes.size() != kIndexHeaderSize + qint64(entryCount) * kIndexEntrySize
        || entryCount != ReadLittle<quint32>(header.constData() + 56)) {
        errorMessage = QStringLiteral("统一 BIN 索引长度或块数不一致");
        return false;
    }
    QSet<quint32> validStreams;
    for (const auto& stream : format.streamFormats) validStreams.insert(stream.streamId);
    QVector<MixedChunkIndex> parsed;
    parsed.reserve(entryCount);
    for (quint32 i = 0; i < entryCount; ++i) {
        const char* data = indexBytes.constData() + kIndexHeaderSize + i * kIndexEntrySize;
        MixedChunkIndex entry;
        entry.chunkOffset = ReadLittle<quint64>(data);
        entry.streamId = ReadLittle<quint32>(data + 8);
        entry.segmentId = ReadLittle<quint32>(data + 12);
        entry.firstTimestamp = ReadLittle<quint64>(data + 16);
        entry.lastTimestamp = ReadLittle<quint64>(data + 24);
        entry.firstSequence = ReadLittle<quint64>(data + 32);
        entry.recordCount = ReadLittle<quint32>(data + 40);
        entry.payloadSize = ReadLittle<quint32>(data + 44);
        entry.dataSize = ReadLittle<quint32>(data + 48);
        entry.flags = ReadLittle<quint32>(data + 52);
        entry.crc32 = ReadLittle<quint32>(data + 56);
        const quint64 end = entry.chunkOffset + kChunkHeaderSize + entry.dataSize;
        if (!validStreams.contains(entry.streamId) || entry.recordCount == 0
            || end < entry.chunkOffset || end > indexOffset
            || quint64(entry.dataSize)
                   != quint64(entry.recordCount) * (15u + quint64(entry.payloadSize))) {
            errorMessage = QStringLiteral("统一 BIN 第 %1 个索引项非法").arg(i);
            return false;
        }
        parsed.append(entry);
    }
    path_ = path;
    format_ = format;
    index_ = std::move(parsed);
    layoutHash_ = layoutHash;
    return true;
}

void IndexedMixedReader::Close() {
    path_.clear();
    format_ = FormatDefinition{};
    index_.clear();
    layoutHash_.clear();
}

quint64 IndexedMixedReader::RecordCount() const {
    quint64 count = 0;
    for (const auto& entry : index_) count += entry.recordCount;
    return count;
}

bool IndexedMixedReader::ReadSignals(const QVector<int>& signalIndices,
                                     qsizetype cacheBudgetBytes,
                                     MixedReadResult& outResult,
                                     QString& errorMessage) const {
    if (!IsOpen()) {
        errorMessage = QStringLiteral("统一日志尚未打开");
        return false;
    }
    QSet<int> selected;
    for (int index : signalIndices) {
        if (index < 0 || index >= static_cast<int>(format_.signalFormats.size())) {
            errorMessage = QStringLiteral("信号索引越界：%1").arg(index);
            return false;
        }
        selected.insert(index);
    }
    MixedReadResult result;
    result.cacheBudgetBytes = std::max<qsizetype>(4 * 1024 * 1024, cacheBudgetBytes);
    result.series.resize(static_cast<qsizetype>(format_.signalFormats.size()));
    for (int i = 0; i < result.series.size(); ++i) {
        result.series[i].name = format_.signalFormats[static_cast<size_t>(i)].name;
        result.series[i].unit = format_.signalFormats[static_cast<size_t>(i)].unit;
    }
    if (selected.isEmpty()) {
        outResult = std::move(result);
        return true;
    }

    QHash<quint32, QVector<int>> selectedByStream;
    QHash<quint32, const StreamFormat*> streamDefinitions;
    for (const auto& stream : format_.streamFormats) {
        streamDefinitions.insert(stream.streamId, &stream);
        for (int local = 0; local < stream.signalCount; ++local) {
            const int flat = stream.firstSignalIndex + local;
            if (selected.contains(flat)) selectedByStream[stream.streamId].append(flat);
        }
    }
    const quint64 maxPointsPerSignal = std::max<quint64>(4000,
        quint64(result.cacheBudgetBytes) / (quint64(selected.size()) * 32u));
    QHash<quint32, quint64> totalByStream;
    QHash<quint32, quint64> segmentMin;
    QHash<quint32, quint64> segmentMax;
    for (const auto& entry : index_) {
        if (selectedByStream.contains(entry.streamId)) totalByStream[entry.streamId] += entry.recordCount;
        if (!segmentMin.contains(entry.segmentId)) {
            segmentMin.insert(entry.segmentId, entry.firstTimestamp);
            segmentMax.insert(entry.segmentId, entry.lastTimestamp);
        } else {
            segmentMin[entry.segmentId] = std::min(segmentMin.value(entry.segmentId), entry.firstTimestamp);
            segmentMax[entry.segmentId] = std::max(segmentMax.value(entry.segmentId), entry.lastTimestamp);
        }
    }
    QList<quint32> segmentIds = segmentMin.keys();
    std::sort(segmentIds.begin(), segmentIds.end());
    QHash<quint32, double> segmentOffset;
    double nextOffset = 0.0;
    for (quint32 segmentId : segmentIds) {
        segmentOffset.insert(segmentId, nextOffset);
        const double span = static_cast<double>(segmentMax.value(segmentId)
                                                - segmentMin.value(segmentId))
                            * format_.timestampScale;
        nextOffset += std::max(0.0, span) + 1.0;
    }
    QHash<int, Decimator> decimators;
    for (auto it = selectedByStream.cbegin(); it != selectedByStream.cend(); ++it) {
        const quint64 total = totalByStream.value(it.key());
        const quint64 bucketWidth = total <= maxPointsPerSignal
            ? 1 : std::max<quint64>(1, (total + maxPointsPerSignal / 2 - 1) / (maxPointsPerSignal / 2));
        for (int flat : it.value()) {
            Decimator state;
            state.series = &result.series[flat];
            state.bucketWidth = bucketWidth;
            decimators.insert(flat, state);
        }
    }

    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法重新打开统一 BIN：%1").arg(file.errorString());
        return false;
    }
    QHash<quint32, quint32> previousSegmentByStream;
    for (const auto& entry : index_) {
        if (!selectedByStream.contains(entry.streamId)) continue;
        const auto* stream = streamDefinitions.value(entry.streamId, nullptr);
        if (!stream || entry.payloadSize != quint32(stream->payloadLength)) {
            errorMessage = QStringLiteral("流 %1 的负载长度与 JSON 不一致").arg(entry.streamId);
            return false;
        }
        if (previousSegmentByStream.contains(entry.streamId)
            && previousSegmentByStream.value(entry.streamId) != entry.segmentId) {
            for (int flat : selectedByStream.value(entry.streamId)) {
                decimators[flat].Flush();
                decimators[flat].series->samples.append(
                    QPointF(segmentOffset.value(entry.segmentId), qQNaN()));
            }
        }
        previousSegmentByStream.insert(entry.streamId, entry.segmentId);
        QByteArray chunkHeader;
        QByteArray chunkData;
        if (!ReadAt(file, entry.chunkOffset, kChunkHeaderSize, chunkHeader, errorMessage)
            || !ReadAt(file, entry.chunkOffset + kChunkHeaderSize, entry.dataSize,
                       chunkData, errorMessage)) return false;
        if (chunkHeader.left(4) != QByteArrayLiteral("PCHK")
            || ReadLittle<quint16>(chunkHeader.constData() + 4) != kChunkHeaderSize
            || ReadLittle<quint32>(chunkHeader.constData() + 8) != entry.streamId
            || ReadLittle<quint32>(chunkHeader.constData() + 12) != entry.segmentId
            || ReadLittle<quint32>(chunkHeader.constData() + 16) != entry.recordCount
            || ReadLittle<quint32>(chunkHeader.constData() + 20) != entry.payloadSize
            || ReadLittle<quint64>(chunkHeader.constData() + 24) != entry.firstTimestamp
            || ReadLittle<quint64>(chunkHeader.constData() + 32) != entry.lastTimestamp
            || ReadLittle<quint64>(chunkHeader.constData() + 40) != entry.firstSequence
            || ReadLittle<quint32>(chunkHeader.constData() + 48) != entry.dataSize
            || ReadLittle<quint32>(chunkHeader.constData() + 52) != entry.crc32
            || Crc32(chunkData) != entry.crc32) {
            errorMessage = QStringLiteral("流 %1 在偏移 0x%2 的数据块校验失败")
                               .arg(entry.streamId).arg(entry.chunkOffset, 0, 16);
            return false;
        }
        const int recordSize = 15 + int(entry.payloadSize);
        const bool little = stream->endianness == QStringLiteral("little");
        const QVector<int> selectedSignals = selectedByStream.value(entry.streamId);
        for (quint32 recordIndex = 0; recordIndex < entry.recordCount; ++recordIndex) {
            const char* record = chunkData.constData() + qsizetype(recordIndex) * recordSize;
            const quint64 timestamp = ReadLittle<quint64>(record);
            const char* payload = record + 15;
            const double x = segmentOffset.value(entry.segmentId)
                + static_cast<double>(timestamp - segmentMin.value(entry.segmentId))
                      * format_.timestampScale;
            for (int flat : selectedSignals) {
                double y = 0.0;
                if (!DecodeField(format_.signalFormats[static_cast<size_t>(flat)], payload,
                                 little, y)) {
                    errorMessage = QStringLiteral("信号 %1 解码失败")
                                       .arg(format_.signalFormats[static_cast<size_t>(flat)].name);
                    return false;
                }
                decimators[flat].Add(QPointF(x, y));
            }
            ++result.decodedRecords;
        }
    }
    for (auto it = decimators.begin(); it != decimators.end(); ++it) it->Flush();
    for (const auto& series : result.series) result.retainedPoints += series.samples.size();
    outResult = std::move(result);
    return true;
}

}  // namespace pat
