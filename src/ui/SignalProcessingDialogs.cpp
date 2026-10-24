#include "ui/SignalProcessingDialogs.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
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
    setWindowTitle(tr("信号做差 / 差值趋势"));
    setMinimumWidth(560);
    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("选择 A、B 后生成独立的差值趋势子图，可重复创建多组对比。\n"
           "使用原始解析值（不含显示比例/平移），两信号单位须一致。\n"
           "前值保持：仅在临时运算图合并两条时间轴，未更新的信号沿用上一值，保留高精度时间点。"
           "仅使用共同覆盖时间，不跨日志段；段内长间隔仍会保持，请结合采样状态判断。\n"
           "线性插值：在 A 的采样时间估算 B，只使用 B 前后有效样本，不外推、不跨日志段。"
           "有效样本间隔较大时也会插值；突变/状态信号建议选择前值保持或严格同时间。"
           "无共同时间范围时仍无有效差值。"), this);
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
    QStringList signalLabels;
    for (int i = 0; i < static_cast<int>(format.signalFormats.size()); ++i) {
        const auto& signal = format.signalFormats[static_cast<size_t>(i)];
        const QString name = signal.QualifiedDisplayName();
        const QString label = signal.unit.isEmpty() ? name
            : tr("%1 (%2)").arg(name, signal.unit);
        signalLabels.append(label);
    }
    form->addRow(tr("运算"), operationCombo_);
    alignmentCombo_ = new QComboBox(this);
    alignmentCombo_->setObjectName(QStringLiteral("differenceAlignmentCombo"));
    alignmentCombo_->addItem(tr("共同时间轴·前值保持（保留高精度时间点）"), 2);
    alignmentCombo_->addItem(tr("按 A 时间线性插值 B（适合异步连续信号）"), 1);
    alignmentCombo_->addItem(tr("严格同时间（不插值）"), 0);
    form->addRow(tr("时间对齐"), alignmentCombo_);
    auto addPicker = [this, form, signalLabels](QComboBox* combo, int initial, const QString& side) {
        auto* box = new QWidget(this);
        auto* rows = new QVBoxLayout(box);
        rows->setContentsMargins(0, 0, 0, 0);
        auto* search = new QLineEdit(box);
        search->setObjectName(QStringLiteral("difference%1Search").arg(side));
        search->setPlaceholderText(tr("搜索信号名、分组或单位"));
        search->setClearButtonEnabled(true);
        rows->addWidget(search);
        rows->addWidget(combo);
        for (int i = 0; i < signalLabels.size(); ++i) combo->addItem(signalLabels[i], i);
        combo->setCurrentIndex(std::max(0, combo->findData(initial)));
        connect(search, &QLineEdit::textChanged, combo, [combo, signalLabels](const QString& text) {
            const int previous = combo->currentIndex() < 0 ? -1 : combo->currentData().toInt();
            combo->clear();
            for (int i = 0; i < signalLabels.size(); ++i) {
                if (signalLabels[i].contains(text.trimmed(), Qt::CaseInsensitive)) combo->addItem(signalLabels[i], i);
            }
            const int restored = combo->findData(previous);
            if (restored >= 0) combo->setCurrentIndex(restored);
        });
        form->addRow(tr("信号 %1").arg(side), box);
    };
    addPicker(leftCombo_, initialLeft, QStringLiteral("A"));
    addPicker(rightCombo_, initialRight, QStringLiteral("B"));
    layout->addLayout(form);

    auto* swap = new QPushButton(tr("交换 A / B"), this);
    swap->setObjectName(QStringLiteral("differenceSwapButton"));
    layout->addWidget(swap);
    connect(swap, &QPushButton::clicked, this, [this]() {
        const int left = LeftSignal(), right = RightSignal();
        findChild<QLineEdit*>(QStringLiteral("differenceASearch"))->clear();
        findChild<QLineEdit*>(QStringLiteral("differenceBSearch"))->clear();
        leftCombo_->setCurrentIndex(leftCombo_->findData(right));
        rightCombo_->setCurrentIndex(rightCombo_->findData(left));
    });
    auto* preview = new QLabel(this);
    preview->setObjectName(QStringLiteral("differencePreview"));
    preview->setWordWrap(true);
    layout->addWidget(preview);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("创建趋势图"));
    auto validate = [this, format, preview, buttons, swap]() {
        pat::SeriesProcessing validation;
        validation.Reset(format);
        QString error;
        const bool valid = validation.AddDerived(LeftSignal(), RightSignal(), Operation(), error, Interpolate(), HoldPrevious());
        if (LeftSignal() < 0 || RightSignal() < 0) error = tr("没有匹配信号，请调整搜索条件并选择 A、B。");
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
        swap->setEnabled(LeftSignal() >= 0 && RightSignal() >= 0);
        preview->setText(valid ? tr("趋势：%1 %2 %3").arg(leftCombo_->currentText(),
            Operation() == pat::SeriesOperation::Difference ? QStringLiteral("−") : QStringLiteral("+"),
            rightCombo_->currentText()) : error);
    };
    connect(leftCombo_, &QComboBox::currentIndexChanged, this, validate);
    connect(rightCombo_, &QComboBox::currentIndexChanged, this, validate);
    connect(operationCombo_, &QComboBox::currentIndexChanged, this, validate);
    validate();
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int DifferenceDialog::LeftSignal() const {
    return leftCombo_->currentIndex() < 0 ? -1 : leftCombo_->currentData().toInt();
}

int DifferenceDialog::RightSignal() const {
    return rightCombo_->currentIndex() < 0 ? -1 : rightCombo_->currentData().toInt();
}

pat::SeriesOperation DifferenceDialog::Operation() const {
    return static_cast<pat::SeriesOperation>(operationCombo_->currentData().toInt());
}

bool DifferenceDialog::Interpolate() const {
    return alignmentCombo_->currentData().toInt() == 1;
}

bool DifferenceDialog::HoldPrevious() const {
    return alignmentCombo_->currentData().toInt() == 2;
}
