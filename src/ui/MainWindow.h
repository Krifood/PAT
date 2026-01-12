#pragma once

#include "core/DataSession.h"
#include "core/FormatDocument.h"
#include "ui/DisplayGroupManager.h"
#include "ui/SignalTreeController.h"

#include <QMainWindow>

#include <memory>

class QLabel;
class SignalTreeWidget;
class SignalTreeController;
class ChartArea;
class QTreeWidgetItem;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void OpenFormatFile();
    void OpenDataFile();
    void NewFormatFile();
    void EditFormatFile();
    void SaveFormatFile();
    void SaveFormatFileAs();
    void SetMaxVisiblePoints();

private:
    void SetupUi();
    void HandleSignalTreeItemChanged(QTreeWidgetItem* item, int column);
    void UpdateCharts();
    void UpdateStatus(const QString& text);
    void BuildSignalTree();
    void ShowSignalTreeMenu(const QPoint& pos);
    void HandleSignalsDropped(const QVector<int>& indices);
    void HandleMergeRequested(const QVector<int>& indices);
    void HandleReorderRequested(int fromIndex, int toIndex);
    void HandleHideSignalsRequested(const QVector<int>& indices);

    pat::FormatDocument formatDocument_;
    pat::DataSession dataSession_;
    DisplayGroupManager displayGroupManager_;

    SignalTreeWidget* signalTree_ = nullptr;
    std::unique_ptr<SignalTreeController> signalTreeController_;
    ChartArea* chartArea_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    int maxVisiblePoints_ = 5000;
    bool signalTreeUpdating_ = false;
};
