// =============================================================================
// schedule.h - 轻量计划表
//
// 所有周期任务不在代码中 Sleep/等待，而是记录“下一次到期时刻”，
// 由主窗口的倒计时循环每秒核对；到期后执行一次并把下一次排到未来，
// 避免睡眠唤醒后补跑多次造成突发请求/卡顿。
// =============================================================================
#pragma once

namespace schedule {

enum Id {
    Hitokoto = 0, // 一言刷新
    Article,      // 一文刷新
    DayChange,    // 本地零点（week 模块换日）
    Count,
};

class Scheduler {
public:
    // 现在（毫秒，单调递增，不受系统时间调整影响）
    static unsigned long long Now();

    // 从现在起按 intervalMs 周期排程
    void SetInterval(Id id, unsigned long long now,
                     unsigned long long intervalMs, bool enabled);

    // 指定绝对到期时刻（用于下一个零点）；interval=0 表示一次性
    void SetDueAt(Id id, unsigned long long dueMs, bool enabled);

    // 是否到期
    bool Due(Id id, unsigned long long now) const;

    // 到期后推进：周期任务排到 now + interval；一次性任务关闭
    void Advance(Id id, unsigned long long now);

    // 下一次到期时刻（0 表示未启用）
    unsigned long long NextDue(Id id) const;

    void Disable(Id id);

private:
    struct Plan {
        unsigned long long nextDue = 0;
        unsigned long long interval = 0;
        bool enabled = false;
    };
    Plan plans_[Count];
};

// 计算距离下一个本地零点的毫秒数（跨夏令时也按真实日历）
unsigned long long MillisecondsToNextMidnight();

} // namespace schedule
