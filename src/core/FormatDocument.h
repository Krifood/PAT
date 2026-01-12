#pragma once

#include "core/FormatDefinition.h"

#include <QString>

namespace pat {

class FormatDocument {
public:
    bool LoadFromFile(const QString& path, QString& errorMessage);
    bool LoadFromJsonText(const QString& jsonText, QString& errorMessage);
    bool Save(QString& errorMessage) const;
    bool SaveAs(const QString& path, QString& errorMessage);
    void Clear();

    bool HasFormat() const { return hasFormat_; }
    const FormatDefinition& Format() const { return format_; }
    const QString& JsonText() const { return jsonText_; }
    const QString& Path() const { return path_; }

private:
    FormatDefinition format_;
    QString jsonText_;
    QString path_;
    bool hasFormat_ = false;
};

}  // namespace pat
