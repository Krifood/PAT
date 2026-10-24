#include "ui/DisplayGroupManager.h"

#include <QObject>

#include <algorithm>
#include <utility>

QString DisplayGroupManager::ResolveUnitLabel(const QVector<int>& indices,
                                              const pat::FormatDefinition& format,
                                              bool& outMixed) {
    outMixed = false;
    QString unit;
    for (int idx : indices) {
        if (idx < 0 || idx >= static_cast<int>(format.signalFormats.size())) {
            outMixed = true;
            continue;
        }
        const QString current = format.signalFormats[static_cast<size_t>(idx)].unit;
        if (current.isEmpty()) continue;
        if (unit.isEmpty()) {
            unit = current;
        } else if (current != unit) {
            outMixed = true;
            break;
        }
    }
    if (outMixed) return QObject::tr("混合单位");
    return unit;
}

bool DisplayGroupManager::CanMerge(const QVector<int>& indices,
                                   const pat::FormatDefinition& format,
                                   QString& outUnit) {
    if (indices.size() < 2) return false;
    bool mixed = false;
    outUnit = ResolveUnitLabel(indices, format, mixed);
    return true;
}

void DisplayGroupManager::UpdateGroups(const QVector<int>& checkedIndices, const pat::FormatDefinition& format) {
    groups_.clear();
    if (checkedIndices.isEmpty()) return;

    QVector<QVector<int>> validMerged;
    QVector<int> mergedIndices;

    for (auto group : mergedGroups_) {
        group.erase(std::remove_if(group.begin(), group.end(),
            [&](int idx) { return !checkedIndices.contains(idx); }), group.end());
        if (group.size() < 2) continue;
        validMerged.append(group);
        for (int idx : group) {
            if (!mergedIndices.contains(idx)) mergedIndices.append(idx);
        }

        DisplayGroup displayGroup;
        displayGroup.signalIndices = group;
        displayGroup.merged = true;
        bool mixed = false;
        displayGroup.unit = ResolveUnitLabel(group, format, mixed);
        QStringList names;
        for (int idx : group) {
            if (idx >= 0 && idx < static_cast<int>(format.signalFormats.size())) {
                names.append(format.signalFormats[static_cast<size_t>(idx)].QualifiedDisplayName());
            }
        }
        displayGroup.title = QObject::tr("合并: %1").arg(names.join(QStringLiteral(" + ")));
        groups_.append(displayGroup);
    }
    mergedGroups_ = std::move(validMerged);

    for (int idx : checkedIndices) {
        if (mergedIndices.contains(idx)) continue;
        DisplayGroup group;
        group.signalIndices = {idx};
        group.merged = false;
        if (idx >= 0 && idx < static_cast<int>(format.signalFormats.size())) {
            group.title = format.signalFormats[static_cast<size_t>(idx)].QualifiedDisplayName();
            group.unit = format.signalFormats[static_cast<size_t>(idx)].unit;
        }
        groups_.append(group);
    }
}

bool DisplayGroupManager::MergeSignals(const QVector<int>& indices,
                                       const pat::FormatDefinition& format,
                                       QString& errorMessage) {
    errorMessage.clear();
    QVector<int> merged = indices;
    for (int idx : merged) {
        if (idx < 0 || idx >= static_cast<int>(format.signalFormats.size())) {
            errorMessage = QObject::tr("只能合并有效的原始信号；派生关系图暂不支持合并");
            return false;
        }
    }
    std::sort(merged.begin(), merged.end());
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
    if (merged.size() < 2) return false;

    // Merging any member of an existing plot brings the whole plot along.
    // Expand to closure before removing old groups, never replace them by a pair.
    bool expanded;
    do {
        expanded = false;
        for (const auto& group : mergedGroups_) {
            if (!std::any_of(group.begin(), group.end(),
                             [&](int idx) { return merged.contains(idx); })) continue;
            for (int idx : group) {
                if (!merged.contains(idx)) { merged.append(idx); expanded = true; }
            }
        }
    } while (expanded);
    std::sort(merged.begin(), merged.end());

    QVector<QVector<int>> filtered;
    for (const auto& group : mergedGroups_) {
        bool intersects = false;
        for (int idx : group) {
            if (merged.contains(idx)) {
                intersects = true;
                break;
            }
        }
        if (!intersects) filtered.append(group);
    }
    mergedGroups_ = std::move(filtered);
    mergedGroups_.append(merged);
    return true;
}

void DisplayGroupManager::UnmergeSignals(const QVector<int>& indices) {
    if (indices.isEmpty()) return;

    QVector<QVector<int>> filtered;
    for (const auto& group : mergedGroups_) {
        bool intersects = false;
        for (int idx : group) {
            if (indices.contains(idx)) {
                intersects = true;
                break;
            }
        }
        if (!intersects) filtered.append(group);
    }
    mergedGroups_ = std::move(filtered);
}

void DisplayGroupManager::RemoveSignals(const QVector<int>& indices) {
    if (indices.isEmpty()) return;
    QVector<QVector<int>> filtered;
    for (auto group : mergedGroups_) {
        group.erase(std::remove_if(group.begin(), group.end(), [&](int idx) { return indices.contains(idx); }),
                    group.end());
        if (group.size() >= 2) filtered.append(group);
    }
    mergedGroups_ = std::move(filtered);
}

void DisplayGroupManager::Clear() {
    mergedGroups_.clear();
    groups_.clear();
}

bool DisplayGroupManager::HasMergedSignals(const QVector<int>& indices) const {
    if (indices.isEmpty()) return false;
    for (const auto& group : mergedGroups_) {
        for (int idx : group) {
            if (indices.contains(idx)) return true;
        }
    }
    return false;
}
