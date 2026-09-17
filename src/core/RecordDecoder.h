#pragma once

#include "core/FormatDefinition.h"

#include <QByteArrayView>
#include <QVector>

namespace pat {

class RecordDecoder {
public:
    explicit RecordDecoder(FormatDefinition format);

    bool Decode(QByteArrayView record, QVector<double>& values, QString& errorMessage) const;

private:
    FormatDefinition format_;
};

}  // namespace pat
