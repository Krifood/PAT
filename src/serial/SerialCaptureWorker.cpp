#include "serial/SerialCaptureWorker.h"

#include <utility>

namespace pat {

SerialCaptureWorker::SerialCaptureWorker(FormatDefinition format, SerialCaptureConfig config)
    : config_(std::move(config)), session_(std::move(format)) {}

void SerialCaptureWorker::Start() {
    if (running_ || stopping_) return;
    if (config_.portName.isEmpty()) {
        Fail(QStringLiteral("串口名称为空"));
        return;
    }
    QString error;
    if (!session_.Start(config_.logDirectory, error)) {
        Fail(error);
        return;
    }

    port_ = new QSerialPort(this);
    port_->setPortName(config_.portName);
    if (!port_->open(QIODevice::ReadOnly)
        || !port_->setBaudRate(config_.baudRate)
        || !port_->setDataBits(config_.dataBits)
        || !port_->setParity(config_.parity)
        || !port_->setStopBits(config_.stopBits)
        || !port_->setFlowControl(config_.flowControl)) {
        Fail(QStringLiteral("串口打开或配置失败：%1").arg(port_->errorString()));
        return;
    }
    connect(port_, &QSerialPort::readyRead, this, &SerialCaptureWorker::ReadAvailable);
    connect(port_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError errorCode) {
        if (errorCode != QSerialPort::NoError && running_) {
            Fail(QStringLiteral("串口错误：%1").arg(port_->errorString()));
        }
    });
    displayTimer_ = new QTimer(this);
    displayTimer_->setInterval(100);
    connect(displayTimer_, &QTimer::timeout, this, [this]() {
        EmitPending();
        if (session_.FrameCount() == 0 && receivedBytes_ > 0 && ++noFrameStatusTicks_ >= 10) {
            noFrameStatusTicks_ = 0;
            emit Status(QStringLiteral("已收到 %1 字节，尚无有效帧；请检查串口参数、帧头和校验")
                            .arg(receivedBytes_));
        }
    });
    displayTimer_->start();
    inactivityTimer_ = new QTimer(this);
    inactivityTimer_->setSingleShot(true);
    connect(inactivityTimer_, &QTimer::timeout, this, [this]() {
        emit Status(QStringLiteral("串口 %1 超过 %2 ms 未收到数据")
                        .arg(config_.portName).arg(config_.inactivityTimeoutMs));
    });
    if (config_.inactivityTimeoutMs > 0) inactivityTimer_->start(config_.inactivityTimeoutMs);
    running_ = true;
    emit Started(session_.RawPath(), session_.CsvPath());
}

void SerialCaptureWorker::ReadAvailable() {
    if (!running_ || !port_) return;
    const QByteArray bytes = port_->readAll();
    if (bytes.isEmpty()) return;
    receivedBytes_ += static_cast<quint64>(bytes.size());
    if (inactivityTimer_ && config_.inactivityTimeoutMs > 0) {
        inactivityTimer_->start(config_.inactivityTimeoutMs);
    }
    QVector<LiveSample> samples;
    QString error;
    if (!session_.Feed(bytes, samples, error)) {
        Fail(error);
        return;
    }
    constexpr qsizetype kMaxPendingSamples = 1024;
    const qsizetype available = kMaxPendingSamples - pendingSamples_.size();
    for (qsizetype i = 0; i < samples.size() && i < available; ++i) {
        pendingSamples_.append(std::move(samples[i]));
    }
    if (samples.size() > available) {
        emit Status(QStringLiteral("绘图队列已满，跳过显示样本；日志仍在记录"));
    }
}

void SerialCaptureWorker::EmitPending() {
    if (pendingSamples_.isEmpty()) return;
    emit SamplesReady(pendingSamples_, session_.SkippedBytes());
    pendingSamples_.clear();
}

void SerialCaptureWorker::Fail(const QString& message) {
    emit Error(message);
    Stop();
}

void SerialCaptureWorker::Stop() {
    if (stopping_) return;
    stopping_ = true;
    if (running_ && port_ && port_->bytesAvailable() > 0) ReadAvailable();
    running_ = false;
    if (displayTimer_) displayTimer_->stop();
    if (inactivityTimer_) inactivityTimer_->stop();
    if (port_) {
        port_->close();
        port_->deleteLater();
        port_ = nullptr;
    }
    EmitPending();
    QString error;
    if (!session_.Stop(error)) emit Error(error);
    emit Stopped();
}

}  // namespace pat
