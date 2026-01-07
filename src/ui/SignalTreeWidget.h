#pragma once

#include <QTreeWidget>

class SignalTreeWidget : public QTreeWidget {
    Q_OBJECT

public:
    static constexpr int kItemTypeRole = Qt::UserRole + 1;
    static constexpr int kSignalIndexRole = Qt::UserRole + 2;
    static constexpr int kItemTypeGroup = 1;
    static constexpr int kItemTypeSignal = 2;

    explicit SignalTreeWidget(QWidget* parent = nullptr);

signals:
    void MergeRequested(const QVector<int>& indices);

protected:
    void dropEvent(QDropEvent* event) override;
};
