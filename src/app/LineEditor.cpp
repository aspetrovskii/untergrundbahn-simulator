#include "LineEditor.h"

#include "api/Functions.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace ubahn::app {
namespace {

QSpinBox* makeSpinBox(int minimum, int maximum, QWidget* parent) {
    auto* input = new QSpinBox(parent);
    input->setRange(minimum, maximum);
    return input;
}

} // namespace

LineEditor::LineEditor(QWidget* parent) : QWidget(parent) {
    setObjectName("lineEditor");
    auto* layout = new QVBoxLayout(this);

    auto* stationsGroup = new QGroupBox(tr("Станции и перегоны"), this);
    auto* stationsLayout = new QVBoxLayout(stationsGroup);
    stationCount_ = makeSpinBox(7, 20, stationsGroup);
    stationCount_->setObjectName("stationCountSpin");
    auto* countForm = new QFormLayout;
    countForm->addRow(tr("Количество станций:"), stationCount_);
    stationsLayout->addLayout(countForm);

    stations_ = new QTableWidget(stationsGroup);
    stations_->setObjectName("stationsTable");
    stations_->setColumnCount(3);
    stations_->setHorizontalHeaderLabels(
        {tr("Название станции"), tr("До следующей, м"), tr("Время хода, с")});
    stations_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    stations_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    stations_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    stations_->verticalHeader()->setVisible(false);
    stations_->setMinimumHeight(220);
    stationsLayout->addWidget(stations_);
    layout->addWidget(stationsGroup);

    auto* limitsGroup = new QGroupBox(tr("Ограничения линии"), this);
    auto* limits = new QFormLayout(limitsGroup);
    fleetSize_ = makeSpinBox(2, 200, limitsGroup);
    fleetSize_->setObjectName("fleetSizeSpin");
    dwellMin_ = makeSpinBox(30, 180, limitsGroup);
    headwayMin_ = makeSpinBox(30, 300, limitsGroup);
    clearTime_ = makeSpinBox(10, 120, limitsGroup);
    slowFactor_ = new QDoubleSpinBox(limitsGroup);
    slowFactor_->setRange(1.0, 2.0);
    slowFactor_->setSingleStep(0.1);
    slowFactor_->setDecimals(1);
    limits->addRow(tr("Размер парка:"), fleetSize_);
    limits->addRow(tr("Минимальная стоянка, с:"), dwellMin_);
    limits->addRow(tr("Минимальный интервал, с:"), headwayMin_);
    limits->addRow(tr("Освобождение платформы, с:"), clearTime_);
    limits->addRow(tr("Коэффициент замедления:"), slowFactor_);
    layout->addWidget(limitsGroup);

    auto* serviceGroup = new QGroupBox(tr("Оборот, депо и сигнал"), this);
    auto* service = new QFormLayout(serviceGroup);
    toDeadEnd_ = makeSpinBox(10, 600, serviceGroup);
    cabChange_ = makeSpinBox(10, 600, serviceGroup);
    fromDeadEnd_ = makeSpinBox(10, 600, serviceGroup);
    depotOut_ = makeSpinBox(10, 600, serviceGroup);
    depotIn_ = makeSpinBox(10, 600, serviceGroup);
    signalOffset_ = new QDoubleSpinBox(serviceGroup);
    signalOffset_->setRange(50.0, 300.0);
    signalOffset_->setDecimals(0);
    signalApproach_ = makeSpinBox(10, 60, serviceGroup);
    service->addRow(tr("Платформа → тупик, с:"), toDeadEnd_);
    service->addRow(tr("Смена кабины, с:"), cabChange_);
    service->addRow(tr("Тупик → платформа, с:"), fromDeadEnd_);
    service->addRow(tr("Выход из депо, с:"), depotOut_);
    service->addRow(tr("Уход в депо, с:"), depotIn_);
    service->addRow(tr("Сигнал перед платформой, м:"), signalOffset_);
    service->addRow(tr("Подход от сигнала, с:"), signalApproach_);
    layout->addWidget(serviceGroup);

    connect(stationCount_, &QSpinBox::valueChanged, this, &LineEditor::resizeStationTable);
    resizeStationTable(8);
}

