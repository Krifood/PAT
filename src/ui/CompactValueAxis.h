#pragma once

#include <QtCharts/QCategoryAxis>

// Only the labels change. Series, ranges, cursor queries and guide coordinates
// always remain in the original numeric domain.
class CompactValueAxis : public QCategoryAxis {
public:
    explicit CompactValueAxis(QObject* parent = nullptr);
    QString Annotation() const { return annotation_; }
    qreal LabelWidth();
    void SetStableLabelWidth();

private:
    void RefreshLabels();
    QString annotation_;
    bool stableLabelWidth_ = false;
};
