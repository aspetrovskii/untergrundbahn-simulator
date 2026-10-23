#include "api/Functions.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ubahn::api {
namespace {

constexpr int32_t kHour = 3600;

// SPEC 6.3, weekdays. The last window ends at closing (24:00).
DayProfile weekdayProfile() {
    DayProfile day;
    day.windows = {
        {6 * kHour, 7 * kHour, 180, 60, 0.15},   {7 * kHour, 10 * kHour, 120, 80, 0.55},
        {10 * kHour, 17 * kHour, 160, 60, 0.25}, {17 * kHour, 20 * kHour, 120, 80, 0.50},
        {20 * kHour, kCloseTod, 300, 40, 0.12},
    };
    return day;
}

// SPEC 6.3, Saturday and Sunday. The profiles are equal until the user edits them.
DayProfile weekendProfile() {
    DayProfile day;
    day.windows = {
        {6 * kHour, 10 * kHour, 360, 40, 0.12},
        {10 * kHour, 20 * kHour, 240, 60, 0.20},
        {20 * kHour, kCloseTod, 360, 40, 0.10},
    };
    return day;
}

struct SpanSpec {
    double lengthM;
    int32_t tStd;
};

// Explicit tStd on every span: a scheduled speed of 50..70 km/h with a few seconds over the
// physical minimum, so tMin is physical and catch-up runs at up to 80 km/h (SPEC C1, C3).
// Turnaround, depot, signal and k_slow stay at the LineConfig defaults.
LineConfig lineOf(std::vector<std::string> names, std::initializer_list<SpanSpec> spans) {
    LineConfig line;
    line.stationNames = std::move(names);
    line.spans.reserve(spans.size());
    for (const SpanSpec& spec : spans) {
        SpanInput span;
        span.lengthM = spec.lengthM;
        span.tStd = spec.tStd;
        line.spans.push_back(span);
    }
    line.dwellMin = 30;
    line.headwayMin = 90;
    return line;
}

// SPEC 12. Spans of 1.0..2.4 km at 51..63 km/h. The 712 s one-way running time keeps the
// no-delay weekend on time at 20:00 (SPEC 14.1); 718..722 s misses a depot handover there.
LineConfig trainingLine() {
    return lineOf(
        {"Сосновая", "Липовая", "Мельничная", "Колокольная", "Торговая", "Ярмарочная", "Овражная",
         "Тополиная"},
        {{1450, 90}, {1820, 108}, {1300, 83}, {2150, 124}, {1600, 99}, {1040, 73}, {2380, 135}});
}

// SPEC 12, lower bound of N.
LineConfig shortLine() {
    return lineOf({"Маячная", "Рыбачья", "Гончарная", "Кирпичная"},
                  {{1700, 104}, {2260, 130}, {1380, 89}});
}

// SPEC 12. The two outer spans of 3.85 and 4.2 km run at 70 km/h; the rest at 53..63 km/h.
LineConfig cityLine() {
    return lineOf({"Кедровая", "Янтарная", "Сенокосная", "Пасечная", "Ветряная", "Часовая",
                   "Ратушная", "Типографская", "Фонтанная", "Каретная", "Лодочная", "Затонная"},
                  {{3850, 198},
                   {1560, 98},
                   {2040, 121},
                   {1350, 87},
                   {1780, 108},
                   {1180, 80},
                   {2290, 132},
                   {1630, 101},
                   {1920, 114},
                   {1410, 90},
                   {4200, 216}});
}

// SPEC 9.1 playback defaults. Fleet is the peak window, which does not depend on the value
// passed in (ScheduleAnalysis::minFleetForAllWindows).
SimParams paramsOf(LineConfig line) {
    SimParams params;
    params.line = std::move(line);
    params.schedule = defaultSchedule();
    params.period = {kOpenTod, kOpenTod + 6 * kHour};
    params.stepSec = 60;
    params.rate = 1.0;
    params.line.fleetSize = analyze(params.line, params.schedule).minFleetForAllWindows;
    return params;
}

SimParams trainingParams() {
    return paramsOf(trainingLine());
}

} // namespace

ScheduleConfig defaultSchedule() {
    ScheduleConfig schedule;
    schedule.delayMin = 20;
    schedule.delayMax = 60;
    for (int day = 0; day < 5; ++day) {
        schedule.days[static_cast<size_t>(day)] = weekdayProfile();
    }
    for (int day = 5; day < 7; ++day) {
        schedule.days[static_cast<size_t>(day)] = weekendProfile();
    }
    return schedule;
}

SimParams defaultParams() {
    return trainingParams();
}

std::vector<Preset> builtinPresets() {
    return {
        {"metro-8", "Учебная, 8 станций", trainingParams()},
        {"short-4", "Короткая, 4 станции", paramsOf(shortLine())},
        {"metro-12", "Городская, 12 станций", paramsOf(cityLine())},
    };
}

} // namespace ubahn::api
