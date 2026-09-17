#include "core/SerialFrameAssembler.h"

#include <algorithm>
#include <utility>

namespace pat {

SerialFrameAssembler::SerialFrameAssembler(QVector<FormatDefinition> formats)
    : formats_(std::move(formats)) {
    if (formats_.isEmpty()) {
        configurationError_ = QStringLiteral("未选择协议模板");
        return;
    }
    for (int i = 0; i < formats_.size(); ++i) {
        const auto& current = formats_[i];
        if (!current.HasFraming() || current.recordSize < current.frameHeader.size()) {
            configurationError_ = QStringLiteral("模板缺少有效分帧配置：%1").arg(current.templateId);
            return;
        }
        longestHeader_ = std::max(longestHeader_, static_cast<int>(current.frameHeader.size()));
        for (int j = 0; j < i; ++j) {
            const auto& previous = formats_[j];
            if (current.frameHeader.startsWith(previous.frameHeader)
                || previous.frameHeader.startsWith(current.frameHeader)) {
                configurationError_ = QStringLiteral("模板帧头重叠：%1 / %2")
                                          .arg(previous.templateId, current.templateId);
                return;
            }
        }
    }
}

QVector<MatchedFrame> SerialFrameAssembler::Feed(const QByteArray& bytes) {
    QVector<MatchedFrame> frames;
    if (!configurationError_.isEmpty() || bytes.isEmpty()) return frames;
    pending_.append(bytes);

    while (!pending_.isEmpty()) {
        int matchIndex = -1;
        int matchOffset = pending_.size();
        for (int i = 0; i < formats_.size(); ++i) {
            const int offset = pending_.indexOf(formats_[i].frameHeader);
            if (offset >= 0 && offset < matchOffset) {
                matchOffset = offset;
                matchIndex = i;
            }
        }

        if (matchIndex < 0) {
            const qsizetype keep = std::min(pending_.size(), static_cast<qsizetype>(longestHeader_ - 1));
            skippedBytes_ += static_cast<quint64>(pending_.size() - keep);
            pending_.remove(0, pending_.size() - keep);
            break;
        }

        if (matchOffset > 0) {
            skippedBytes_ += static_cast<quint64>(matchOffset);
            pending_.remove(0, matchOffset);
        }
        const int frameLength = formats_[matchIndex].recordSize;
        if (pending_.size() < frameLength) break;

        frames.append({matchIndex, pending_.left(frameLength)});
        pending_.remove(0, frameLength);
    }
    return frames;
}

void SerialFrameAssembler::Reset() {
    pending_.clear();
    skippedBytes_ = 0;
}

}  // namespace pat
