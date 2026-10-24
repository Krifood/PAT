#include "core/MixedChunkIO.h"
#include "core/LogCrc32.h"
#include "core/IndexedMixedReader.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <array>
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
    const QString& type = signal.valueType;
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
    QPointF firstPoint, lastPoint;
    quint64 firstOrder = 0, lastOrder = 0;
    QPointF minPoint;
    QPointF maxPoint;
    quint64 minOrder = 0;
    quint64 maxOrder = 0;
    bool hasBucket = false;

    void Flush() {
        if (!hasBucket || !series) return;
        std::array<std::pair<quint64, QPointF>, 4> points{{
            {firstOrder, firstPoint}, {minOrder, minPoint},
            {maxOrder, maxPoint}, {lastOrder, lastPoint}}};
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        quint64 previous = std::numeric_limits<quint64>::max();
        for (const auto& point : points) {
            if (point.first != previous) series->samples.append(point.second);
            previous = point.first;
        }
        hasBucket = false;
    }

    void Add(const QPointF& point) {
        const quint64 nextBucket = seen / bucketWidth;
        if (hasBucket && nextBucket != bucket) Flush();
        if (!hasBucket) {
            bucket = nextBucket;
            firstPoint = point; firstOrder = seen;
            minPoint = maxPoint = point;
            minOrder = maxOrder = seen;
            hasBucket = true;
        } else {
            if (std::isfinite(point.y()) && (!std::isfinite(minPoint.y()) || point.y() < minPoint.y())) { minPoint = point; minOrder = seen; }
            if (std::isfinite(point.y()) && (!std::isfinite(maxPoint.y()) || point.y() > maxPoint.y())) { maxPoint = point; maxOrder = seen; }
        }
        lastPoint = point; lastOrder = seen;
        ++seen;
    }
};

}  // namespace

bool IndexedMixedReader::SupportsFixedFile(const QString& path, const FormatDefinition& format) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return format.schemaVersion <= 2 && !format.HasFraming() && format.recordSize > 0
        && !format.signalFormats.empty() && (ext == "bin" || ext == "dat");
}

