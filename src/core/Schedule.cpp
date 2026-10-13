#include "core/Schedule.h"

#include "api/Functions.h"
#include "core/Time.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace ubahn::core {
namespace {

int32_t narrowDuration(int64_t value) {
    if (value > std::numeric_limits<int32_t>::max()) {
        return std::numeric_limits<int32_t>::max();
    }
    if (value < std::numeric_limits<int32_t>::min()) {
        return std::numeric_limits<int32_t>::min();
    }
    return static_cast<int32_t>(value);
}

std::string formatTod(int32_t seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    const int32_t hours = seconds / 3600;
    const int32_t minutes = (seconds % 3600) / 60;
    std::string text = std::to_string(hours);
    text += ':';
    if (minutes < 10) {
        text += '0';
    }
    text += std::to_string(minutes);
    return text;
}

// 26 поездов, 21 поезд, 22 поезда, 11 поездов.
const char* trainsWord(int32_t count) {
    const int32_t n = count < 0 ? -count : count;
    const int32_t mod100 = n % 100;
    const int32_t mod10 = n % 10;
    if (mod100 >= 11 && mod100 <= 14) {
        return "поездов";
    }
    if (mod10 == 1) {
        return "поезд";
    }
    if (mod10 >= 2 && mod10 <= 4) {
        return "поезда";
    }
    return "поездов";
}

std::string stretchMessage(int day, const api::Window& window, int32_t effective,
                           int32_t required) {
    static constexpr std::array<const char*, 7> kDays = {
        "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота", "Воскресенье",
    };
    const char* dayName = (day >= 0 && day < 7) ? kDays[static_cast<size_t>(day)] : "День";
    std::string text = dayName;
    text += ", ";
    text += formatTod(window.fromTod);
    text += '-';
    text += formatTod(window.toTod);
    text += ": плановый интервал ";
    text += std::to_string(window.headway);
    text += " с растянут до ";
    text += std::to_string(effective);
    text += " с, требуется ";
    text += std::to_string(required);
    text += ' ';
    text += trainsWord(required);
    return text;
}

} // namespace

