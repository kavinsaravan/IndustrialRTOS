#include "telemetry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cinttypes>
namespace industrial {
bool RetryBackoff::attempt(uint64_t now) {
    if (now < next_ms_) return false;
    next_ms_ = now + delay_ms_;
    delay_ms_ = std::min<uint64_t>(delay_ms_*2,30000);
    return true;
}
bool format_json(const TelemetryFrame& f, char* buffer, size_t capacity) {
    if (!buffer || !capacity) return false;
    char temperature[32]="null", vibration[32]="null";
    if (f.sample.valid && std::isfinite(f.sample.temperature_c))
        std::snprintf(temperature,sizeof(temperature),"%.2f",static_cast<double>(f.sample.temperature_c));
    if (f.sample.valid && std::isfinite(f.sample.vibration))
        std::snprintf(vibration,sizeof(vibration),"%.2f",static_cast<double>(f.sample.vibration));
    const auto& c=f.counters;
    const int n=std::snprintf(buffer,capacity,
        "{\"uptime_ms\":%" PRIu64 ",\"sample_ms\":%" PRIu64 ",\"temperature_c\":%s,\"vibration\":%s,"
        "\"state\":\"%s\",\"valid\":%s,\"motor\":%s,\"fan\":%s,\"alarm\":%s,"
        "\"active_faults\":%" PRIu32 ",\"latched_faults\":%" PRIu32 ",\"stale_tasks\":%" PRIu32 ","
        "\"supervisor_inhibit\":%s,\"wifi\":%s,\"mqtt\":%s,"
        "\"sensor_queue_overflows\":%" PRIu32 ",\"event_queue_overflows\":%" PRIu32 ","
        "\"invalid_samples\":%" PRIu32 ",\"fault_entries\":%" PRIu32 ",\"ack_rejections\":%" PRIu32 ","
        "\"telemetry_drops\":%" PRIu32 ",\"max_control_exec_us\":%" PRIu64 ",\"max_sample_to_output_ms\":%" PRIu64 ","
        "\"last_seen_ms\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],"
        "\"missed_deadlines\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "],"
        "\"max_jitter_ms\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],"
        "\"stack_free_bytes\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "]}",
        f.now_ms,f.sample.timestamp_ms,temperature,vibration,state_name(f.output.state),
        f.sample.valid?"true":"false",f.output.motor?"true":"false",f.output.fan?"true":"false",f.output.alarm?"true":"false",
        f.output.active_faults,f.output.latched_faults,f.stale_tasks,f.supervisor_inhibit?"true":"false",
        f.wifi_connected?"true":"false",f.mqtt_connected?"true":"false",
        c.sensor_queue_overflows,c.event_queue_overflows,c.invalid_samples,c.fault_entries,c.ack_rejections,c.telemetry_drops,
        c.max_control_exec_us,c.max_sample_to_output_ms,
        f.tasks[0].last_seen_ms,f.tasks[1].last_seen_ms,f.tasks[2].last_seen_ms,f.tasks[3].last_seen_ms,
        f.tasks[0].missed_deadlines,f.tasks[1].missed_deadlines,f.tasks[2].missed_deadlines,f.tasks[3].missed_deadlines,
        f.tasks[0].max_jitter_ms,f.tasks[1].max_jitter_ms,f.tasks[2].max_jitter_ms,f.tasks[3].max_jitter_ms,
        f.tasks[0].stack_free_bytes,f.tasks[1].stack_free_bytes,f.tasks[2].stack_free_bytes,f.tasks[3].stack_free_bytes);
    if (n<0 || static_cast<size_t>(n)>=capacity) { buffer[0]='\0'; return false; }
    return true;
}
}
