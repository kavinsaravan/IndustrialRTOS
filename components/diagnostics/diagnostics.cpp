#include "diagnostics.hpp"
#include <algorithm>
namespace industrial {
namespace {
constexpr std::array<uint64_t,TASK_COUNT> periods = {100,50,100,1000};
constexpr std::array<uint64_t,TASK_COUNT> limits = {300,150,300,3000};
}
void Diagnostics::beat(Task task, uint64_t now, uint64_t lateness) {
    const size_t i = static_cast<size_t>(task);
    auto& stats = stats_[i];
    const bool missed = lateness > periods[i] / 2 ||
        (stats.seen && (now < stats.last_seen_ms || now - stats.last_seen_ms > periods[i] + periods[i]/2));
    if (missed) ++stats.missed_deadlines;
    stats.last_seen_ms = now;
    stats.seen = true;
    ++stats.beats;
    stats.max_jitter_ms = std::max(stats.max_jitter_ms,lateness);
}
uint32_t Diagnostics::stale_mask(uint64_t now) const {
    uint32_t mask = 0;
    for (size_t i = 0; i < TASK_COUNT; ++i) {
        const auto& s = stats_[i];
        if ((!s.seen && now > limits[i]) ||
            (s.seen && (now < s.last_seen_ms || now - s.last_seen_ms > limits[i]))) mask |= 1u << i;
    }
    return mask;
}
bool Diagnostics::control_deadline_ok(uint64_t now) const {
    const auto& s = stats_[static_cast<size_t>(Task::Control)];
    return s.seen ? now >= s.last_seen_ms && now - s.last_seen_ms <= 75 : now <= 150;
}
void Diagnostics::record_queue_send(bool success, bool sensor_queue) {
    if (success) return;
    if (sensor_queue) ++counters.sensor_queue_overflows;
    else ++counters.event_queue_overflows;
}
}
