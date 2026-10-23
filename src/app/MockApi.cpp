#include "MockSamples.h"
#include "api/Functions.h"
#include "api/SimController.h"

#include <memory>
#include <string>
#include <utility>

namespace ubahn::api {
namespace {

ScheduleConfig makeDefaultSchedule() {
    ScheduleConfig schedule;
    const std::vector<Window> weekday = {
        {6 * 3600, 7 * 3600, 180, 60, 0.15},   {7 * 3600, 10 * 3600, 120, 80, 0.55},
        {10 * 3600, 17 * 3600, 160, 60, 0.25}, {17 * 3600, 20 * 3600, 120, 80, 0.50},
        {20 * 3600, 24 * 3600, 300, 40, 0.12},
    };
    const std::vector<Window> weekend = {
        {6 * 3600, 10 * 3600, 360, 40, 0.12},
        {10 * 3600, 20 * 3600, 240, 60, 0.20},
        {20 * 3600, 24 * 3600, 360, 40, 0.10},
    };
    for (int day = 0; day < 5; ++day) {
        schedule.days[day].windows = weekday;
    }
    schedule.days[5].windows = weekend;
    schedule.days[6].windows = weekend;
    return schedule;
}

LineConfig makeLine(const std::vector<std::string>& names, const std::vector<double>& lengths,
                    const std::vector<int32_t>& times, int fleetSize) {
    LineConfig line;
    line.stationNames = names;
    line.fleetSize = fleetSize;
    for (std::size_t index = 0; index < lengths.size(); ++index) {
        line.spans.push_back({lengths[index], times[index]});
    }
    return line;
}

SimParams makeParams(LineConfig line) {
    SimParams params;
    params.line = std::move(line);
    params.schedule = makeDefaultSchedule();
    params.period = {6 * 3600, 12 * 3600};
    return params;
}

Preset trainingPreset() {
    return {
        "training-8",
        "Учебная, 8 станций",
        makeParams(makeLine({"Северная", "Полевая", "Заводская", "Университет", "Центральная",
                             "Рынок", "Парк", "Южная"},
                            {1100, 780, 1700, 950, 1500, 860, 1400},
                            {120, 90, 130, 100, 110, 95, 125}, 12)),
    };
}

Preset generatedPreset(const std::string& id, const std::string& name, int stationCount,
                       int fleet) {
    std::vector<std::string> names;
    std::vector<double> lengths;
    std::vector<int32_t> times;
    for (int station = 1; station <= stationCount; ++station) {
        names.push_back("Станция " + std::to_string(station));
        if (station < stationCount) {
            lengths.push_back(1000.0);
            times.push_back(100);
        }
    }
    return {id, name, makeParams(makeLine(names, lengths, times, fleet))};
}

void addIssue(std::vector<ValidationIssue>& issues, const std::string& field, IssueCode code,
              const std::string& message) {
    issues.push_back({field, code, message});
}

class MockSimController final : public SimController {
public:
    using SimController::SimController;

    void start(const SimParams& params) override {
        emit started(app::mock::runInfo());
        for (const auto& chunk : app::mock::viewChunks(params.line)) {
            emit chunkReady(chunk);
        }
        emit summaryReady(app::mock::summary());
    }

    void setRate(double rate) override { (void)rate; }

    void setStep(int stepSec) override { (void)stepSec; }

    void seekForward(SimTime target) override { (void)target; }

    void applySchedule(const ScheduleConfig& schedule) override { (void)schedule; }

    void stop() override {}
};

} // namespace

std::vector<Preset> builtinPresets() {
    return {
        trainingPreset(),
        generatedPreset("minimal-7", "Минимальная, 7 станций", 7, 10),
        generatedPreset("long-20", "Длинная, 20 станций", 20, 30),
    };
}

SimParams defaultParams() {
    return trainingPreset().params;
}

ScheduleConfig defaultSchedule() {
    return makeDefaultSchedule();
}

int32_t physMinRunTime(double lengthM) {
    (void)lengthM;
    return 60;
}

int32_t defaultStdRunTime(double lengthM) {
    (void)lengthM;
    return 90;
}

int32_t minRunTime(const LineConfig& line, int span) {
    if (span < 0 || span >= static_cast<int>(line.spans.size())) {
        return 0;
    }
    return line.spans[static_cast<std::size_t>(span)].tStd.value_or(90);
}

int32_t maxRunTime(const LineConfig& line, int span) {
    return minRunTime(line, span);
}

std::vector<ValidationIssue> validateSchedule(const LineConfig& line,
                                              const ScheduleConfig& schedule) {
    (void)line;
    std::vector<ValidationIssue> issues;
    if (schedule.delayMin < 0 || schedule.delayMin > schedule.delayMax || schedule.delayMax > 600) {
        addIssue(issues, "schedule.delayMin", IssueCode::DelayRangeInvalid,
                 "Диапазон задержки задан неверно");
    }
    for (std::size_t day = 0; day < schedule.days.size(); ++day) {
        for (std::size_t window = 0; window < schedule.days[day].windows.size(); ++window) {
            const auto& value = schedule.days[day].windows[window];
            const std::string prefix = "schedule.days[" + std::to_string(day) + "].windows[" +
                                       std::to_string(window) + "]";
            if (value.fromTod < kOpenTod || value.toTod > kCloseTod ||
                value.fromTod >= value.toTod) {
                addIssue(issues, prefix, IssueCode::WindowOutOfHours,
                         "Окно должно находиться внутри часов работы");
            }
            if (value.delayProb < 0.0 || value.delayProb > 1.0) {
                addIssue(issues, prefix + ".delayProb", IssueCode::ProbabilityOutOfRange,
                         "Вероятность должна быть от 0 до 1");
            }
        }
    }
    return issues;
}

std::vector<ValidationIssue> validate(const SimParams& params) {
    std::vector<ValidationIssue> issues = validateSchedule(params.line, params.schedule);
    const auto stationCount = params.line.stationNames.size();
    if (stationCount < 7 || stationCount > 20) {
        addIssue(issues, "line.stationNames", IssueCode::StationCountOutOfRange,
                 "Число станций должно быть от 7 до 20");
    }
    if (stationCount == 0 || params.line.spans.size() + 1 != stationCount) {
        addIssue(issues, "line.spans", IssueCode::SpanCountMismatch,
                 "Число перегонов должно быть на один меньше числа станций");
    }
    if (params.stepSec < 1 || params.stepSec > 240) {
        addIssue(issues, "stepSec", IssueCode::StepOutOfRange, "Шаг должен быть от 1 до 240 с");
    }
    if (params.period.end <= params.period.start) {
        addIssue(issues, "period.end", IssueCode::PeriodInvalid,
                 "Конец периода должен быть позже начала");
    }
    return issues;
}

ScheduleAnalysis analyze(const LineConfig& line, const ScheduleConfig& schedule) {
    (void)line;
    ScheduleAnalysis result;
    result.tCycle = 1440;
    result.tTurn = 120;
    result.minFleetForAllWindows = 12;
    for (std::size_t day = 0; day < schedule.days.size(); ++day) {
        for (const auto& window : schedule.days[day].windows) {
            result.days[day].push_back({12, window.headway, false});
        }
    }
    return result;
}

std::unique_ptr<SimController> createSimController() {
    return std::make_unique<MockSimController>();
}

} // namespace ubahn::api
