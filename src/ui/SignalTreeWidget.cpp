#include "ui/SignalTreeWidget.h"

#include <QContextMenuEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QDataStream>
#include <functional>

SignalTreeWidget::SignalTreeWidget(QWidget* parent) : QTreeWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);
    if (viewport()) {
        viewport()->setAcceptDrops(true);
    }
}

void SignalTreeWidget::contextMenuEvent(QContextMenuEvent* event) {
    if (!event) return;
    const QPoint viewportPos = viewport() ? viewport()->mapFromGlobal(event->globalPos()) : event->pos();
    emit customContextMenuRequested(viewportPos);
    event->accept();
}

bool SignalTreeWidget::viewportEvent(QEvent* event) {
    if (event && event->type() == QEvent::ContextMenu) {
        auto* ctx = static_cast<QContextMenuEvent*>(event);
        emit customContextMenuRequested(ctx->pos());
        ctx->accept();
        return true;
    }
    return QTreeWidget::viewportEvent(event);
}

QMimeData* SignalTreeWidget::mimeData(const QList<QTreeWidgetItem*>& items) const {
    QMimeData* mime = QTreeWidget::mimeData(items);
    QVector<int> indices;

    const std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, ITEM_TYPE_ROLE).toInt();
        if (type == ITEM_TYPE_SIGNAL) {
            const int idx = item->data(0, SIGNAL_INDEX_ROLE).toInt();
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
        mime->setData(SIGNAL_INDICES_MIME, payload);
    }
    return mime;
}

void SignalTreeWidget::dropEvent(QDropEvent* event) {
    auto* targetItem = itemAt(event->position().toPoint());
    if (!targetItem) {
        event->ignore();
        return;
    }
    const int targetType = targetItem->data(0, ITEM_TYPE_ROLE).toInt();
    if (targetType != ITEM_TYPE_SIGNAL) {
        event->ignore();
        return;
    }

    QVector<int> indices;
    const std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, ITEM_TYPE_ROLE).toInt();
        if (type == ITEM_TYPE_SIGNAL) {
            const int idx = item->data(0, SIGNAL_INDEX_ROLE).toInt();
            if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
            return;
        }
        for (int i = 0; i < item->childCount(); ++i) {
            collect(item->child(i));
        }
    };

    for (auto* item : selectedItems()) collect(item);

    const int targetIndex = targetItem->data(0, SIGNAL_INDEX_ROLE).toInt();
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
