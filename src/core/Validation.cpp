#include "api/Functions.h"
#include "core/Schedule.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ubahn::api {
namespace {

// SPEC 9.1, 9.4. Issues come out line, then schedule, then period, then step, then rate
// (BE-06). A one-way trip longer than six hours is not an issue: slot generation stops
// instead of rejecting the line.

void addIssue(std::vector<ValidationIssue>& out, std::string field, IssueCode code,
              std::string message) {
    ValidationIssue issue;
    issue.field = std::move(field);
    issue.code = code;
    issue.message = std::move(message);
    out.push_back(std::move(issue));
}

std::string formatDouble(double value) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value > 0.0 ? "inf" : "-inf";
    }
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) <= 1e-9 && std::fabs(rounded) < 1.0e15) {
        return std::to_string(static_cast<long long>(rounded));
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    return std::string(buffer);
}

// Whole minutes as H:MM, including 24:00. Anything else stays in seconds so the text
// does not drop a remainder.
std::string formatClock(int32_t seconds) {
    if (seconds < 0 || seconds % 60 != 0) {
        return std::to_string(seconds) + " с";
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

std::string formatWindow(const Window& window) {
    return formatClock(window.fromTod) + "-" + formatClock(window.toTod);
}

std::string outsideRange(const char* label, const std::string& value, const char* unit, int lo,
                         int hi) {
    std::string text = label;
    text += ' ';
    text += value;
    if (unit != nullptr && unit[0] != '\0') {
        text += ' ';
        text += unit;
    }
    text += " вне диапазона ";
    text += std::to_string(lo);
    text += "…";
    text += std::to_string(hi);
    if (unit != nullptr && unit[0] != '\0') {
        text += ' ';
        text += unit;
    }
    return text;
}

void checkInt(std::vector<ValidationIssue>& out, std::string field, const char* label,
              const char* unit, int32_t value, int lo, int hi) {
    if (value < lo || value > hi) {
        addIssue(out, std::move(field), IssueCode::ValueOutOfRange,
                 outsideRange(label, std::to_string(value), unit, lo, hi));
    }
}

void checkDouble(std::vector<ValidationIssue>& out, std::string field, const char* label,
                 const char* unit, double value, int lo, int hi) {
    if (!(value >= static_cast<double>(lo) && value <= static_cast<double>(hi))) {
        addIssue(out, std::move(field), IssueCode::ValueOutOfRange,
                 outsideRange(label, formatDouble(value), unit, lo, hi));
    }
}

std::string spanField(size_t index, const char* leaf) {
    std::string field = "line.spans[";
    field += std::to_string(index);
    field += "].";
    field += leaf;
    return field;
}

std::string dayWindows(int day) {
    std::string field = "schedule.days[";
    field += std::to_string(day);
    field += "].windows";
    return field;
}

std::string windowField(int day, size_t index, const char* leaf) {
    std::string field = dayWindows(day);
    field += '[';
    field += std::to_string(index);
    field += ']';
    if (leaf != nullptr && leaf[0] != '\0') {
        field += '.';
        field += leaf;
    }
    return field;
}

void validateLine(const LineConfig& line, std::vector<ValidationIssue>& out) {
    const int64_t stationCount = static_cast<int64_t>(line.stationNames.size());
    if (stationCount < 4 || stationCount > 24) {
        addIssue(out, "line.stationNames", IssueCode::StationCountOutOfRange,
                 "Число станций " + std::to_string(stationCount) + " вне диапазона 4…24");
    }

    const int64_t spanCount = static_cast<int64_t>(line.spans.size());
    if (spanCount != stationCount - 1) {
        addIssue(out, "line.spans", IssueCode::SpanCountMismatch,
                 "Число перегонов " + std::to_string(spanCount) + " не равно " +
                     std::to_string(stationCount - 1));
    }

    for (size_t i = 0; i < line.spans.size(); ++i) {
        const SpanInput& span = line.spans[i];
        checkDouble(out, spanField(i, "lengthM"), "Длина перегона", "м", span.lengthM, 400, 6500);
        // physMinRunTime of a non-positive length is not a meaningful minimum (tPhys goes NaN).
        if (span.tStd.has_value() && std::isfinite(span.lengthM) && span.lengthM > 0.0) {
            const int32_t phys = physMinRunTime(span.lengthM);
            if (*span.tStd < phys) {
                addIssue(out, spanField(i, "tStd"), IssueCode::TStdBelowPhysMin,
                         "Время хода " + std::to_string(*span.tStd) +
                             " с меньше физического минимума " + std::to_string(phys) + " с");
            }
        }
    }

    checkInt(out, "line.fleetSize", "Размер парка", "", line.fleetSize, 2, 200);
    checkInt(out, "line.dwellMin", "Минимальная стоянка", "с", line.dwellMin, 30, 180);
    checkInt(out, "line.headwayMin", "Минимальный интервал прибытия", "с", line.headwayMin, 30,
             300);
    checkInt(out, "line.tClear", "t_clear", "с", line.tClear, 10, 120);
    checkDouble(out, "line.kSlow", "k_slow", "", line.kSlow, 1, 2);
    checkInt(out, "line.toDeadEnd", "Время в тупик", "с", line.toDeadEnd, 10, 600);
    checkInt(out, "line.cabChange", "Смена кабины", "с", line.cabChange, 10, 600);
    checkInt(out, "line.fromDeadEnd", "Время из тупика", "с", line.fromDeadEnd, 10, 600);
    checkInt(out, "line.depotOut", "Выдача из депо", "с", line.depotOut, 10, 600);
    checkInt(out, "line.depotIn", "Уход в депо", "с", line.depotIn, 10, 600);
    checkDouble(out, "line.signalStopOffsetM", "Точка остановки у сигнала", "м",
                line.signalStopOffsetM, 50, 300);

    bool haveLength = false;
    double shortest = 0.0;
    for (const SpanInput& span : line.spans) {
        if (!std::isfinite(span.lengthM)) {
            continue;
        }
        if (!haveLength || span.lengthM < shortest) {
            shortest = span.lengthM;
            haveLength = true;
        }
    }
    // SPEC 9.4: the stop must be strictly closer than the shortest span (Types.h: < min).
    if (haveLength && !(line.signalStopOffsetM < shortest)) {
        addIssue(out, "line.signalStopOffsetM", IssueCode::SignalOffsetTooLong,
                 "Точка остановки у сигнала " + formatDouble(line.signalStopOffsetM) +
                     " м не короче самого короткого перегона " + formatDouble(shortest) + " м");
    }

    checkInt(out, "line.signalApproach", "Подход от сигнала", "с", line.signalApproach, 10, 60);
}

struct Interval {
    int32_t from = 0;
    int32_t to = 0;
    size_t index = 0;
};

void appendOrderAndOverlap(const std::vector<Window>& windows, int day,
                           std::vector<ValidationIssue>& out) {
    for (size_t i = 1; i < windows.size(); ++i) {
        if (windows[i].fromTod < windows[i - 1].fromTod) {
            // TODO(BE-06): IssueCode has no value for an unsorted profile. WindowsOverlap
            // rejects it; the message says the windows are not in ascending order.
            addIssue(out, windowField(day, i, nullptr), IssueCode::WindowsOverlap,
                     "Окно " + formatWindow(windows[i]) + " начинается раньше предыдущего окна " +
                         formatWindow(windows[i - 1]));
        }
    }

    std::vector<Interval> ordered;
    ordered.reserve(windows.size());
    for (size_t i = 0; i < windows.size(); ++i) {
        if (windows[i].fromTod < windows[i].toTod) {
            ordered.push_back(Interval{windows[i].fromTod, windows[i].toTod, i});
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const Interval& a, const Interval& b) {
        if (a.from != b.from) {
            return a.from < b.from;
        }
        return a.index < b.index;
    });
    for (size_t i = 1; i < ordered.size(); ++i) {
        if (ordered[i].from < ordered[i - 1].to) {
            const Window& later = windows[ordered[i].index];
            const Window& earlier = windows[ordered[i - 1].index];
            addIssue(out, windowField(day, ordered[i].index, nullptr), IssueCode::WindowsOverlap,
                     "Окно " + formatWindow(later) + " перекрывается с окном " +
                         formatWindow(earlier));
        }
    }
}

void appendGaps(const std::vector<Window>& windows, int day, std::vector<ValidationIssue>& out) {
    std::vector<Interval> covered;
    covered.reserve(windows.size());
    for (size_t i = 0; i < windows.size(); ++i) {
        if (windows[i].fromTod >= windows[i].toTod) {
            continue;
        }
        const int32_t from = windows[i].fromTod < kOpenTod ? kOpenTod : windows[i].fromTod;
        const int32_t to = windows[i].toTod > kCloseTod ? kCloseTod : windows[i].toTod;
        if (from < to) {
            covered.push_back(Interval{from, to, i});
        }
    }
    std::sort(covered.begin(), covered.end(), [](const Interval& a, const Interval& b) {
        if (a.from != b.from) {
            return a.from < b.from;
        }
        return a.index < b.index;
    });

    int32_t cursor = kOpenTod;
    for (const Interval& piece : covered) {
        if (piece.from > cursor) {
            addIssue(out, windowField(day, piece.index, nullptr), IssueCode::WindowsGap,
                     "Разрыв " + formatClock(cursor) + "-" + formatClock(piece.from) +
                         " внутри часов работы 6:00-24:00");
        }
        if (piece.to > cursor) {
            cursor = piece.to;
        }
    }
    if (cursor < kCloseTod) {
        if (covered.empty()) {
            addIssue(out, dayWindows(day), IssueCode::WindowsGap,
                     "Часы работы 6:00-24:00 не покрыты окнами");
        } else {
            addIssue(out, windowField(day, covered.back().index, nullptr), IssueCode::WindowsGap,
                     "Разрыв " + formatClock(cursor) + "-24:00 внутри часов работы 6:00-24:00");
        }
    }
}

void appendWindowLimits(const LineConfig& line, const Window& window, int day, size_t index,
                        int32_t turn, std::vector<ValidationIssue>& out) {
    const int64_t headway = window.headway;
    const int64_t dwell = window.dwell;
    const int64_t clear = line.tClear;
    if (headway < dwell + clear) {
        addIssue(out, windowField(day, index, "headway"), IssueCode::HeadwayBelowDwellPlusClear,
                 "Интервал " + std::to_string(window.headway) + " с меньше стоянки " +
                     std::to_string(window.dwell) + " с плюс t_clear " +
                     std::to_string(line.tClear) + " с (C11)");
    }
    if (headway < turn) {
        addIssue(out, windowField(day, index, "headway"), IssueCode::HeadwayBelowTurnaround,
                 "Интервал " + std::to_string(window.headway) + " с меньше времени оборота " +
                     std::to_string(turn) + " с (C12)");
    }
    if (headway < line.headwayMin) {
        addIssue(out, windowField(day, index, "headway"), IssueCode::HeadwayBelowMin,
                 "Интервал " + std::to_string(window.headway) +
                     " с меньше минимального интервала прибытия " +
                     std::to_string(line.headwayMin) + " с (C6)");
    }
    if (dwell < line.dwellMin) {
        addIssue(out, windowField(day, index, "dwell"), IssueCode::DwellBelowMin,
                 "Стоянка " + std::to_string(window.dwell) + " с меньше минимальной стоянки " +
                     std::to_string(line.dwellMin) + " с (C5)");
    }
    if (!(window.delayProb >= 0.0 && window.delayProb <= 1.0)) {
        addIssue(out, windowField(day, index, "delayProb"), IssueCode::ProbabilityOutOfRange,
                 "Вероятность задержки " + formatDouble(window.delayProb) +
                     " вне диапазона [0, 1]");
    }
}

void validateDay(const LineConfig& line, const DayProfile& profile, int day, int32_t turn,
                 std::vector<ValidationIssue>& out) {
    const std::vector<Window>& windows = profile.windows;
    if (windows.empty()) {
        addIssue(out, dayWindows(day), IssueCode::WindowsGap,
                 "Профиль дня пуст: нет окон на 6:00-24:00");
        return;
    }

    for (size_t i = 0; i < windows.size(); ++i) {
        const Window& window = windows[i];
        const bool outside = window.fromTod < kOpenTod || window.fromTod >= kCloseTod ||
                             window.toTod < kOpenTod || window.toTod > kCloseTod;
        if (outside) {
            addIssue(out, windowField(day, i, nullptr), IssueCode::WindowOutOfHours,
                     "Окно " + formatWindow(window) + " выходит за часы работы 6:00-24:00");
        }
        if (window.fromTod >= window.toTod) {
            addIssue(out, windowField(day, i, "toTod"), IssueCode::ValueOutOfRange,
                     "Конец окна " + formatClock(window.toTod) + " не позже начала " +
                         formatClock(window.fromTod));
        }
    }

    appendOrderAndOverlap(windows, day, out);
    appendGaps(windows, day, out);
    for (size_t i = 0; i < windows.size(); ++i) {
        appendWindowLimits(line, windows[i], day, i, turn, out);
    }
}

void validateScheduleInto(const LineConfig& line, const ScheduleConfig& schedule,
                          std::vector<ValidationIssue>& out) {
    const int32_t turn = core::tTurn(line);
    for (int day = 0; day < 7; ++day) {
        validateDay(line, schedule.days[static_cast<size_t>(day)], day, turn, out);
    }

    if (schedule.delayMin < 0 || schedule.delayMin > 600) {
        addIssue(
            out, "schedule.delayMin", IssueCode::ValueOutOfRange,
            outsideRange("Минимальная задержка", std::to_string(schedule.delayMin), "с", 0, 600));
    }
    if (schedule.delayMax < 0 || schedule.delayMax > 600) {
        addIssue(
            out, "schedule.delayMax", IssueCode::ValueOutOfRange,
            outsideRange("Максимальная задержка", std::to_string(schedule.delayMax), "с", 0, 600));
    }
    if (schedule.delayMin > schedule.delayMax) {
        addIssue(out, "schedule.delayMin", IssueCode::DelayRangeInvalid,
                 "Минимальная задержка " + std::to_string(schedule.delayMin) +
                     " с больше максимальной " + std::to_string(schedule.delayMax) + " с");
    }
}

void validatePeriod(const SimPeriod& period, std::vector<ValidationIssue>& out) {
    if (period.end <= period.start) {
        addIssue(out, "period.end", IssueCode::PeriodInvalid,
                 "Конец периода " + std::to_string(period.end) + " с не позже начала " +
                     std::to_string(period.start) + " с");
    }
    // Decision R14. A negative start is not covered by SPEC 9.4 (end <= start).
    if (period.start < 0) {
        addIssue(out, "period.start", IssueCode::PeriodInvalid,
                 "Начало периода " + std::to_string(period.start) + " с меньше 0");
    }
}

void validateStep(int32_t stepSec, std::vector<ValidationIssue>& out) {
    if (stepSec < 1 || stepSec > 240) {
        addIssue(out, "stepSec", IssueCode::StepOutOfRange,
                 "Шаг проигрывания " + std::to_string(stepSec) + " с вне диапазона 1…240 с");
    }
}

void validateRate(double rate, std::vector<ValidationIssue>& out) {
    // Decision R14. Zero is a pause (API.md 4.2), not an error. NaN is not a rate.
    if (!(rate >= 0.0)) {
        addIssue(out, "rate", IssueCode::ValueOutOfRange,
                 "Темп " + formatDouble(rate) + " меньше 0");
    }
}

} // namespace

std::vector<ValidationIssue> validate(const SimParams& params) {
    std::vector<ValidationIssue> issues;
    validateLine(params.line, issues);
    validateScheduleInto(params.line, params.schedule, issues);
    validatePeriod(params.period, issues);
    validateStep(params.stepSec, issues);
    validateRate(params.rate, issues);
    return issues;
}

std::vector<ValidationIssue> validateSchedule(const LineConfig& line,
                                              const ScheduleConfig& schedule) {
    std::vector<ValidationIssue> issues;
    validateScheduleInto(line, schedule, issues);
    return issues;
}

} // namespace ubahn::api
