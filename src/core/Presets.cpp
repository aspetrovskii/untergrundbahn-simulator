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

// Names and lengths only. tStd stays empty: buildLine fills defaultStdRunTime (decision R7).
LineConfig lineFromLengths(std::vector<std::string> names, std::initializer_list<double> lengths) {
    LineConfig line;
    line.stationNames = std::move(names);
    line.spans.reserve(lengths.size());
    for (double lengthM : lengths) {
        SpanInput span;
        span.lengthM = lengthM;
        line.spans.push_back(span);
    }
    return line;
}

// SPEC 12. Explicit tStd, so some spans are coefficient-limited and some are physical.
LineConfig trainingLine() {
    LineConfig line = lineFromLengths({"Северная", "Полевая", "Заводская", "Университет",
                                       "Центральная", "Рынок", "Парк", "Южная"},
                                      {1100, 780, 1700, 950, 1500, 860, 1400});
    const int32_t tStd[] = {120, 90, 130, 100, 110, 95, 125};
    for (size_t i = 0; i < line.spans.size(); ++i) {
        line.spans[i].tStd = tStd[i];
    }
    return line;
}

// Decision R7. Lengths where ⌈tStd/1.5⌉ ties ⌈t_phys⌉ alternate with coefficient-limited ones.
// defaultStdRunTime never makes the physical minimum strictly tighter, so tStd is not set.
LineConfig minimalLine() {
    return lineFromLengths(
        {"Пристань", "Берёзовая", "Слобода", "Ратуша", "Ярмарка", "Роща", "Застава"},
        {400, 1200, 650, 2100, 800, 1500});
}

// Decision R7. Same binding mix as minimalLine, across 400..5000 m (SPEC 12, N = 20).
LineConfig longLine() {
    return lineFromLengths({"Озёрная",    "Каменная",      "Луговая",   "Прядильная", "Набережная",
                            "Крепостная", "Вишнёвая",      "Мостовая",  "Губернская", "Ткацкая",
                            "Садовая",    "Привокзальная", "Кузнечная", "Звёздная",   "Ремесленная",
                            "Соборная",   "Холмистая",     "Граничная", "Портовая",   "Дальняя"},
                           {4600, 4100, 3200, 2800, 1950, 1700, 1200, 900, 600, 500, 400, 800, 1050,
                            1400, 1650, 2400, 3050, 4500, 5000});
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
        {"training-8", "Учебная, 8 станций", trainingParams()},
        {"minimal-7", "Минимальная, 7 станций", paramsOf(minimalLine())},
        {"long-20", "Длинная, 20 станций", paramsOf(longLine())},
    };
}

} // namespace ubahn::api
