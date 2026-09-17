#pragma once

#include <QIODevice>
#include <QString>

#include <functional>

namespace pat {

struct InputReadStats {
    quint64 validBytes = 0;
    quint64 skippedTokens = 0;
};

bool IsHexTextPath(const QString& path);
bool ReadInputBytes(QIODevice& device,
                    bool hexText,
                    const std::function<bool(const QByteArray&)>& consumer,
                    InputReadStats& stats,
                    QString& errorMessage);

}  // namespace pat