bool IndexedMixedReader::OpenFixed(const QString& path, const FormatDefinition& format, QString& error) {
    if (!SupportsFixedFile(path, format)) {
        error = QStringLiteral("索引读取支持 schema 3 或无帧头固定长度 BIN/DAT"); return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    const quint64 count = quint64(file.size() / format.recordSize);
    if (!count || format.recordSize > 4 * 1024 * 1024) {
        error = QStringLiteral("固定记录为空或单条记录超过 4 MiB"); return false;
    }
    FormatDefinition indexed = format;
    indexed.streamFormats.clear();
    quint32 id = 0;
    for (int i = 0; i < int(format.signalFormats.size());) {
        const double scale = format.signalFormats[size_t(i)].timeScale;
        if (!std::isfinite(scale) || scale <= 0) { error = QStringLiteral("采样比例非法"); return false; }
        StreamFormat stream;
        stream.streamId = ++id; stream.name = QStringLiteral("fixed");
        stream.payloadLength = format.recordSize; stream.endianness = format.endianness;
        stream.firstSignalIndex = i;
        while (i < int(format.signalFormats.size()) && format.signalFormats[size_t(i)].timeScale == scale) ++i;
        stream.signalCount = i - stream.firstSignalIndex;
        indexed.streamFormats.push_back(stream);
        fixedScales_[id] = scale;
        maxX_ = std::max(maxX_, double(count - 1) * scale);
    }
    const quint64 perChunk = std::max<quint64>(1, (1024 * 1024) / format.recordSize);
    const quint64 chunks = (count + perChunk - 1) / perChunk;
    if (chunks > (32 * 1024 * 1024) / (sizeof(MixedChunkIndex) * indexed.streamFormats.size())) {
        error = QStringLiteral("固定记录索引超过 32 MiB，请分段分析"); return false;
    }
    for (const auto& stream : indexed.streamFormats) {
        for (quint64 first = 0; first < count; first += perChunk) {
            MixedChunkIndex entry;
            entry.chunkOffset = first * quint64(format.recordSize);
            entry.streamId = stream.streamId;
            entry.firstTimestamp = entry.firstSequence = first;
            entry.recordCount = quint32(std::min(perChunk, count - first));
            entry.lastTimestamp = first + entry.recordCount - 1;
            entry.payloadSize = quint32(format.recordSize);
            entry.dataSize = entry.recordCount * entry.payloadSize;
            entry.flags = 0x80000000u;
            index_.append(entry);
        }
    }
    segmentMin_[0] = 0; segmentOffset_[0] = 0;
    path_ = path; format_ = std::move(indexed); fixedRecords_ = count;
    return true;
}

bool IndexedMixedReader::Open(const QString& path, const FormatDefinition& format,
                              QString& errorMessage) {
    Close();
    if (!format.IsIndexedMixed()) {
        return OpenFixed(path, format, errorMessage);
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
    if (indexSize < kIndexHeaderSize || indexSize > 32 * 1024 * 1024) {
        errorMessage = QStringLiteral("索引长度超出 32 MiB 安全读取上限");
        return false;
    }
    QByteArray indexBytes;
    if (!ReadAt(file, indexOffset, indexSize, indexBytes, errorMessage)) return false;
    if (LogCrc32(indexBytes) != expectedIndexCrc
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
            || entry.chunkOffset < kFileHeaderSize || entry.firstTimestamp > entry.lastTimestamp
            || entry.flags != 0 || entry.payloadSize > 65536 || entry.dataSize > 4 * 1024 * 1024 + 65551
            || end < entry.chunkOffset || end > indexOffset
            || quint64(entry.dataSize)
                   != quint64(entry.recordCount) * (15u + quint64(entry.payloadSize))) {
            errorMessage = QStringLiteral("统一 BIN 第 %1 个索引项非法").arg(i);
            return false;
        }
        parsed.append(entry);
    }
    QHash<quint32, quint64> segmentMax;
    for (const auto& entry : parsed) {
        if (!segmentMin_.contains(entry.segmentId)) {
            segmentMin_[entry.segmentId] = entry.firstTimestamp;
            segmentMax[entry.segmentId] = entry.lastTimestamp;
        } else {
            segmentMin_[entry.segmentId] = std::min(segmentMin_[entry.segmentId], entry.firstTimestamp);
            segmentMax[entry.segmentId] = std::max(segmentMax[entry.segmentId], entry.lastTimestamp);
        }
    }
    auto segments = segmentMin_.keys();
    std::sort(segments.begin(), segments.end());
    double offset = 0;
    for (quint32 segment : segments) {
        segmentOffset_[segment] = offset;
        maxX_ = offset + double(segmentMax[segment] - segmentMin_[segment]) * format.timestampScale;
        offset = maxX_ + std::max(1.0, format.timestampScale);
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
    segmentMin_.clear();
    segmentOffset_.clear();
    maxX_ = 0;
    fixedRecords_ = 0;
    fixedScales_.clear();
}

quint64 IndexedMixedReader::RecordCount() const {
    if (fixedRecords_) return fixedRecords_;
    quint64 count = 0;
    for (const auto& entry : index_) count += entry.recordCount;
    return count;
}

bool IndexedMixedReader::ReadSignals(const QVector<int>& signalIndices,
                                     qsizetype cacheBudgetBytes,
                                     MixedReadResult& outResult,
                                     QString& errorMessage,
                                     const MixedReadWindow& window,
                                     const std::function<bool()>& isCancelled) const {
    if (!std::isfinite(window.minX) || std::isnan(window.maxX)
        || window.minX < 0 || window.maxX < window.minX) {
        errorMessage = QStringLiteral("读取时间范围非法");
        return false;
    }
    const auto cancelled = [&]() {
        if (!isCancelled || !isCancelled()) return false;
        errorMessage = QStringLiteral("读取已取消");
        return true;
    };
    if (cancelled()) return false;
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
    result.statistics.resize(qsizetype(format_.signalFormats.size()));
    result.series.resize(static_cast<qsizetype>(format_.signalFormats.size()));
    for (int i = 0; i < result.series.size(); ++i) {
        result.series[i].name = format_.signalFormats[static_cast<size_t>(i)].QualifiedDisplayName();
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
    // Reserve half the budget for vector capacity and replacement; no per-signal
    // minimum that silently multiplies the budget when hundreds of fields are selected.
    const quint64 maxPointsPerSignal = quint64(result.cacheBudgetBytes)
        / (quint64(selected.size()) * 32u);
    if (maxPointsPerSignal < 8) {
        errorMessage = QStringLiteral("所选字段过多，请增加缓存预算或减少选择");
        return false;
    }
    const auto scaleFor = [&](const MixedChunkIndex& entry) {
        return fixedRecords_ ? fixedScales_.value(entry.streamId) : format_.timestampScale;
    };
    const auto& segmentMin = segmentMin_;
    const auto& segmentOffset = segmentOffset_;
    const auto intersects = [&](const MixedChunkIndex& entry) {
        const double base = segmentOffset.value(entry.segmentId);
        const quint64 origin = segmentMin.value(entry.segmentId);
        return base + double(entry.lastTimestamp - origin) * scaleFor(entry) >= window.minX
            && base + double(entry.firstTimestamp - origin) * scaleFor(entry) <= window.maxX;
    };
    QHash<quint32, quint64> totalByStream;
    QHash<quint32, quint64> chunksByStream;
    for (const auto& entry : index_) {
        if (selectedByStream.contains(entry.streamId) && intersects(entry)) {
            totalByStream[entry.streamId] += entry.recordCount;
            ++chunksByStream[entry.streamId];
        }
    }
    // Chunk counts are an upper bound. For a narrow window whose intersecting
    // chunks exceed the point budget, count timestamps first so local detail is
    // not needlessly reduced because of records outside the viewport.
    if (std::isfinite(window.maxX)) {
        QSet<quint32> countExactly;
        for (auto it = totalByStream.cbegin(); it != totalByStream.cend(); ++it)
            if (it.value() + 5 * chunksByStream.value(it.key()) > maxPointsPerSignal)
                countExactly.insert(it.key());
        if (!countExactly.isEmpty()) {
            QFile countFile(path_);
            if (!countFile.open(QIODevice::ReadOnly)) { errorMessage = countFile.errorString(); return false; }
            for (quint32 id : countExactly) totalByStream[id] = 0;
            for (const auto& entry : index_) {
                if (!countExactly.contains(entry.streamId) || !intersects(entry)) continue;
                if (cancelled()) return false;
                QByteArray bytes;
                if (!ReadMixedChunk(countFile, entry, bytes, errorMessage)) return false;
                ++result.readChunks; result.readBytes += entry.dataSize + (fixedRecords_ ? 0 : 56);
                const qsizetype stride = entry.payloadSize + (fixedRecords_ ? 0 : 15);
                for (quint32 row = 0; row < entry.recordCount; ++row) {
                    if (row % 1024 == 0 && cancelled()) return false;
                    const quint64 timestamp = fixedRecords_ ? entry.firstSequence + row
                        : ReadLittle<quint64>(bytes.constData() + qsizetype(row) * stride);
                    if (timestamp < entry.firstTimestamp || timestamp > entry.lastTimestamp) {
                        errorMessage = QStringLiteral("时间戳与索引不符"); return false;
                    }
                    const double x = segmentOffset.value(entry.segmentId)
                        + double(timestamp - segmentMin.value(entry.segmentId)) * scaleFor(entry);
                    if (x >= window.minX && x <= window.maxX) ++totalByStream[entry.streamId];
                }
            }
        }
    }
    QHash<int, Decimator> decimators;
    for (auto it = selectedByStream.cbegin(); it != selectedByStream.cend(); ++it) {
        const quint64 total = totalByStream.value(it.key());
        const quint64 overhead = 5 * chunksByStream.value(it.key());
        if (overhead + 8 >= maxPointsPerSignal) {
            errorMessage = QStringLiteral("来源段/块数量超过当前显示预算，请缩小时间范围");
            return false;
        }
        const quint64 available = maxPointsPerSignal - overhead;
        const quint64 bucketWidth = total <= available
            ? 1 : (total + available / 4 - 1) / (available / 4);
        result.reduced = result.reduced || bucketWidth > 1;
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
    QHash<quint32, quint64> previousTimestampByStream;
    for (const auto& entry : index_) {
        if (cancelled()) return false;
        if (!selectedByStream.contains(entry.streamId) || !intersects(entry)) continue;
        const auto* stream = streamDefinitions.value(entry.streamId, nullptr);
        if (!stream || entry.payloadSize != quint32(stream->payloadLength)) {
            errorMessage = QStringLiteral("流 %1 的负载长度与 JSON 不一致").arg(entry.streamId);
            return false;
        }
        if (previousSegmentByStream.contains(entry.streamId)
            && previousSegmentByStream.value(entry.streamId) != entry.segmentId) {
            previousTimestampByStream.remove(entry.streamId);
            for (int flat : selectedByStream.value(entry.streamId)) {
                decimators[flat].Flush();
                decimators[flat].series->samples.append(
                    QPointF(segmentOffset.value(entry.segmentId), qQNaN()));
            }
        }
        previousSegmentByStream.insert(entry.streamId, entry.segmentId);
        QByteArray chunkData;
        if (!ReadMixedChunk(file, entry, chunkData, errorMessage)) return false;
        ++result.readChunks;
        result.readBytes += (fixedRecords_ ? 0 : kChunkHeaderSize) + entry.dataSize;
        const int prefixSize = fixedRecords_ ? 0 : 15;
        const int recordSize = prefixSize + int(entry.payloadSize);
        const bool little = stream->endianness == QStringLiteral("little");
        const QVector<int> selectedSignals = selectedByStream.value(entry.streamId);
        for (quint32 recordIndex = 0; recordIndex < entry.recordCount; ++recordIndex) {
            if ((recordIndex % 1024) == 0 && cancelled()) return false;
            const char* record = chunkData.constData() + qsizetype(recordIndex) * recordSize;
            const quint64 timestamp = fixedRecords_ ? entry.firstSequence + recordIndex : ReadLittle<quint64>(record);
            if (timestamp < entry.firstTimestamp || timestamp > entry.lastTimestamp
                || (previousTimestampByStream.contains(entry.streamId)
                    && timestamp < previousTimestampByStream.value(entry.streamId))) {
                errorMessage = QStringLiteral("流时间戳回退或与索引不符，请按来源段检查日志");
                return false;
            }
            previousTimestampByStream[entry.streamId] = timestamp;
            const char* payload = record + prefixSize;
            const double x = segmentOffset.value(entry.segmentId)
                + static_cast<double>(timestamp - segmentMin.value(entry.segmentId))
                      * scaleFor(entry);
            if (x < window.minX || x > window.maxX) continue;
            for (int flat : selectedSignals) {
                double y = 0.0;
                if (!DecodeField(format_.signalFormats[static_cast<size_t>(flat)], payload,
                                 little, y)) {
                    errorMessage = QStringLiteral("信号 %1 解码失败")
                                       .arg(format_.signalFormats[static_cast<size_t>(flat)].name);
                    return false;
                }
                auto& stats = result.statistics[flat];
                if (std::isfinite(y)) {
                    ++stats.count;
                    stats.min = std::min(stats.min, y);
                    stats.max = std::max(stats.max, y);
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
