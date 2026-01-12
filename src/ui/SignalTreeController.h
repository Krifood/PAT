#pragma once

#include "core/FormatDefinition.h"
#include "ui/SignalTreeWidget.h"

#include <QVector>
#include <QtCore/Qt>

class QMimeData;
class QTreeWidgetItem;

class SignalTreeController {
public:
    explicit SignalTreeController(SignalTreeWidget* tree);

    void Build(const pat::FormatDefinition& format);
    QVector<int> CollectCheckedSignalIndices() const;
    QVector<int> CollectSelectedSignalIndices() const;
    QVector<int> CollectSignalIndices(const QTreeWidgetItem* item) const;
    void SetSignalsChecked(const QVector<int>& indices, bool checked);
    void SetSignalsCheckedUnderItem(QTreeWidgetItem* item, Qt::CheckState state);
    void SetAllSignalsChecked(bool checked);
    void UpdateParentCheckStates(QTreeWidgetItem* item);
    bool ReadSignalIndicesMime(const QMimeData* mime, QVector<int>& outIndices) const;

private:
    void UpdateAllGroupStates();
    static Qt::CheckState ComputeChildState(const QTreeWidgetItem* item);

    SignalTreeWidget* tree_ = nullptr;
};
