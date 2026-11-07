// =============================================================================
// schedule.cpp - 计划表实现
// =============================================================================
#include "schedule.h"

#include <windows.h>
#include <ctime>

namespace schedule {

unsigned long long Scheduler::Now() {
    return GetTickCount64();
}

void Scheduler::SetInterval(Id id, unsigned long long now,
                            unsigned long long intervalMs, bool enabled) {
    if (id < 0 || id >= Count) return;
    Plan& p = plans_[id];
    p.enabled = enabled && intervalMs > 0;
    p.interval = intervalMs;
    p.nextDue = p.enabled ? now + intervalMs : 0;
}

void Scheduler::SetDueAt(Id id, unsigned long long dueMs, bool enabled) {
    if (id < 0 || id >= Count) return;
    Plan& p = plans_[id];
    p.enabled = enabled;
    p.interval = 0;
    p.nextDue = enabled ? dueMs : 0;
}

bool Scheduler::Due(Id id, unsigned long long now) const {
    if (id < 0 || id >= Count) return false;
    const Plan& p = plans_[id];
    return p.enabled && p.nextDue != 0 && now >= p.nextDue;
}

void Scheduler::Advance(Id id, unsigned long long now) {
    if (id < 0 || id >= Count) return;
    Plan& p = plans_[id];
    if (!p.enabled) return;
    if (p.interval > 0) {
        // 直接排到未来：睡眠/卡顿错过的周期只执行一次，不补跑
        p.nextDue = now + p.interval;
    } else {
        p.enabled = false;
        p.nextDue = 0;
    }
}

unsigned long long Scheduler::NextDue(Id id) const {
    if (id < 0 || id >= Count) return 0;
    return plans_[id].nextDue;
}

void Scheduler::Disable(Id id) {
    if (id < 0 || id >= Count) return;
    plans_[id].enabled = false;
    plans_[id].nextDue = 0;
}

unsigned long long MillisecondsToNextMidnight() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_mday += 1;   // 明天零点；mktime 自动处理跨月、闰年与夏令时
    local.tm_isdst = -1;
    std::time_t midnight = std::mktime(&local);
    long long diff = static_cast<long long>(midnight - now);
    if (diff <= 0) diff = 1000;
    if (diff > 25LL * 3600 * 1000) diff = 25LL * 3600 * 1000;
    return static_cast<unsigned long long>(diff) * 1000ULL;
}

} // namespace schedule
