#pragma once

#include "core/format_definition.h"

#include <QPointF>
#include <QVector>

namespace pat {

struct Series {
  QString name;
  QString unit;
  QVector<QPointF> samples;
};

class RecordParser {
 public:
  explicit RecordParser(FormatDefinition format);

  bool parseFile(const QString& path, QVector<Series>& outSeries, QString& errorMessage) const;

 private:
  FormatDefinition format_;
};

}  // namespace pat
