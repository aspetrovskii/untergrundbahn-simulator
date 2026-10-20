#include "RuntimeErrorBanner.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace ubahn::app {

RuntimeErrorBanner::RuntimeErrorBanner(QWidget* parent) : QFrame(parent) {
    setObjectName("runtimeErrorBanner");
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral("QFrame#runtimeErrorBanner { background: #fde7e9; "
                                 "border: 1px solid #b00020; padding: 6px; }"));

    auto* layout = new QHBoxLayout(this);
    message_ = new QLabel(this);
    message_->setObjectName("runtimeErrorMessage");
    message_->setWordWrap(true);
    layout->addWidget(message_, 1);

    restartButton_ = new QPushButton(tr("Перезапустить"), this);
    restartButton_->setObjectName("restartSimulationButton");
    layout->addWidget(restartButton_);
    connect(restartButton_, &QPushButton::clicked, this, &RuntimeErrorBanner::restartRequested);

    hide();
}

void RuntimeErrorBanner::showError(const QString& title, const QString& details, bool canRestart) {
    QString text = title;
    if (!details.isEmpty()) {
        text += QStringLiteral("\n") + details;
    }
    message_->setText(text);
    restartButton_->setVisible(canRestart);
    show();
}

void RuntimeErrorBanner::clear() {
    message_->clear();
    hide();
}

} // namespace ubahn::app
