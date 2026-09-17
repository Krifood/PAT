#include "core/InputReader.h"

#include <QFile>
#include <QFileInfo>

#include <utility>

namespace pat {
namespace {

bool IsSeparator(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t'
        || ch == ',' || ch == ';' || ch == '[' || ch == ']'
        || ch == '(' || ch == ')' || ch == '{' || ch == '}';
}

bool IsHex(char ch) {
    return (ch >= '0' && ch <= '9')
        || (ch >= 'a' && ch <= 'f')
        || (ch >= 'A' && ch <= 'F');
}

}  // namespace

bool IsHexTextPath(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QStringLiteral("txt") || suffix == QStringLiteral("log")
        || suffix == QStringLiteral("hex") || suffix == QStringLiteral("csv");
}

bool ReadInputBytes(QIODevice& device,
                    bool hexText,
                    const std::function<bool(const QByteArray&)>& consumer,
                    InputReadStats& stats,
                    QString& errorMessage) {
    stats = {};
    if (!device.isOpen() || !device.isReadable()) {
        errorMessage = QStringLiteral("输入未打开或不可读");
        return false;
    }
    QByteArray token;
    QByteArray output;
    const auto flushOutput = [&]() -> bool {
        if (output.isEmpty()) return true;
        const QByteArray bytes = std::move(output);
        output = QByteArray{};
        return consumer(bytes);
    };
    const auto parseToken = [&]() -> bool {
        if (token.isEmpty()) return true;
        QByteArray digits = token;
        token.clear();
        if (digits.startsWith("0x") || digits.startsWith("0X")) digits.remove(0, 2);
        if (digits.size() < 2 || digits.size() % 2 != 0) {
            ++stats.skippedTokens;
            return true;
        }
        for (char ch : digits) {
            if (!IsHex(ch)) {
                ++stats.skippedTokens;
                return true;
            }
        }
        const QByteArray bytes = QByteArray::fromHex(digits);
        stats.validBytes += static_cast<quint64>(bytes.size());
        output.append(bytes);
        if (output.size() >= 64 * 1024) return flushOutput();
        return true;
    };

    while (!device.atEnd()) {
        const QByteArray chunk = device.read(64 * 1024);
        if (chunk.isEmpty()) {
            if (device.atEnd()) break;
            errorMessage = QStringLiteral("读取输入失败：%1").arg(device.errorString());
            return false;
        }
        if (!hexText) {
            stats.validBytes += static_cast<quint64>(chunk.size());
            if (!consumer(chunk)) return false;
            continue;
        }
        for (char ch : chunk) {
            if (IsSeparator(ch)) {
                if (!parseToken()) return false;
            } else {
                token.append(ch);
                // Permit continuous hex dumps larger than the one-hour design load.
                // Keep a ceiling for malformed files without separators.
                if (token.size() > 64 * 1024 * 1024) {
                    errorMessage = QStringLiteral("十六进制文本含超长连续片段，无法安全解析");
                    return false;
                }
            }
        }
    }
    if (hexText) {
        if (!parseToken() || !flushOutput()) return false;
        if (stats.validBytes == 0) {
            errorMessage = QStringLiteral("文本中没有可识别的十六进制字节");
            return false;
        }
    }
    return true;
}

}  // namespace pat
