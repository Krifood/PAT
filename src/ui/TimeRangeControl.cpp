#include "ui/TimeRangeControl.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

class TimeRangeBar : public QWidget {
public:
    explicit TimeRangeBar(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("timeRangeBar"));
        setFixedHeight(50);
        setMinimumWidth(180);
        setMouseTracking(true);
        setToolTip(tr("尺子 0–100% 对应完整时间范围，实际时间见上方输入框。悬停已选色带联动时间游标；拖动端点选区，拖动中间平移，释放后更新数据"));
        setAccessibleName(tr("时间区间滑块"));
        setAccessibleDescription(tr("0 到 100% 的完整时间范围参考尺"));
    }
    double start = 0.0;
    double end = 1.0;
    std::function<void(double, double, bool)> changed;
    std::function<void(double)> hovered;
    void ClearHover() {
        hover_ = -1;
        if (hovered) hovered(-1);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal left = Pixel(start), right = Pixel(end);
        QFont rulerFont = font();
        rulerFont.setPointSizeF(9);
        p.setFont(rulerFont);
        p.setPen(QPen(palette().color(QPalette::Text), 1));
        const int labelStep = width() >= 500 ? 10 : 20;
        const qreal baseline = height() - 24;
        p.drawLine(QPointF(Pixel(0), baseline), QPointF(Pixel(1), baseline));
        for (int percent = 0; percent <= 100; percent += 5) {
            const qreal x = Pixel(percent / 100.0);
            const bool major = percent % labelStep == 0;
            p.drawLine(QPointF(x, baseline - (major ? 5 : 2)), QPointF(x, baseline));
            if (major) {
                const QString label = QString::number(percent) + (percent == 0 || percent == 100 ? "%" : "");
                const qreal labelWidth = p.fontMetrics().horizontalAdvance(label) + 4;
                const qreal labelLeft = std::clamp(x - labelWidth / 2, 0.0, std::max(0.0, width() - labelWidth));
                p.drawText(QRectF(labelLeft, 0, labelWidth, baseline - 6), Qt::AlignHCenter | Qt::AlignBottom, label);
            }
        }
        const qreal trackCenter = height() - 12;
        const QRectF track(10, trackCenter - 4, width() - 20, 8);
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::Mid));
        p.drawRoundedRect(track, 4, 4);
        p.setBrush(palette().color(isEnabled() ? QPalette::Highlight : QPalette::Mid));
        p.drawRoundedRect(QRectF(left, track.top(), right - left, track.height()), 4, 4);
        p.setPen(QPen(palette().color(QPalette::Text), 1));
        p.setBrush(palette().color(QPalette::Button));
        for (qreal x : {left, right}) {
            p.drawRoundedRect(QRectF(x - 5, trackCenter - 9, 10, 18), 3, 3);
        }
        if (hover_ >= 0) {
            p.setPen(QPen(Qt::black, 3));
            p.drawLine(QPointF(Pixel(hover_), baseline - 5), QPointF(Pixel(hover_), height()-2));
            p.setPen(QPen(Qt::yellow, 1));
            p.drawLine(QPointF(Pixel(hover_), baseline - 5), QPointF(Pixel(hover_), height()-2));
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        ClearHover();
        const double x = event->position().x();
        const double left = Pixel(start), right = Pixel(end);
        if (std::min(std::abs(x - left), std::abs(x - right)) <= 8) {
            drag_ = std::abs(x - left) < std::abs(x - right) ? 1 : 2;
        } else {
            drag_ = x > left && x < right ? 3 : (x <= left ? 1 : 2);
        }
        anchor_ = Fraction(x);
        initialStart_ = start;
        initialEnd_ = end;
        Move(x, false);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (drag_) Move(event->position().x(), false);
        else setCursor(event->position().x() > Pixel(start) + 8
                       && event->position().x() < Pixel(end) - 8
                           ? Qt::SizeAllCursor : Qt::SizeHorCursor);
        if (!drag_) {
            const double x = event->position().x();
            hover_ = x >= Pixel(start) && x <= Pixel(end) ? Fraction(x) : -1;
            if (hovered) hovered(hover_);
            update();
        }
    }
    void leaveEvent(QEvent*) override { ClearHover(); }
    void hideEvent(QHideEvent*) override { ClearHover(); }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !drag_) return;
        Move(event->position().x(), true);
        drag_ = 0;
    }
private:
    qreal Pixel(double fraction) const { return 10 + fraction * std::max(1, width() - 20); }
    double Fraction(double x) const {
        return std::clamp((x - 10) / std::max(1, width() - 20), 0.0, 1.0);
    }
    void Move(double x, bool commit) {
        const double value = Fraction(x);
        constexpr double minimumSpan = 1e-9;
        if (drag_ == 1) start = std::clamp(value, 0.0, std::max(0.0, end - minimumSpan));
        if (drag_ == 2) end = std::clamp(value, std::min(1.0, start + minimumSpan), 1.0);
        if (drag_ == 3) {
            const double delta = std::clamp(value - anchor_, -initialStart_, 1 - initialEnd_);
            start = initialStart_ + delta;
            end = initialEnd_ + delta;
        }
        update();
        if (changed) changed(start, end, commit);
    }
    int drag_ = 0;
    double hover_ = -1;
    double anchor_ = 0, initialStart_ = 0, initialEnd_ = 1;
};

