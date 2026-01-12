#include "ui/SignalTreeController.h"

#include <QDataStream>
#include <QMap>
#include <QMimeData>
#include <QObject>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QTreeWidgetItem>

#include <functional>

namespace {

QStringList SplitGroupPath(const QString& groupPath) {
    if (groupPath.trimmed().isEmpty()) return {};
    return groupPath.split('/', Qt::SkipEmptyParts);
}

}  // namespace

SignalTreeController::SignalTreeController(SignalTreeWidget* tree) : tree_(tree) {}

void SignalTreeController::Build(const pat::FormatDefinition& format) {
    if (!tree_) return;

    QSignalBlocker blocker(tree_);
    tree_->clear();
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({QObject::tr("信号"), QObject::tr("说明"), QObject::tr("时间比例")});

    QMap<QString, QTreeWidgetItem*> groupItems;
    auto* root = tree_->invisibleRootItem();

    for (int i = 0; i < static_cast<int>(format.signalFormats.size()); ++i) {
        const auto& signal = format.signalFormats[static_cast<size_t>(i)];
        QTreeWidgetItem* parent = root;
        const QStringList groups = SplitGroupPath(signal.groupPath);
        QString currentPath;
        for (const auto& groupName : groups) {
            if (!currentPath.isEmpty()) currentPath += '/';
            currentPath += groupName;
            if (!groupItems.contains(currentPath)) {
                const QString groupDescription = format.groupDescriptions.value(currentPath);
                auto* groupItem = new QTreeWidgetItem(parent, {groupName, groupDescription, QString()});
                groupItem->setFlags(groupItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate | Qt::ItemIsDragEnabled);
                groupItem->setCheckState(0, Qt::Unchecked);
                groupItem->setData(0, SignalTreeWidget::ITEM_TYPE_ROLE, SignalTreeWidget::ITEM_TYPE_GROUP);
                groupItems.insert(currentPath, groupItem);
                parent = groupItem;
            } else {
                parent = groupItems.value(currentPath);
            }
        }

        QString timeScaleText = QString::number(signal.timeScale, 'f', 3);
        if (!signal.timeUnit.isEmpty()) {
            timeScaleText = QStringLiteral("%1 %2").arg(timeScaleText, signal.timeUnit);
        }
        auto* signalItem = new QTreeWidgetItem(parent, {signal.name, signal.description, timeScaleText});
        signalItem->setFlags(signalItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable |
                             Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        signalItem->setCheckState(0, Qt::Unchecked);
        signalItem->setData(0, SignalTreeWidget::ITEM_TYPE_ROLE, SignalTreeWidget::ITEM_TYPE_SIGNAL);
        signalItem->setData(0, SignalTreeWidget::SIGNAL_INDEX_ROLE, i);
    }

    tree_->expandAll();
    UpdateAllGroupStates();
}

QVector<int> SignalTreeController::CollectCheckedSignalIndices() const {
    QVector<int> indices;
    if (!tree_) return indices;

    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt();
        if (type == SignalTreeWidget::ITEM_TYPE_SIGNAL && item->checkState(0) == Qt::Checked) {
            const int idx = item->data(0, SignalTreeWidget::SIGNAL_INDEX_ROLE).toInt();
            if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
        }
        for (int i = 0; i < item->childCount(); ++i) {
            visit(item->child(i));
        }
    };

    auto* root = tree_->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) {
        visit(root->child(i));
    }
    return indices;
}

QVector<int> SignalTreeController::CollectSelectedSignalIndices() const {
    QVector<int> indices;
    if (!tree_) return indices;
    for (auto* item : tree_->selectedItems()) {
        if (item->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt() != SignalTreeWidget::ITEM_TYPE_SIGNAL) continue;
        const int idx = item->data(0, SignalTreeWidget::SIGNAL_INDEX_ROLE).toInt();
        if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
    }
    return indices;
}

QVector<int> SignalTreeController::CollectSignalIndices(const QTreeWidgetItem* item) const {
    QVector<int> indices;
    if (!tree_ || !item) return indices;

    const std::function<void(const QTreeWidgetItem*)> visit = [&](const QTreeWidgetItem* node) {
        if (!node) return;
        const int type = node->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt();
        if (type == SignalTreeWidget::ITEM_TYPE_SIGNAL) {
            const int idx = node->data(0, SignalTreeWidget::SIGNAL_INDEX_ROLE).toInt();
            if (idx >= 0 && !indices.contains(idx)) indices.append(idx);
            return;
        }
        for (int i = 0; i < node->childCount(); ++i) {
            visit(node->child(i));
        }
    };

    visit(item);
    return indices;
}

