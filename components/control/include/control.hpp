#pragma once
#include <cstdint>

namespace industrial {
struct Sample {
    uint64_t timestamp_ms = 0;
    float temperature_c = 0;
    float vibration = 0;
    bool valid = false;
    bool publish = true;
};

enum class State : uint8_t { INIT, NORMAL, WARNING, FAULT_LATCHED, RECOVERY_READY };
enum Fault : uint32_t {
    NO_FAULT = 0, TEMPERATURE = 1u << 0, VIBRATION = 1u << 1,
    INVALID_SAMPLE = 1u << 2, SENSOR_TIMEOUT = 1u << 3,
    TASK_HEALTH = 1u << 4, CONTROL_DEADLINE = 1u << 5
};
struct Config {
    float warning_c = 65, warning_clear_c = 62;
    float critical_c = 80, vibration_critical = 7;
    float recovery_c = 60, recovery_vibration = 4;
    float fan_on_c = 55, fan_off_c = 50;
    uint64_t freshness_ms = 350, recovery_dwell_ms = 3000;
};
struct Input {
    uint64_t now_ms = 0;
    Sample sample{};
    bool has_sample = false;
    bool initialized = true;
    bool task_healthy = true;
    bool deadline_ok = true;
    bool acknowledge = false;
};
struct Output {
    State state = State::INIT;
    uint32_t active_faults = NO_FAULT;
    uint32_t latched_faults = NO_FAULT;
    bool motor = false, fan = false, alarm = false;
    bool transition = false, ack_accepted = false, ack_rejected = false;
};

class Controller {
public:
    explicit Controller(Config config = {}) : config_(config) {}
    Output step(const Input& input);
private:
    Config config_;
    State state_ = State::INIT;
    uint32_t latched_ = NO_FAULT;
    uint64_t safe_since_ = 0;
    bool safe_tracking_ = false, fan_ = false;
};

// Debounce both press and release; a boot-held button cannot acknowledge.
class ButtonDebouncer {
public:
    explicit ButtonDebouncer(bool initially_high = true, uint64_t debounce_ms = 50)
        : candidate_high_(initially_high), stable_high_(initially_high),
          armed_(initially_high), debounce_ms_(debounce_ms) {}
    bool update(bool high, uint64_t now_ms);
private:
    bool candidate_high_, stable_high_, armed_;
    uint64_t changed_ms_ = 0, debounce_ms_;
};
const char* state_name(State state);
const char* primary_fault(uint32_t faults);
}
