#pragma once
#include "control.hpp"
#include "diagnostics.hpp"
#include <cstddef>
#include <cstdint>
namespace industrial {
struct TelemetryFrame {
    uint64_t now_ms = 0;
    Sample sample{};
    Output output{};
    Counters counters{};
    std::array<TaskStats,TASK_COUNT> tasks{};
    uint32_t stale_tasks = 0;
    bool supervisor_inhibit = false;
    bool wifi_connected = false, mqtt_connected = false;
};
// Returns false on insufficient capacity; never emits a truncated JSON payload.
// Invalid sensor values are represented by JSON null, not NaN/Infinity.
bool format_json(const TelemetryFrame& frame, char* buffer, size_t capacity);
class RetryBackoff {
public:
    bool attempt(uint64_t now_ms);
    void connected() { next_ms_ = 0; delay_ms_ = 1000; }
    uint64_t next_ms() const { return next_ms_; }
private:
    uint64_t next_ms_ = 0, delay_ms_ = 1000;
};
struct NetworkStatus { bool wifi = false, mqtt = false; uint32_t dropped = 0; };
// ESP-IDF implementation stays out of host-native logic builds.
bool network_initialize();
void network_service(uint64_t now_ms, const char* json);
NetworkStatus network_status();
}
