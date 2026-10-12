#pragma once

#include "api/Types.h"

#include <QDialog>
#include <vector>

class QComboBox;

namespace ubahn::app {

class SetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit SetupDialog(QWidget* parent = nullptr);

    api::SimParams selectedParams() const;

private:
    std::vector<api::Preset> presets_;
    QComboBox* presetCombo_ = nullptr;
};

} // namespace ubahn::app