std::optional<int> windowIndexAt(const api::ScheduleConfig& schedule, api::SimTime t) {
    // Service hours are [kOpenTod, 86400). todOf is already in [0, 86400).
    const int32_t tod = todOf(t);
    if (tod < api::kOpenTod) {
        return std::nullopt;
    }
    const int day = dowOf(t);
    const std::vector<api::Window>& windows = schedule.days[static_cast<size_t>(day)].windows;
    for (size_t i = 0; i < windows.size(); ++i) {
        const api::Window& window = windows[i];
        if (window.fromTod <= tod && tod < window.toTod) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

const api::Window* windowAt(const api::ScheduleConfig& schedule, api::SimTime t) {
    const std::optional<int> index = windowIndexAt(schedule, t);
    if (!index) {
        return nullptr;
    }
    const int day = dowOf(t);
    const std::vector<api::Window>& windows = schedule.days[static_cast<size_t>(day)].windows;
    if (*index < 0 || static_cast<size_t>(*index) >= windows.size()) {
        return nullptr;
    }
    return &windows[static_cast<size_t>(*index)];
}

int32_t tToTerm(const Line& line, int32_t dwell) {
    // ALGORITHM 5.2: running time plus intermediate dwells, without the final alighting.
    int64_t sum = 0;
    for (const Span& span : line.spans) {
        sum += span.tStd;
    }
    sum += static_cast<int64_t>(dwell) * (static_cast<int64_t>(line.n) - 2);
    return narrowDuration(sum);
}

int32_t tOneway(const Line& line, int32_t dwell) {
    return narrowDuration(static_cast<int64_t>(tToTerm(line, dwell)) + dwell);
}

int32_t tCycle(const Line& line, int32_t dwell) {
    // 2 * T_oneway + 2 * dwell + 2 * T_turn. ALGORITHM 5.2.
    const int64_t oneway = tOneway(line, dwell);
    const int64_t turn = tTurn(line.cfg);
    return narrowDuration(2 * oneway + 2 * static_cast<int64_t>(dwell) + 2 * turn);
}

int32_t tTurn(const api::LineConfig& line) {
    const int64_t turn = static_cast<int64_t>(line.toDeadEnd) + line.cabChange + line.fromDeadEnd;
    return narrowDuration(turn);
}

int32_t nReq(int32_t cycle, int32_t headway) {
    // ceil(T_cycle / H). A non-positive H is invalid (validate); do not divide by it.
    if (headway <= 0) {
        return cycle > 0 ? std::numeric_limits<int32_t>::max() : 0;
    }
    return narrowDuration(ceilDiv(cycle, headway));
}

int32_t hEff(int32_t cycle, int32_t headway, int fleet) {
    // SPEC 6.4. Planned H when the fleet covers the window; otherwise round the
    // per-train share of T_cycle up to 5 s (ALGORITHM 5.2).
    const int32_t need = nReq(cycle, headway);
    if (fleet >= need) {
        return headway;
    }
    if (fleet <= 0) {
        return std::numeric_limits<int32_t>::max();
    }
    return narrowDuration(roundUpTo(ceilDiv(cycle, fleet), 5));
}

api::ScheduleAnalysis analyzeLine(const Line& line, const api::ScheduleConfig& schedule) {
    api::ScheduleAnalysis analysis;
    analysis.tTurn = tTurn(line.cfg);
    // T_cycle depends on the window dwell. The top-level field is the maximum (decision R5).
    int32_t maxCycle = 0;
    int32_t maxNeed = 0;
    for (size_t day = 0; day < schedule.days.size(); ++day) {
        const std::vector<api::Window>& windows = schedule.days[day].windows;
        std::vector<api::WindowAnalysis>& analyzed = analysis.days[day];
        analyzed.reserve(windows.size());
        for (const api::Window& window : windows) {
            const int32_t cycle = tCycle(line, window.dwell);
            if (cycle > maxCycle) {
                maxCycle = cycle;
            }
            api::WindowAnalysis row;
            row.nReq = nReq(cycle, window.headway);
            row.hEff = hEff(cycle, window.headway, line.cfg.fleetSize);
            row.stretched = line.cfg.fleetSize < row.nReq;
            if (row.nReq > maxNeed) {
                maxNeed = row.nReq;
            }
            analyzed.push_back(row);
        }
    }
    analysis.tCycle = maxCycle;
    analysis.minFleetForAllWindows = maxNeed;
    return analysis;
}

std::vector<api::Warning> stretchWarnings(const api::ScheduleAnalysis& analysis,
                                          const api::ScheduleConfig& schedule) {
    std::vector<api::Warning> warnings;
    for (size_t day = 0; day < schedule.days.size(); ++day) {
        const std::vector<api::Window>& windows = schedule.days[day].windows;
        const std::vector<api::WindowAnalysis>& analyzed = analysis.days[day];
        const size_t count = windows.size() < analyzed.size() ? windows.size() : analyzed.size();
        for (size_t i = 0; i < count; ++i) {
            if (!analyzed[i].stretched) {
                continue;
            }
            api::Warning warning;
            warning.code = api::WarningCode::IntervalStretched;
            warning.dayOfWeek = static_cast<int>(day);
            warning.windowIndex = static_cast<int>(i);
            warning.plannedHeadway = windows[i].headway;
            warning.effectiveHeadway = analyzed[i].hEff;
            warning.requiredTrains = analyzed[i].nReq;
            warning.message = stretchMessage(static_cast<int>(day), windows[i], analyzed[i].hEff,
                                             analyzed[i].nReq);
            warnings.push_back(std::move(warning));
        }
    }
    return warnings;
}

std::vector<api::StretchInfo> stretchedWindows(const api::ScheduleAnalysis& analysis,
                                               const api::ScheduleConfig& schedule) {
    std::vector<api::StretchInfo> stretched;
    for (size_t day = 0; day < schedule.days.size(); ++day) {
        const std::vector<api::Window>& windows = schedule.days[day].windows;
        const std::vector<api::WindowAnalysis>& analyzed = analysis.days[day];
        const size_t count = windows.size() < analyzed.size() ? windows.size() : analyzed.size();
        for (size_t i = 0; i < count; ++i) {
            if (!analyzed[i].stretched) {
                continue;
            }
            api::StretchInfo info;
            info.dayOfWeek = static_cast<int>(day);
            info.windowIndex = static_cast<int>(i);
            info.planned = windows[i].headway;
            info.effective = analyzed[i].hEff;
            stretched.push_back(info);
        }
    }
    return stretched;
}

int32_t hEffAt(const api::ScheduleAnalysis& analysis, const api::ScheduleConfig& schedule,
               api::SimTime t) {
    const std::optional<int> index = windowIndexAt(schedule, t);
    if (!index) {
        return 0;
    }
    const int day = dowOf(t);
    const std::vector<api::WindowAnalysis>& analyzed = analysis.days[static_cast<size_t>(day)];
    if (*index < 0 || static_cast<size_t>(*index) >= analyzed.size()) {
        return 0;
    }
    return analyzed[static_cast<size_t>(*index)].hEff;
}

} // namespace ubahn::core

namespace ubahn::api {

ScheduleAnalysis analyze(const LineConfig& line, const ScheduleConfig& schedule) {
    return core::analyzeLine(core::buildLine(line), schedule);
}

} // namespace ubahn::api
