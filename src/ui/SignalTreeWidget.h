#pragma once

#include <QTreeWidget>
#include <QVector>

class QMimeData;

class SignalTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    static constexpr const char* SIGNAL_INDICES_MIME = "application/x-pat-signal-indices";
    static constexpr int ITEM_TYPE_ROLE = Qt::UserRole + 1;
    static constexpr int SIGNAL_INDEX_ROLE = Qt::UserRole + 2;
    static constexpr int ITEM_TYPE_GROUP = 1;
    static constexpr int ITEM_TYPE_SIGNAL = 2;

    explicit SignalTreeWidget(QWidget* parent = nullptr);

signals:
    void MergeRequested(const QVector<int>& indices);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    void dropEvent(QDropEvent* event) override;
};
