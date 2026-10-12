#include "SetupDialog.h"

#include "api/Functions.h"
#include "api/Version.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ubahn::app {

SetupDialog::SetupDialog(QWidget* parent) : QDialog(parent), presets_(api::builtinPresets()) {
    setObjectName("setupDialog");
    setWindowTitle(tr("Настройка симуляции"));
    setMinimumWidth(460);

    auto* intro = new QLabel(
        tr("Выберите заготовку линии. Подробные параметры будут добавлены на следующих этапах."),
        this);
    intro->setWordWrap(true);

    presetCombo_ = new QComboBox(this);
    presetCombo_->setObjectName("presetCombo");
    for (const auto& preset : presets_) {
        presetCombo_->addItem(QString::fromStdString(preset.name));
    }

    auto* form = new QFormLayout;
    form->addRow(tr("Пресет:"), presetCombo_);

    auto* apiVersion = new QLabel(tr("Версия API: %1.%2.%3")
                                      .arg(api::kApiVersionMajor)
                                      .arg(api::kApiVersionMinor)
                                      .arg(api::kApiVersionPatch),
                                  this);
    apiVersion->setObjectName("apiVersionLabel");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto* startButton = buttons->addButton(tr("Открыть симуляцию"), QDialogButtonBox::AcceptRole);
    startButton->setObjectName("startButton");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(apiVersion);
    layout->addStretch();
    layout->addWidget(buttons);
}

api::SimParams SetupDialog::selectedParams() const {
    const int index = presetCombo_->currentIndex();
    if (index >= 0 && index < static_cast<int>(presets_.size())) {
        return presets_[static_cast<std::size_t>(index)].params;
    }
    return api::defaultParams();
}

} // namespace ubahn::app