TimeRangeControl::TimeRangeControl(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("timeRangeControl"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(1);
    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    caption_ = new QLabel(tr("显示区间"), this);
    row->addWidget(caption_);
    start_ = new QDoubleSpinBox(this);
    end_ = new QDoubleSpinBox(this);
    start_->setObjectName(QStringLiteral("timeRangeStart"));
    end_->setObjectName(QStringLiteral("timeRangeEnd"));
    for (auto* spin : {start_, end_}) {
        spin->setDecimals(9);
        spin->setKeyboardTracking(false);
        spin->setMinimumWidth(115);
        spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    start_->setAccessibleName(tr("区间起点"));
    end_->setAccessibleName(tr("区间终点"));
    row->addWidget(start_);
    row->addWidget(new QLabel(tr("至"), this));
    row->addWidget(end_);
    auto* previous = new QPushButton(tr("◀"), this);
    auto* next = new QPushButton(tr("▶"), this);
    previous->setObjectName(QStringLiteral("timeRangePrevious"));
    next->setObjectName(QStringLiteral("timeRangeNext"));
    previous->setToolTip(tr("向前移动一个区间"));
    next->setToolTip(tr("向后移动一个区间"));
    for (auto* button : {previous, next}) {
        button->setMaximumWidth(32);
        row->addWidget(button);
    }
    auto* reset = new QPushButton(tr("全范围"), this);
    reset->setObjectName(QStringLiteral("timeRangeReset"));
    row->addWidget(reset);
    layout->addLayout(row);
    bar_ = new TimeRangeBar(this);
    bar_->hovered = [this](double fraction) {
        if (fraction >= 0 && isEnabled()) emit CursorMoved(fraction * maximum_);
        else emit CursorLeft();
    };
    layout->addWidget(bar_);
    connect(start_, &QDoubleSpinBox::editingFinished, this, [this] {
        Commit(start_->value(), std::max(start_->value(), end_->value()));
    });
    connect(end_, &QDoubleSpinBox::editingFinished, this, [this] {
        Commit(std::min(start_->value(), end_->value()), end_->value());
    });
    connect(previous, &QPushButton::clicked, this, [this] {
        const double span = end_->value() - start_->value();
        const double start = std::max(0.0, start_->value() - span);
        Commit(start, start + span);
    });
    connect(next, &QPushButton::clicked, this, [this] {
        const double span = end_->value() - start_->value();
        const double end = std::min(maximum_, end_->value() + span);
        Commit(end - span, end);
    });
    connect(reset, &QPushButton::clicked, this, [this] { Commit(0.0, maximum_); });
    bar_->changed = [this](double start, double end, bool commit) {
        SetSelection(start * maximum_, end * maximum_);
        if (commit) Commit(start_->value(), end_->value());
    };
    SetTheme(ThemeFor(AppThemeId::IndoorDark));
    setEnabled(false);
}

void TimeRangeControl::SetTheme(const AppTheme& theme) {
    QPalette colors = bar_->palette();
    colors.setColor(QPalette::Text, theme.text);
    colors.setColor(QPalette::Mid, theme.border);
    colors.setColor(QPalette::Button, theme.buttonBackground);
    colors.setColor(QPalette::Highlight, theme.seriesColors.value(0, theme.selectionBackground));
    bar_->setPalette(colors);
    bar_->update();
}

void TimeRangeControl::SetExtent(double maximum, const QString& unit, bool available) {
    bar_->ClearHover();
    maximum_ = std::isfinite(maximum) ? std::max(0.0, maximum) : 0.0;
    const QSignalBlocker blockStart(start_), blockEnd(end_);
    for (auto* spin : {start_, end_}) {
        spin->setRange(0.0, maximum_);
        spin->setSingleStep(std::max(1e-9, maximum_ / 1000.0));
    }
    caption_->setText(tr("显示区间 (%1)").arg(unit));
    setEnabled(available && maximum_ > 0.0);
    setToolTip(tr("时序图和二维/三维图共用此区间；轨迹按 X 信号的时间筛选。实时采集时不可用。"));
}

void TimeRangeControl::SetSelection(double start, double end) {
    bar_->ClearHover();
    const QSignalBlocker blockStart(start_), blockEnd(end_);
    start_->setValue(std::clamp(start, 0.0, maximum_));
    end_->setValue(std::clamp(end, start_->value(), maximum_));
    bar_->start = maximum_ > 0 ? start_->value() / maximum_ : 0.0;
    bar_->end = maximum_ > 0 ? end_->value() / maximum_ : 1.0;
    bar_->update();
}

void TimeRangeControl::Commit(double start, double end) {
    if (end <= start && maximum_ > 0.0) {
        const double step = std::max(1e-9, maximum_ * 1e-9);
        end = std::min(maximum_, start + step);
        start = std::max(0.0, end - step);
    }
    SetSelection(start, end);
    if (end_->value() > start_->value()) emit RangeSelected(start_->value(), end_->value());
}
