#pragma once

#include <QTreeWidget>

class QMimeData;

class SignalTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    static constexpr const char* kSignalIndicesMime = "application/x-pat-signal-indices";
    static constexpr int kItemTypeRole = Qt::UserRole + 1;
    static constexpr int kSignalIndexRole = Qt::UserRole + 2;
    static constexpr int kItemTypeGroup = 1;
    static constexpr int kItemTypeSignal = 2;

    explicit SignalTreeWidget(QWidget* parent = nullptr);

signals:
    void MergeRequested(const QVector<int>& indices);

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
    void dropEvent(QDropEvent* event) override;
};