void SignalTreeController::SetSignalsChecked(const QVector<int>& indices, bool checked) {
    if (!tree_) return;
    QSignalBlocker blocker(tree_);
    const QSet<int> target(indices.begin(), indices.end());

    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt();
        if (type == SignalTreeWidget::ITEM_TYPE_SIGNAL) {
            const int idx = item->data(0, SignalTreeWidget::SIGNAL_INDEX_ROLE).toInt();
            if (target.contains(idx)) item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
            return;
        }
        for (int i = 0; i < item->childCount(); ++i) visit(item->child(i));
    };

    auto* root = tree_->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) visit(root->child(i));
    UpdateAllGroupStates();
}

void SignalTreeController::SetSignalsCheckedUnderItem(QTreeWidgetItem* item, Qt::CheckState state) {
    if (!tree_ || !item) return;
    QSignalBlocker blocker(tree_);
    const Qt::CheckState targetState = (state == Qt::PartiallyChecked) ? Qt::Checked : state;

    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* node) {
        if (!node) return;
        const int type = node->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt();
        if (type == SignalTreeWidget::ITEM_TYPE_SIGNAL) {
            node->setCheckState(0, targetState);
            return;
        }
        if (type == SignalTreeWidget::ITEM_TYPE_GROUP) {
            node->setCheckState(0, targetState);
        }
        for (int i = 0; i < node->childCount(); ++i) {
            visit(node->child(i));
        }
    };

    visit(item);
    UpdateAllGroupStates();
}

void SignalTreeController::SetAllSignalsChecked(bool checked) {
    if (!tree_) return;
    QSignalBlocker blocker(tree_);

    const std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
        if (!item) return;
        const int type = item->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt();
        if (type == SignalTreeWidget::ITEM_TYPE_SIGNAL || type == SignalTreeWidget::ITEM_TYPE_GROUP) {
            item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        }
        for (int i = 0; i < item->childCount(); ++i) visit(item->child(i));
    };

    auto* root = tree_->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) visit(root->child(i));
    UpdateAllGroupStates();
}

void SignalTreeController::UpdateParentCheckStates(QTreeWidgetItem* item) {
    if (!tree_ || !item) return;
    UpdateAllGroupStates();
}

bool SignalTreeController::ReadSignalIndicesMime(const QMimeData* mime, QVector<int>& outIndices) const {
    outIndices.clear();
    if (!mime || !mime->hasFormat(SignalTreeWidget::SIGNAL_INDICES_MIME)) return false;
    const QByteArray payload = mime->data(SignalTreeWidget::SIGNAL_INDICES_MIME);
    QDataStream stream(payload);
    stream >> outIndices;
    return !outIndices.isEmpty();
}

void SignalTreeController::UpdateAllGroupStates() {
    if (!tree_) return;
    QSignalBlocker blocker(tree_);

    const std::function<Qt::CheckState(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) -> Qt::CheckState {
        if (!item) return Qt::Unchecked;
        const int type = item->data(0, SignalTreeWidget::ITEM_TYPE_ROLE).toInt();
        if (type == SignalTreeWidget::ITEM_TYPE_SIGNAL) {
            return item->checkState(0);
        }

        bool hasChecked = false;
        bool hasUnchecked = false;
        for (int i = 0; i < item->childCount(); ++i) {
            const Qt::CheckState childState = visit(item->child(i));
            if (childState == Qt::PartiallyChecked) {
                hasChecked = true;
                hasUnchecked = true;
            } else if (childState == Qt::Checked) {
                hasChecked = true;
            } else {
                hasUnchecked = true;
            }
            if (hasChecked && hasUnchecked) break;
        }

        Qt::CheckState state = Qt::Unchecked;
        if (hasChecked && hasUnchecked) {
            state = Qt::PartiallyChecked;
        } else if (hasChecked) {
            state = Qt::Checked;
        }
        if (type == SignalTreeWidget::ITEM_TYPE_GROUP) {
            item->setCheckState(0, state);
        }
        return state;
    };

    auto* root = tree_->invisibleRootItem();
    for (int i = 0; i < root->childCount(); ++i) {
        visit(root->child(i));
    }
}

Qt::CheckState SignalTreeController::ComputeChildState(const QTreeWidgetItem* item) {
    if (!item) return Qt::Unchecked;
    bool hasChecked = false;
    bool hasUnchecked = false;

    for (int i = 0; i < item->childCount(); ++i) {
        const QTreeWidgetItem* child = item->child(i);
        const Qt::CheckState state = child ? child->checkState(0) : Qt::Unchecked;
        if (state == Qt::PartiallyChecked) {
            hasChecked = true;
            hasUnchecked = true;
        } else if (state == Qt::Checked) {
            hasChecked = true;
        } else {
            hasUnchecked = true;
        }
        if (hasChecked && hasUnchecked) break;
    }

    if (hasChecked && hasUnchecked) return Qt::PartiallyChecked;
    if (hasChecked) return Qt::Checked;
    return Qt::Unchecked;
}
