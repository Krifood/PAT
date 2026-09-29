#pragma once

#include "core/DataSession.h"
#include "core/FormatDocument.h"
#include "core/LiveDataSession.h"
#include "core/MixedDataSession.h"
#include "core/SeriesProcessing.h"
#include "ui/AppTheme.h"
#include "ui/DisplayGroupManager.h"
#include "ui/SignalTreeController.h"
#ifdef PAT_ENABLE_SERIAL
#include "serial/SerialCaptureWorker.h"
#endif

#include <QMainWindow>

#include <memory>

class MixedLoadController;
class QLabel;
class SignalTreeWidget;
class SignalTreeController;
class ChartArea;
class AboutDialog;
class TrajectoryView;
class QTabWidget;
class QTreeWidgetItem;
class QMenu;
class QLineEdit;
#ifdef PAT_ENABLE_SERIAL
class QComboBox;
class QSpinBox;
class QPushButton;
class QCheckBox;
class QTableWidget;
class QThread;
#endif

class MainWindow : public QMainWindow {
    Q_OBJECT
    friend class UiTests;

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OpenFormatFile();
    void OpenDataFile();
    void ImportVmcLog();
    void ExportRawFrames();
    void PreviewFrame();
    void NewFormatFile();
    void NewSerialFormatFile();
    void EditFormatFile();
    void SaveFormatFile();
    void SaveFormatFileAs();
    void SetMaxVisiblePoints();
    void ShowVersionInfo();
    void ShowChangelog();

private:
    void SetupUi();
    void HandleSignalTreeItemChanged(QTreeWidgetItem* item, int column);
    void UpdateCharts();
    void UpdateStatus(const QString& text);
    void BuildSignalTree();
    void ShowSignalTreeMenu(const QPoint& pos);
    void ShowSearchResultsMenu(const QPoint& pos);
    void ShowSignalActionsMenu(const QVector<int>& targetIndices,
                               bool hasGroupContext,
                               const QPoint& globalPos);
    void UpdateSignalSearchResults(const QString& query);
    void SyncSignalSearchChecks();
    void HandleSearchResultItemChanged(QTreeWidgetItem* item, int column);
    void HandleSignalsDropped(const QVector<int>& indices);
    void HandleMergeRequested(const QVector<int>& indices);
    void HandleReorderRequested(int fromIndex, int toIndex);
    void HandleHideSignalsRequested(const QVector<int>& indices);
    void HandleSplitRequested(const QVector<int>& indices);
    void ShowSignalTransformDialog(int signalIndex);
    void ShowDifferenceDialog(const QVector<int>& suggestedSignals);
    bool CreateDerived(int leftSignal, int rightSignal, pat::SeriesOperation operation,
                       QString& errorMessage);
    bool CreateDifference(int leftSignal, int rightSignal, QString& errorMessage);
    bool CreateSum(int leftSignal, int rightSignal, QString& errorMessage);
    void ApplyTheme(AppThemeId id, bool persist = true);
    void RequestMixedSignalsForCurrentView();
    const QVector<pat::Series>& OfflineSeries() const;
    const pat::SeriesStatistics& OfflineStatistics() const;
    bool HasOfflineData() const;
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
    pat::MixedDataSession mixedDataSession_;
    pat::LiveDataSession liveSession_;
    DisplayGroupManager displayGroupManager_;
    pat::SeriesProcessing seriesProcessing_;

    SignalTreeWidget* signalTree_ = nullptr;
    QLineEdit* signalSearchEdit_ = nullptr;
    SignalTreeWidget* signalSearchResults_ = nullptr;
    std::unique_ptr<SignalTreeController> signalTreeController_;
    ChartArea* chartArea_ = nullptr;
    TrajectoryView* trajectoryView_ = nullptr;
    QTabWidget* dataViewTabs_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    AboutDialog* infoDialog_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    int maxVisiblePoints_ = 5000;
    bool signalTreeUpdating_ = false;
    bool signalSearchUpdating_ = false;
    bool liveMode_ = false;
    bool mixedMode_ = false;
    MixedLoadController* mixedLoader_ = nullptr;
    pat::MixedReadWindow mixedWindow_;
    AppThemeId currentThemeId_ = AppThemeId::IndoorDark;
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
    QLabel* captureStatsLabel_ = nullptr;
    QThread* serialThread_ = nullptr;
    pat::SerialCaptureWorker* serialWorker_ = nullptr;
    bool captureRunning_ = false;
    bool displayPaused_ = false;
    bool liveChartsInitialized_ = false;
#endif
};
