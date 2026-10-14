#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace industrial {
enum class Task : size_t { Sensor, Control, Diagnostics, Telemetry, Count };
constexpr size_t TASK_COUNT = static_cast<size_t>(Task::Count);
struct TaskStats {
    uint64_t last_seen_ms = 0;
    uint32_t beats = 0, missed_deadlines = 0;
    uint64_t max_jitter_ms = 0;
    uint32_t stack_free_bytes = 0;
    bool seen = false;
};
struct Counters {
    uint32_t sensor_queue_overflows = 0, event_queue_overflows = 0;
    uint32_t invalid_samples = 0, fault_entries = 0, ack_rejections = 0;
    uint32_t telemetry_drops = 0;
    uint64_t max_control_exec_us = 0, max_sample_to_output_ms = 0;
};
// Caller provides synchronization. No allocator or RTOS dependency.
class Diagnostics {
public:
    void beat(Task task, uint64_t now_ms, uint64_t lateness_ms = 0);
    uint32_t stale_mask(uint64_t now_ms) const;
    bool control_deadline_ok(uint64_t now_ms) const;
    void record_queue_send(bool success, bool sensor_queue);
    const std::array<TaskStats,TASK_COUNT>& tasks() const { return stats_; }
    void stack_free(Task task, uint32_t bytes) { stats_[static_cast<size_t>(task)].stack_free_bytes = bytes; }
    Counters counters{};
private:
    std::array<TaskStats,TASK_COUNT> stats_{};
};
}
