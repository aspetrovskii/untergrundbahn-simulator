#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;

namespace ubahn::app {

class RuntimeErrorBanner : public QFrame {
    Q_OBJECT

public:
    explicit RuntimeErrorBanner(QWidget* parent = nullptr);

    void showError(const QString& title, const QString& details, bool canRestart);
    void clear();

signals:
    void restartRequested();

private:
    QLabel* message_ = nullptr;
    QPushButton* restartButton_ = nullptr;
};

} // namespace ubahn::app
