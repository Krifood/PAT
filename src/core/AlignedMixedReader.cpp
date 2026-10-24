#include "core/IndexedMixedReader.h"
#include "core/MixedChunkIO.h"
#include "core/RecordDecoder.h"
#include "core/SeriesProcessing.h"
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace pat {
namespace {
struct Row {
    quint64 order = 0;
    double x = 0;
    std::array<double, 4> values{};
};
// All coordinate columns and the relation retain the same raw records, including
// extrema of the relation itself. Never subtract two independently reduced lines.
struct JointReducer {
    MixedReadResult& result;
    const QVector<int>& indices;
    quint64 width;
    int operation;
    quint64 count = 0;
    bool active = false;
    Row first, last;
    std::array<Row, 4> minima, maxima;
    JointReducer(MixedReadResult& output, const QVector<int>& columns, quint64 size, int op)
        : result(output), indices(columns), width(size), operation(op) {}
    int Columns() const { return indices.size() + (operation >= 0 ? 1 : 0); }
    void Flush() {
        if (!active) return;
        QVector<Row> retained{first, last};
        for (int i = 0; i < Columns(); ++i) { retained.append(minima[i]); retained.append(maxima[i]); }
        std::sort(retained.begin(), retained.end(), [](const Row& a, const Row& b) { return a.order < b.order; });
        quint64 previous = std::numeric_limits<quint64>::max();
        for (const auto& row : retained) {
            if (row.order == previous) continue;
            for (int i = 0; i < indices.size(); ++i)
                result.series[indices[i]].samples.append({row.x, row.values[i]});
            if (operation >= 0) result.relation.samples.append({row.x, row.values[indices.size()]});
            previous = row.order;
        }
        active = false;
    }
    void Add(Row row) {
        if (active && count % width == 0) Flush();
        row.order = count++;
        if (operation >= 0) row.values[indices.size()] = operation == 0
            ? row.values[0] - row.values[1] : row.values[0] + row.values[1];
        if (!active) {
            first = row;
            for (int i = 0; i < Columns(); ++i) minima[i] = maxima[i] = row;
            active = true;
        } else {
            for (int i = 0; i < Columns(); ++i) {
                if (std::isfinite(row.values[i]) && (!std::isfinite(minima[i].values[i]) || row.values[i] < minima[i].values[i])) minima[i] = row;
                if (std::isfinite(row.values[i]) && (!std::isfinite(maxima[i].values[i]) || row.values[i] > maxima[i].values[i])) maxima[i] = row;
            }
        }
        last = row;
    }
};
}

bool IndexedMixedReader::ReadAlignedSignals(const QVector<int>& indices, qsizetype budget,
    MixedReadResult& output, QString& error, const MixedReadWindow& window,
    const std::function<bool()>& cancelled, int operation, bool interpolate, bool holdPrevious) const {
    if (!IsOpen() || indices.size() < 2 || indices.size() > 3
        || (operation >= 0 && indices.size() != 2) || ((interpolate || holdPrevious) && operation < 0)
        || (interpolate && holdPrevious)
        || operation < -1 || operation > 1
        || !std::isfinite(window.minX) || std::isnan(window.maxX) || window.minX < 0
        || window.minX > window.maxX) {
        error = QStringLiteral("配对读取参数非法"); return false;
    }
    struct Cursor {
        QVector<const MixedChunkIndex*> chunks;
        qsizetype chunk = 0;
        quint32 record = 0;
        QByteArray bytes;
        std::unique_ptr<RecordDecoder> decoder;
        QVector<double> values;
        double scale = 1.0;
        quint32 segment = 0;
        quint64 timestamp = 0;
        double x = 0, y = 0;
        bool ended = false, hasPrevious = false;
        quint32 previousSegment = 0;
        quint64 previousTimestamp = 0;
    };
    std::array<Cursor, 3> cursors;
    MixedReadResult result;
    result.cacheBudgetBytes = std::max<qsizetype>(65536, budget);
    result.series.resize(qsizetype(format_.signalFormats.size()));
    result.statistics.resize(result.series.size());
    quint64 upperRecords = std::numeric_limits<quint64>::max(), upperChunks = 0;
    if (holdPrevious) upperRecords = 0;
    for (int i = 0; i < indices.size(); ++i) {
        const int flat = indices[i];
        if (flat < 0 || flat >= result.series.size()) { error = QStringLiteral("配对字段越界"); return false; }
        const StreamFormat* stream = nullptr;
        for (const auto& candidate : format_.streamFormats)
            if (flat >= candidate.firstSignalIndex && flat < candidate.firstSignalIndex + candidate.signalCount)
                stream = &candidate;
        if (!stream) { error = QStringLiteral("配对字段没有对应流"); return false; }
        auto& cursor = cursors[i];
        cursor.scale = fixedRecords_ ? fixedScales_.value(stream->streamId) : format_.timestampScale;
        FormatDefinition single;
        single.recordSize = stream->payloadLength;
        single.endianness = stream->endianness;
        single.signalFormats.push_back(format_.signalFormats[size_t(flat)]);
        result.series[flat].name = single.signalFormats[0].QualifiedDisplayName();
        result.series[flat].unit = single.signalFormats[0].unit;
        cursor.decoder = std::make_unique<RecordDecoder>(single);
        quint64 records = 0;
        for (const auto& entry : index_) {
            if (entry.streamId != stream->streamId) continue;
            const double base = segmentOffset_.value(entry.segmentId);
            const quint64 origin = segmentMin_.value(entry.segmentId);
            const bool before = base + double(entry.lastTimestamp - origin) * cursor.scale < window.minX;
            const bool after = base + double(entry.firstTimestamp - origin) * cursor.scale > window.maxX;
            if ((interpolate && i == 1) || holdPrevious) {
                // Retain the neighboring B chunks for interpolation at window edges.
                // Only one preceding/following chunk per source segment is needed.
                if (!cursor.chunks.isEmpty() && cursor.chunks.last()->segmentId == entry.segmentId) {
                    const auto* previous = cursor.chunks.last();
                    if (before) { records -= previous->recordCount; cursor.chunks.removeLast(); }
                    else if (after && base + double(previous->firstTimestamp - origin) * cursor.scale > window.maxX) continue;
                }
            } else if (before || after) continue;
            if (entry.payloadSize != quint32(stream->payloadLength)) { error = QStringLiteral("配对流负载长度不符"); return false; }
            cursor.chunks.append(&entry);
            records += entry.recordCount;
        }
        if (holdPrevious) upperRecords += records;
        else if (!interpolate || i == 0) upperRecords = std::min(upperRecords, records);
        upperChunks += cursor.chunks.size();
    }
    const quint64 limit = quint64(result.cacheBudgetBytes) / (32 * quint64(indices.size() + (operation >= 0)));
    const quint64 overhead = upperChunks * 10;
    if (limit <= overhead + 16) { error = QStringLiteral("配对范围超过缓存预算，请缩小范围"); return false; }
    const quint64 bucketCapacity = (limit - overhead) / 10;
    const quint64 width = upperRecords <= limit - overhead ? 1 : (upperRecords + bucketCapacity - 1) / bucketCapacity;
    result.reduced = width > 1;
    JointReducer reducer{result, indices, width, operation};
    QFile file(path_);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    bool counting = false;
    const auto advance = [&](Cursor& cursor) -> bool {
        while (cursor.chunk < cursor.chunks.size()) {
            if (cancelled && cancelled()) { error = QStringLiteral("读取已取消"); return false; }
            const auto& entry = *cursor.chunks[cursor.chunk];
            if (cursor.bytes.isEmpty()) {
                if (!ReadMixedChunk(file, entry, cursor.bytes, error)) return false;
                ++result.readChunks; result.readBytes += (fixedRecords_ ? 0 : 56) + entry.dataSize;
            }
            if (cursor.record >= entry.recordCount) {
                ++cursor.chunk; cursor.record = 0; cursor.bytes.clear(); continue;
            }
            const int prefix = fixedRecords_ ? 0 : 15;
            const quint32 recordIndex = cursor.record++;
            const char* bytes = cursor.bytes.constData() + qsizetype(recordIndex) * (prefix + entry.payloadSize);
            cursor.segment = entry.segmentId;
            cursor.timestamp = fixedRecords_ ? entry.firstSequence + recordIndex : qFromLittleEndian<quint64>(bytes);
            if (cursor.timestamp < entry.firstTimestamp || cursor.timestamp > entry.lastTimestamp
                || (cursor.hasPrevious && cursor.segment == cursor.previousSegment
                    && cursor.timestamp < cursor.previousTimestamp)) {
                error = QStringLiteral("流时间戳回退或与索引不符，不能进行时间配对"); return false;
            }
            cursor.hasPrevious = true; cursor.previousSegment = cursor.segment; cursor.previousTimestamp = cursor.timestamp;
            cursor.x = segmentOffset_.value(cursor.segment)
                + double(cursor.timestamp - segmentMin_.value(cursor.segment)) * cursor.scale;
            if (!holdPrevious && !(interpolate && &cursor == &cursors[1])
                && (cursor.x < window.minX || cursor.x > window.maxX)) continue;
            if (counting) return true;
            if (!cursor.decoder->Decode(QByteArrayView(bytes + prefix, entry.payloadSize), cursor.values, error)) return false;
            cursor.y = cursor.values[0];
            return true;
        }
        cursor.ended = true; return true;
    };

    struct Key { quint32 segment; quint64 timestamp; double x; };
    const auto key = [](const Cursor& cursor) -> Key {
        return {cursor.segment, cursor.timestamp, cursor.x};
    };
    const auto compare = [&](Key a, Key b) {
        if (a.segment != b.segment) return a.segment < b.segment ? -1 : 1;
        if (fixedRecords_ && operation >= 0) {
            // Preserve the existing fixed-record relation's floating time tolerance.
            const double tolerance = 1e-9 * std::max({1.0, std::abs(a.x), std::abs(b.x)});
            if (a.x == b.x || (!holdPrevious && std::abs(a.x - b.x) <= tolerance)) return 0;
            return a.x < b.x ? -1 : 1;
        }
        // Raw uint64 timestamps must not lose bits through double conversion.
        return a.timestamp == b.timestamp ? 0 : (a.timestamp < b.timestamp ? -1 : 1);
    };
    const int passes = result.reduced && std::isfinite(window.maxX) ? 2 : 1;
    quint64 matching = 0;
    for (int pass = 0; pass < passes; ++pass) {
        counting = passes == 2 && pass == 0;
        for (int i = 0; i < indices.size(); ++i) {
            auto& cursor = cursors[i];
            cursor.chunk = 0; cursor.record = 0; cursor.bytes.clear();
            cursor.ended = false; cursor.hasPrevious = false;
            if (!advance(cursor)) return false;
        }
        quint32 previousSegment = 0; bool hadSegment = false;
        if (holdPrevious) {
            Key previousKeys[2]{};
            double previousValues[2]{qQNaN(), qQNaN()};
            bool valid[2]{false, false};
            // Merge both event timelines; retain each stream's most recent value.
            // Once a stream ends, its last value is not extrapolated past its range.
            while (!cursors[0].ended && !cursors[1].ended) {
                const Key target = compare(key(cursors[0]), key(cursors[1])) <= 0 ? key(cursors[0]) : key(cursors[1]);
                for (int i = 0; i < 2; ++i) {
                    while (!cursors[i].ended && compare(key(cursors[i]), target) == 0) {
                        previousKeys[i] = key(cursors[i]); previousValues[i] = cursors[i].y; valid[i] = true;
                        if (!advance(cursors[i])) return false;
                    }
                }
                if (target.x < window.minX || target.x > window.maxX) continue;
                if (counting) { ++matching; continue; }
                Row row; row.x = target.x;
                for (int i = 0; i < 2; ++i) row.values[i] = valid[i] && previousKeys[i].segment == target.segment
                    && (cursors[i].ended || cursors[i].segment == target.segment || compare(previousKeys[i], target) == 0)
                    ? previousValues[i] : qQNaN();
                if (hadSegment && previousSegment != target.segment) {
                    reducer.Flush();
                    for (int flat : indices) result.series[flat].samples.append({row.x, qQNaN()});
                    result.relation.samples.append({row.x, qQNaN()});
                }
                previousSegment = target.segment; hadSegment = true;
                reducer.Add(row);
                ++result.decodedRecords;
            }
        } else if (interpolate) {
            auto& left = cursors[0];
            auto& right = cursors[1];
            Key previousRight{};
            double previousY = qQNaN();
            bool hasPreviousRight = false;
            while (!left.ended) {
                const Key target = key(left);
                while (!right.ended && compare(key(right), target) < 0) {
                    previousRight = key(right); previousY = right.y; hasPreviousRight = true;
                    if (!advance(right)) return false;
                }
                double value = qQNaN();
                if (!right.ended && compare(key(right), target) == 0) value = right.y;
                else if (!right.ended && hasPreviousRight
                         && previousRight.segment == target.segment && right.segment == target.segment
                         && compare(previousRight, target) < 0 && compare(target, key(right)) < 0
                         && std::isfinite(previousY) && std::isfinite(right.y)) {
                    // Subtract uint64 ticks BEFORE conversion: preserve small offsets at huge epochs.
                    const double fraction = fixedRecords_
                        ? (left.x - previousRight.x) / (right.x - previousRight.x)
                        : double(left.timestamp - previousRight.timestamp) / double(right.timestamp - previousRight.timestamp);
                    value = std::lerp(previousY, right.y, fraction);
                }
                if (counting) ++matching;
                else {
                    Row row; row.x = left.x; row.values[0] = left.y; row.values[1] = value;
                    if (hadSegment && previousSegment != left.segment) {
                        reducer.Flush();
                        for (int flat : indices) result.series[flat].samples.append({row.x, qQNaN()});
                        result.relation.samples.append({row.x, qQNaN()});
                    }
                    previousSegment = left.segment; hadSegment = true;
                    reducer.Add(row);
                    ++result.decodedRecords;
                }
                if (!advance(left)) return false;
            }
        } else while (true) {
            bool ended = false;
            Key target = key(cursors[0]);
            for (int i = 0; i < indices.size(); ++i) {
                ended = ended || cursors[i].ended;
                if (compare(key(cursors[i]), target) > 0) target = key(cursors[i]);
            }
            if (ended) break;
            bool matched = true;
            for (int i = 0; i < indices.size(); ++i) {
                auto& cursor = cursors[i];
                while (!cursor.ended && compare(key(cursor), target) < 0)
                    if (!advance(cursor)) return false;
                matched = matched && !cursor.ended && compare(key(cursor), target) == 0;
            }
            if (!matched) continue;
            if (counting) {
                ++matching;
                for (int i = 0; i < indices.size(); ++i) if (!advance(cursors[i])) return false;
                continue;
            }
            Row row;
            row.x = fixedRecords_ && operation == -1 ? double(cursors[0].timestamp) : cursors[0].x;
            for (int i = 0; i < indices.size(); ++i) row.values[i] = cursors[i].y;
            if (hadSegment && previousSegment != target.segment) {
                reducer.Flush();
                for (int flat : indices) result.series[flat].samples.append({row.x, qQNaN()});
                if (operation >= 0) result.relation.samples.append({row.x, qQNaN()});
            }
            previousSegment = target.segment; hadSegment = true;
            reducer.Add(row);
            ++result.decodedRecords;
            for (int i = 0; i < indices.size(); ++i) if (!advance(cursors[i])) return false;
        }
        if (counting) {
            reducer.width = matching <= limit - overhead ? 1 : (matching + bucketCapacity - 1) / bucketCapacity;
            result.reduced = reducer.width > 1;
        }
    } // counting pass / data pass
    reducer.Flush();
    if (holdPrevious) SeriesProcessing::MakeSteps(result.relation.samples);
    for (const auto& series : result.series) result.retainedPoints += series.samples.size();
    result.retainedPoints += result.relation.samples.size();
    output = std::move(result);
    return true;
}
}
