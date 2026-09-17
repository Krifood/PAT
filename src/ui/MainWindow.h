#pragma once

#include "core/DataSession.h"
#include "core/FormatDocument.h"
#include "core/LiveDataSession.h"
#include "ui/DisplayGroupManager.h"
#include "ui/SignalTreeController.h"
#ifdef PAT_ENABLE_SERIAL
#include "serial/SerialCaptureWorker.h"
#endif

#include <QMainWindow>

#include <memory>

class QLabel;
class SignalTreeWidget;
class SignalTreeController;
class ChartArea;
class QTreeWidgetItem;
#ifdef PAT_ENABLE_SERIAL
class QComboBox;
class QSpinBox;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QTableWidget;
class QThread;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OpenFormatFile();
    void OpenDataFile();
    void ExportRawFrames();
    void PreviewFrame();
    void NewFormatFile();
    void NewSerialFormatFile();
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
#ifdef PAT_ENABLE_SERIAL
    void SetupSerialDock();
    void RefreshSerialPorts();
    void StartSerialCapture();
    void StopSerialCapture();
    void HandleSerialSamples(const QVector<pat::LiveSample>& samples, quint64 skippedBytes);
    void HandleSerialStopped();
#endif

    pat::FormatDocument formatDocument_;
    pat::DataSession dataSession_;
    pat::LiveDataSession liveSession_;
    DisplayGroupManager displayGroupManager_;

    SignalTreeWidget* signalTree_ = nullptr;
    std::unique_ptr<SignalTreeController> signalTreeController_;
    ChartArea* chartArea_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    int maxVisiblePoints_ = 5000;
    bool signalTreeUpdating_ = false;
    bool liveMode_ = false;
#ifdef PAT_ENABLE_SERIAL
    QComboBox* portCombo_ = nullptr;
    QComboBox* dataBitsCombo_ = nullptr;
    QComboBox* parityCombo_ = nullptr;
    QComboBox* stopBitsCombo_ = nullptr;
    QComboBox* flowControlCombo_ = nullptr;
    QSpinBox* baudSpin_ = nullptr;
    QSpinBox* timeoutSpin_ = nullptr;
    QLineEdit* logDirectoryEdit_ = nullptr;
    QPushButton* startCaptureButton_ = nullptr;
    QPushButton* stopCaptureButton_ = nullptr;
    QPushButton* pauseDisplayButton_ = nullptr;
    QCheckBox* followLatestCheck_ = nullptr;
    QTableWidget* liveFrameTable_ = nullptr;
    QThread* serialThread_ = nullptr;
    pat::SerialCaptureWorker* serialWorker_ = nullptr;
    bool captureRunning_ = false;
    bool displayPaused_ = false;
    bool liveChartsInitialized_ = false;
#endif
};
