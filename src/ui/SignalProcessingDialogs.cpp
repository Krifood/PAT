#include "ui/SignalProcessingDialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

SignalTransformDialog::SignalTransformDialog(const QString& signalName,
                                             const pat::DisplayTransform& initial,
                                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("显示比例与平移 — %1").arg(signalName));
    setMinimumWidth(430);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("显示值 = 解析值 × 显示比例/LSB 系数 + 平移量\n"
           "此设置只影响曲线显示，不修改解析值、差值源数据或日志。"), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout;
    scaleSpin_ = new QDoubleSpinBox(this);
    scaleSpin_->setObjectName(QStringLiteral("displayScaleSpin"));
    scaleSpin_->setDecimals(12);
    scaleSpin_->setRange(-1e12, 1e12);
    scaleSpin_->setValue(initial.scale);
    scaleSpin_->setSingleStep(0.1);
    offsetSpin_ = new QDoubleSpinBox(this);
    offsetSpin_->setObjectName(QStringLiteral("displayOffsetSpin"));
    offsetSpin_->setDecimals(12);
    offsetSpin_->setRange(-1e12, 1e12);
    offsetSpin_->setValue(initial.offset);
    offsetSpin_->setSingleStep(1.0);
    form->addRow(tr("显示比例 / LSB 系数"), scaleSpin_);
    form->addRow(tr("纵向平移量"), offsetSpin_);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                          | QDialogButtonBox::Reset, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, [this]() {
        scaleSpin_->setValue(1.0);
        offsetSpin_->setValue(0.0);
    });
    layout->addWidget(buttons);
}

pat::DisplayTransform SignalTransformDialog::Transform() const {
    return {scaleSpin_->value(), offsetSpin_->value()};
}

DifferenceDialog::DifferenceDialog(const pat::FormatDefinition& format,
                                   int initialLeft,
                                   int initialRight,
                                   QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("创建差值图"));
    setMinimumWidth(430);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("生成独立子图：被减信号 A − 减数信号 B。\n"
           "只有相同采样位置且单位相同的有效数据参与计算。"), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout;
    leftCombo_ = new QComboBox(this);
    leftCombo_->setObjectName(QStringLiteral("differenceLeftCombo"));
    rightCombo_ = new QComboBox(this);
    rightCombo_->setObjectName(QStringLiteral("differenceRightCombo"));
    for (int i = 0; i < static_cast<int>(format.signalFormats.size()); ++i) {
        const auto& signal = format.signalFormats[static_cast<size_t>(i)];
        const QString label = signal.unit.isEmpty() ? signal.name
            : tr("%1 (%2)").arg(signal.name, signal.unit);
        leftCombo_->addItem(label, i);
        rightCombo_->addItem(label, i);
    }
    leftCombo_->setCurrentIndex(std::max(0, leftCombo_->findData(initialLeft)));
    rightCombo_->setCurrentIndex(std::max(0, rightCombo_->findData(initialRight)));
    form->addRow(tr("被减信号 A"), leftCombo_);
    form->addRow(tr("减数信号 B"), rightCombo_);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int DifferenceDialog::LeftSignal() const {
    return leftCombo_->currentData().toInt();
}

int DifferenceDialog::RightSignal() const {
    return rightCombo_->currentData().toInt();
}
