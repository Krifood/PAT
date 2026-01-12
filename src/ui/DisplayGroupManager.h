#pragma once

#include "core/FormatDefinition.h"

#include <QVector>

struct DisplayGroup {
    QVector<int> signalIndices;
    QString title;
    QString unit;
    bool merged = false;
};

class DisplayGroupManager {
public:
    void UpdateGroups(const QVector<int>& checkedIndices, const pat::FormatDefinition& format);
    bool MergeSignals(const QVector<int>& indices, const pat::FormatDefinition& format, QString& errorMessage);
    void UnmergeSignals(const QVector<int>& indices);
    void RemoveSignals(const QVector<int>& indices);
    void Clear();

    const QVector<DisplayGroup>& Groups() const { return groups_; }
    static bool CanMerge(const QVector<int>& indices, const pat::FormatDefinition& format, QString& outUnit);
    bool HasMergedSignals(const QVector<int>& indices) const;

private:
    static QString ResolveUnitLabel(const QVector<int>& indices,
                                    const pat::FormatDefinition& format,
                                    bool& outMixed);

    QVector<QVector<int>> mergedGroups_;
    QVector<DisplayGroup> groups_;
};
