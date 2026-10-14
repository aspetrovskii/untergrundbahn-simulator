#include "PlaybackPanel.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <array>
#include <cmath>

namespace ubahn::app {
namespace {

QString timeText(int32_t seconds) {
    const int32_t hours = seconds / 3600;
    const int32_t minutes = seconds % 3600 / 60;
    const int32_t remainingSeconds = seconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(remainingSeconds, 2, 10, QLatin1Char('0'));
}

QString shortTimeText(int32_t seconds) {
    const int32_t hours = seconds / 3600;
    const int32_t minutes = seconds % 3600 / 60;
    return QStringLiteral("%1:%2")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'));
}

int dayIndex(api::SimTime modelTime) {
    constexpr api::SimTime day = api::kSecondsPerDay;
    const api::SimTime index = static_cast<api::SimTime>(
        std::floor(static_cast<double>(modelTime) / static_cast<double>(day)));
    return static_cast<int>((index % 7 + 7) % 7);
}

int32_t timeOfDay(api::SimTime modelTime) {
    constexpr api::SimTime day = api::kSecondsPerDay;
    return static_cast<int32_t>((modelTime % day + day) % day);
}

} // namespace

PlaybackPanel::PlaybackPanel(QWidget* parent) : QFrame(parent) {
    setObjectName("playbackPanel");
    setFrameShape(QFrame::StyledPanel);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(6);

    clockLabel_ = new QLabel(tr("Модельное время: —"), this);
    clockLabel_->setObjectName("modelClockLabel");
    QFont clockFont = clockLabel_->font();
    clockFont.setBold(true);
    clockLabel_->setFont(clockFont);
    layout->addWidget(clockLabel_);

    windowLabel_ = new QLabel(tr("Текущее окно: —"), this);
    windowLabel_->setObjectName("currentWindowLabel");
    windowLabel_->setWordWrap(true);
    layout->addWidget(windowLabel_);

    auto* controls = new QHBoxLayout;
    pauseButton_ = new QPushButton(tr("Пауза"), this);
    pauseButton_->setObjectName("pauseButton");
    controls->addWidget(pauseButton_);

    controls->addWidget(new QLabel(tr("Темп:"), this));
    rateCombo_ = new QComboBox(this);
    rateCombo_->setObjectName("rateCombo");
    rateCombo_->addItem(tr("0,5×"), 0.5);
    rateCombo_->addItem(tr("1×"), 1.0);
    rateCombo_->addItem(tr("2×"), 2.0);
    rateCombo_->addItem(tr("5×"), 5.0);
    controls->addWidget(rateCombo_);

    controls->addWidget(new QLabel(tr("Шаг:"), this));
    stepCombo_ = new QComboBox(this);
    stepCombo_->setObjectName("stepCombo");
    for (const int step : {15, 30, 60, 120, 240}) {
        stepCombo_->addItem(tr("%1 с").arg(step), step);
    }
    controls->addWidget(stepCombo_);

    controls->addWidget(new QLabel(tr("Вперёд:"), this));
    seekCombo_ = new QComboBox(this);
    seekCombo_->setObjectName("seekCombo");
    seekCombo_->addItem(tr("1 мин"), 60);
    seekCombo_->addItem(tr("5 мин"), 5 * 60);
    seekCombo_->addItem(tr("15 мин"), 15 * 60);
    seekCombo_->addItem(tr("1 ч"), 60 * 60);
    controls->addWidget(seekCombo_);

    seekButton_ = new QPushButton(tr("Перемотать"), this);
    seekButton_->setObjectName("seekButton");
    controls->addWidget(seekButton_);

    stopButton_ = new QPushButton(tr("Остановить"), this);
    stopButton_->setObjectName("stopButton");
    controls->addWidget(stopButton_);
    controls->addStretch();
    layout->addLayout(controls);

    connect(pauseButton_, &QPushButton::clicked, this, &PlaybackPanel::togglePause);
    connect(rateCombo_, &QComboBox::currentIndexChanged, this, &PlaybackPanel::selectRate);
    connect(stepCombo_, &QComboBox::currentIndexChanged, this,
            [this] { emit stepChanged(stepCombo_->currentData().toInt()); });
    connect(seekButton_, &QPushButton::clicked, this, &PlaybackPanel::requestSeek);
    connect(stopButton_, &QPushButton::clicked, this, &PlaybackPanel::requestStop);

    setInitialValues(1.0, 60);
}

void PlaybackPanel::setFinished(bool finished) {
    pauseButton_->setEnabled(!finished);
    rateCombo_->setEnabled(!finished);
    stepCombo_->setEnabled(!finished);
    seekCombo_->setEnabled(!finished);
    seekButton_->setEnabled(!finished);
    stopButton_->setEnabled(!finished);
}

void PlaybackPanel::setInitialValues(double rate, int stepSec) {
    const QSignalBlocker rateBlocker(rateCombo_);
    const QSignalBlocker stepBlocker(stepCombo_);

    const int rateIndex = rateCombo_->findData(rate);
    if (rateIndex >= 0) {
        rateCombo_->setCurrentIndex(rateIndex);
    }
    const int stepIndex = stepCombo_->findData(stepSec);
    if (stepIndex >= 0) {
        stepCombo_->setCurrentIndex(stepIndex);
    }

    rate_ = rate;
    if (rate > 0.0) {
        resumeRate_ = rate;
        pauseButton_->setText(tr("Пауза"));
    } else {
        pauseButton_->setText(tr("Продолжить"));
    }
}

void PlaybackPanel::setModelState(api::SimTime modelTime,
                                  const std::optional<api::ClockUpdate>& clock) {
    modelTime_ = modelTime;
    static const std::array<const char*, 7> days = {
        "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота", "Воскресенье"};
    clockLabel_->setText(
        tr("%1, %2").arg(tr(days[dayIndex(modelTime)]), timeText(timeOfDay(modelTime))));
    updateWindowText(clock);
}

void PlaybackPanel::togglePause() {
    if (rate_ > 0.0) {
        resumeRate_ = rate_;
        rate_ = 0.0;
        pauseButton_->setText(tr("Продолжить"));
    } else {
        rate_ = resumeRate_;
        pauseButton_->setText(tr("Пауза"));
    }
    emit rateChanged(rate_);
}

void PlaybackPanel::selectRate() {
    rate_ = rateCombo_->currentData().toDouble();
    resumeRate_ = rate_;
    pauseButton_->setText(tr("Пауза"));
    emit rateChanged(rate_);
}

void PlaybackPanel::requestSeek() {
    emit seekForwardRequested(modelTime_ + seekCombo_->currentData().toInt());
}

void PlaybackPanel::requestStop() {
    if (rate_ > 0.0) {
        resumeRate_ = rate_;
        rate_ = 0.0;
        pauseButton_->setText(tr("Продолжить"));
        emit rateChanged(0.0);
    }
    stopButton_->setEnabled(false);
    emit stopRequested();
}

void PlaybackPanel::updateWindowText(const std::optional<api::ClockUpdate>& clock) {
    if (!clock.has_value()) {
        windowLabel_->setText(tr("Текущее окно: —"));
        return;
    }
    if (!clock->open) {
        windowLabel_->setText(tr("Пассажирское движение закрыто"));
        return;
    }

    const api::Window& window = clock->window;
    windowLabel_->setText(
        tr("Окно %1 · %2–%3 · интервал %4 с (эффективный %5 с) · стоянка %6 с · задержка %7%")
            .arg(clock->windowIndex + 1)
            .arg(shortTimeText(window.fromTod))
            .arg(shortTimeText(window.toTod))
            .arg(window.headway)
            .arg(clock->hEff)
            .arg(window.dwell)
            .arg(std::lround(window.delayProb * 100.0)));
}

} // namespace ubahn::app
