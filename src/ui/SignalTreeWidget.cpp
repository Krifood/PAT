#include "ui/SignalTreeWidget.h"

#include <QDropEvent>
#include <QMimeData>

SignalTreeWidget::SignalTreeWidget(QWidget* parent) : QTreeWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
}

void SignalTreeWidget::dropEvent(QDropEvent* event) {
    auto* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }
    const int targetType = targetItem->data(0, kItemTypeRole).toInt();
    if (targetType != kItemTypeSignal) {
        event->ignore();
        return;
    }

    QVector<int> indices;
    for (auto* item : selectedItems()) {
        if (item->data(0, kItemTypeRole).toInt() != kItemTypeSignal) continue;
        const int idx = item->data(0, kSignalIndexRole).toInt();
        if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
    }

    const int targetIndex = targetItem->data(0, kSignalIndexRole).toInt();
    if (targetIndex >= 0 && !indices.contains(targetIndex)) {
        indices.append(targetIndex);
    }

    if (indices.size() >= 2) {
        emit MergeRequested(indices);
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}
