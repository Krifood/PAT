#pragma once

#include "core/CaptureSession.h"

#include <QObject>
#include <QSerialPort>
#include <QTimer>

namespace pat {

struct SerialCaptureConfig {
    QString portName;
    qint32 baudRate = 230400;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    int inactivityTimeoutMs = 5000;
    QString logDirectory;
};

class SerialCaptureWorker : public QObject {
    Q_OBJECT

public:
    SerialCaptureWorker(FormatDefinition format, SerialCaptureConfig config);

public slots:
    void Start();
    void Stop();

signals:
    void Started(const QString& rawPath, const QString& csvPath);
    void Stopped();
    void Error(const QString& message);
    void Status(const QString& message);
    void SamplesReady(const QVector<pat::LiveSample>& samples, quint64 skippedBytes);

private:
    void ReadAvailable();
    void EmitPending();
    void Fail(const QString& message);

    SerialCaptureConfig config_;
    CaptureSession session_;
    QSerialPort* port_ = nullptr;
    QTimer* displayTimer_ = nullptr;
    QTimer* inactivityTimer_ = nullptr;
    QVector<LiveSample> pendingSamples_;
    bool running_ = false;
    bool stopping_ = false;
};

}  // namespace pat
