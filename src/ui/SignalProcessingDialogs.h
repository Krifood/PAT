#pragma once

#include "core/FormatDefinition.h"
#include "core/SeriesProcessing.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;

class SignalTransformDialog : public QDialog {
    Q_OBJECT

public:
    SignalTransformDialog(const QString& signalName,
                          const pat::DisplayTransform& initial,
                          QWidget* parent = nullptr);

    pat::DisplayTransform Transform() const;

private:
    QDoubleSpinBox* scaleSpin_ = nullptr;
    QDoubleSpinBox* offsetSpin_ = nullptr;
};

class DifferenceDialog : public QDialog {
    Q_OBJECT

public:
    DifferenceDialog(const pat::FormatDefinition& format,
                     int initialLeft,
                     int initialRight,
                     QWidget* parent = nullptr);

    int LeftSignal() const;
    int RightSignal() const;
    pat::SeriesOperation Operation() const;

private:
    QComboBox* operationCombo_ = nullptr;
    QComboBox* leftCombo_ = nullptr;
    QComboBox* rightCombo_ = nullptr;
};
