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
    setWindowTitle(tr("信号属性 — %1").arg(signalName));
    setMinimumWidth(430);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("当前信号所有显示值 = 解析值 × 显示比例/LSB 系数 + 整体纵向平移量\n"
           "时序图和二维/三维空间图同步应用；不修改解析值、派生图源数据或日志。"), this);
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
    form->addRow(tr("整体纵向平移量"), offsetSpin_);
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
    setWindowTitle(tr("创建差值/和值关系图"));
    setMinimumWidth(430);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("生成没有信号树来源的独立关系子图，标题会声明运算和数据来源。\n"
           "只有相同采样位置且单位相同的有效数据参与计算。"), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout;
    operationCombo_ = new QComboBox(this);
    operationCombo_->setObjectName(QStringLiteral("derivedOperationCombo"));
    operationCombo_->addItem(tr("差值：A − B"),
                             static_cast<int>(pat::SeriesOperation::Difference));
    operationCombo_->addItem(tr("和值：A + B"),
                             static_cast<int>(pat::SeriesOperation::Sum));
    leftCombo_ = new QComboBox(this);
    leftCombo_->setObjectName(QStringLiteral("differenceLeftCombo"));
    rightCombo_ = new QComboBox(this);
    rightCombo_->setObjectName(QStringLiteral("differenceRightCombo"));
    for (int i = 0; i < static_cast<int>(format.signalFormats.size()); ++i) {
        const auto& signal = format.signalFormats[static_cast<size_t>(i)];
        const QString name = signal.QualifiedDisplayName();
        const QString label = signal.unit.isEmpty() ? name
            : tr("%1 (%2)").arg(name, signal.unit);
        leftCombo_->addItem(label, i);
        rightCombo_->addItem(label, i);
    }
    leftCombo_->setCurrentIndex(std::max(0, leftCombo_->findData(initialLeft)));
    rightCombo_->setCurrentIndex(std::max(0, rightCombo_->findData(initialRight)));
    form->addRow(tr("运算"), operationCombo_);
    form->addRow(tr("信号 A"), leftCombo_);
    form->addRow(tr("信号 B"), rightCombo_);
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

pat::SeriesOperation DifferenceDialog::Operation() const {
    return static_cast<pat::SeriesOperation>(operationCombo_->currentData().toInt());
}