void LineEditor::setLine(const api::LineConfig& line) {
    const QSignalBlocker blocker(stationCount_);
    stationCount_->setValue(static_cast<int>(line.stationNames.size()));
    stations_->clearContents();
    stations_->setRowCount(static_cast<int>(line.stationNames.size()));
    for (std::size_t row = 0; row < line.stationNames.size(); ++row) {
        setStationCell(static_cast<int>(row), 0, QString::fromStdString(line.stationNames[row]));
        if (row < line.spans.size()) {
            setStationCell(static_cast<int>(row), 1,
                           QString::number(line.spans[row].lengthM, 'f', 0));
            const int32_t runTime =
                line.spans[row].tStd.value_or(api::defaultStdRunTime(line.spans[row].lengthM));
            setStationCell(static_cast<int>(row), 2, QString::number(runTime));
        } else {
            setStationCell(static_cast<int>(row), 1, tr("—"), false);
            setStationCell(static_cast<int>(row), 2, tr("—"), false);
        }
    }

    fleetSize_->setValue(line.fleetSize);
    dwellMin_->setValue(line.dwellMin);
    headwayMin_->setValue(line.headwayMin);
    clearTime_->setValue(line.tClear);
    slowFactor_->setValue(line.kSlow);
    toDeadEnd_->setValue(line.toDeadEnd);
    cabChange_->setValue(line.cabChange);
    fromDeadEnd_->setValue(line.fromDeadEnd);
    depotOut_->setValue(line.depotOut);
    depotIn_->setValue(line.depotIn);
    signalOffset_->setValue(line.signalStopOffsetM);
    signalApproach_->setValue(line.signalApproach);
}

api::LineConfig LineEditor::line() const {
    api::LineConfig result;
    for (int row = 0; row < stations_->rowCount(); ++row) {
        result.stationNames.push_back(stations_->item(row, 0)->text().toStdString());
        if (row + 1 < stations_->rowCount()) {
            result.spans.push_back({stations_->item(row, 1)->text().toDouble(),
                                    stations_->item(row, 2)->text().toInt()});
        }
    }
    result.fleetSize = fleetSize_->value();
    result.dwellMin = dwellMin_->value();
    result.headwayMin = headwayMin_->value();
    result.tClear = clearTime_->value();
    result.kSlow = slowFactor_->value();
    result.toDeadEnd = toDeadEnd_->value();
    result.cabChange = cabChange_->value();
    result.fromDeadEnd = fromDeadEnd_->value();
    result.depotOut = depotOut_->value();
    result.depotIn = depotIn_->value();
    result.signalStopOffsetM = signalOffset_->value();
    result.signalApproach = signalApproach_->value();
    return result;
}

std::vector<std::string> LineEditor::inputErrors() const {
    std::vector<std::string> errors;
    for (int row = 0; row < stations_->rowCount(); ++row) {
        if (stations_->item(row, 0)->text().trimmed().isEmpty()) {
            errors.push_back("У станции " + std::to_string(row + 1) + " нет названия");
        }
        if (row + 1 < stations_->rowCount()) {
            bool lengthOk = false;
            bool timeOk = false;
            const double length = stations_->item(row, 1)->text().toDouble(&lengthOk);
            const int runTime = stations_->item(row, 2)->text().toInt(&timeOk);
            if (!lengthOk || length < 400.0 || length > 5000.0) {
                errors.push_back("Длина перегона " + std::to_string(row + 1) +
                                 " должна быть от 400 до 5000 м");
            }
            if (!timeOk || runTime <= 0) {
                errors.push_back("Время хода по перегону " + std::to_string(row + 1) +
                                 " должно быть положительным числом");
            }
        }
    }
    return errors;
}

void LineEditor::resizeStationTable(int stationCount) {
    const int previousCount = stations_->rowCount();
    stations_->setRowCount(stationCount);
    for (int row = 0; row < stationCount; ++row) {
        if (row >= previousCount || stations_->item(row, 0) == nullptr) {
            setStationCell(row, 0, tr("Станция %1").arg(row + 1));
        }
        if (row + 1 < stationCount) {
            if (stations_->item(row, 1) == nullptr || row + 1 == previousCount) {
                setStationCell(row, 1, QStringLiteral("1000"));
                setStationCell(row, 2, QStringLiteral("100"));
            } else {
                stations_->item(row, 1)->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                                                  Qt::ItemIsEditable);
                stations_->item(row, 2)->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                                                  Qt::ItemIsEditable);
            }
        } else {
            setStationCell(row, 1, tr("—"), false);
            setStationCell(row, 2, tr("—"), false);
        }
    }
}

void LineEditor::setStationCell(int row, int column, const QString& text, bool editable) {
    auto* item = new QTableWidgetItem(text);
    Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (editable) {
        flags |= Qt::ItemIsEditable;
    }
    item->setFlags(flags);
    stations_->setItem(row, column, item);
}

} // namespace ubahn::app
