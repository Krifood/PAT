#include "ui/SignalTreeWidget.h"

#include <QDropEvent>
#include <QMimeData>
#include <QDataStream>
#include <functional>

SignalTreeWidget::SignalTreeWidget(QWidget* parent) : QTreeWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
}

QMimeData* SignalTreeWidget::mimeData(const QList<QTreeWidgetItem*>& items) const {
    QMimeData* mime = QTreeWidget::mimeData(items);
    QVector<int> indices;

    const std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, kItemTypeRole).toInt();
        if (type == kItemTypeSignal) {
            const int idx = item->data(0, kSignalIndexRole).toInt();
            if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
            return;
        }
        for (int i = 0; i < item->childCount(); ++i) {
            collect(item->child(i));
        }
    };

    for (auto* item : items) collect(item);

    if (!indices.isEmpty()) {
        QByteArray payload;
        QDataStream stream(&payload, QIODevice::WriteOnly);
        stream << indices;
        mime->setData(kSignalIndicesMime, payload);
    }
    return mime;
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
    const std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, kItemTypeRole).toInt();
        if (type == kItemTypeSignal) {
            const int idx = item->data(0, kSignalIndexRole).toInt();
            if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
            return;
        }
        for (int i = 0; i < item->childCount(); ++i) {
            collect(item->child(i));
        }
    };

    for (auto* item : selectedItems()) collect(item);

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
