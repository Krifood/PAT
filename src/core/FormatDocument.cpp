#include "core/FormatDocument.h"

#include <QFile>

#include <utility>

namespace pat {

bool FormatDocument::LoadFromFile(const QString& path, QString& errorMessage) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorMessage = QStringLiteral("无法打开格式文件：%1").arg(path);
        return false;
    }

    const QByteArray data = file.readAll();
    FormatDefinition tmp;
    if (!LoadFormatFromJsonData(data, tmp, errorMessage)) {
        return false;
    }

    format_ = std::move(tmp);
    jsonText_ = QString::fromUtf8(data);
    path_ = path;
    hasFormat_ = true;
    return true;
}

bool FormatDocument::LoadFromJsonText(const QString& jsonText, QString& errorMessage) {
    FormatDefinition tmp;
    if (!LoadFormatFromJsonData(jsonText.toUtf8(), tmp, errorMessage)) {
        return false;
    }

    format_ = std::move(tmp);
    jsonText_ = jsonText;
    hasFormat_ = true;
    return true;
}

bool FormatDocument::Save(QString& errorMessage) const {
    if (path_.isEmpty()) {
        errorMessage = QStringLiteral("格式文件路径为空");
        return false;
    }
    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        errorMessage = QStringLiteral("无法写入：%1").arg(path_);
        return false;
    }
    file.write(jsonText_.toUtf8());
    return true;
}

bool FormatDocument::SaveAs(const QString& path, QString& errorMessage) {
    if (path.isEmpty()) {
        errorMessage = QStringLiteral("格式文件路径为空");
        return false;
    }
    path_ = path;
    return Save(errorMessage);
}

void FormatDocument::Clear() {
    format_ = FormatDefinition{};
    jsonText_.clear();
    path_.clear();
    hasFormat_ = false;
}

}  // namespace pat
