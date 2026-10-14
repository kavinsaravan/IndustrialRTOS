#include "control.hpp"
#include <cmath>

namespace industrial {
Output Controller::step(const Input& in) {
    const State previous = state_;
    uint32_t active = NO_FAULT;
    const bool finite = std::isfinite(in.sample.temperature_c) && std::isfinite(in.sample.vibration);
    const bool valid = in.has_sample && in.sample.valid && finite &&
        in.sample.temperature_c >= 20 && in.sample.temperature_c <= 100 &&
        in.sample.vibration >= 0 && in.sample.vibration <= 10;
    const bool fresh = in.has_sample && in.sample.timestamp_ms <= in.now_ms &&
        in.now_ms - in.sample.timestamp_ms <= config_.freshness_ms;
    if (!in.initialized || (in.has_sample && !valid)) active |= INVALID_SAMPLE;
    if ((in.has_sample && !fresh) || (!in.has_sample && in.now_ms > config_.freshness_ms))
        active |= SENSOR_TIMEOUT;
    if (!in.task_healthy) active |= TASK_HEALTH;
    if (!in.deadline_ok) active |= CONTROL_DEADLINE;
    // Preserve simultaneous process faults even when another interlock fails.
    if (valid && in.sample.temperature_c >= config_.critical_c) active |= TEMPERATURE;
    if (valid && in.sample.vibration >= config_.vibration_critical) active |= VIBRATION;

    bool accepted = false;
    if (active != NO_FAULT) {
        latched_ |= active;
        state_ = State::FAULT_LATCHED;
        safe_tracking_ = false;
    } else if (state_ == State::FAULT_LATCHED || state_ == State::RECOVERY_READY) {
        const bool safe = valid && fresh && in.sample.temperature_c < config_.recovery_c &&
            in.sample.vibration < config_.recovery_vibration;
        if (!safe) {
            safe_tracking_ = false;
            state_ = State::FAULT_LATCHED;
        } else {
            if (!safe_tracking_) { safe_since_ = in.now_ms; safe_tracking_ = true; }
            if (in.now_ms >= safe_since_ && in.now_ms - safe_since_ >= config_.recovery_dwell_ms)
                state_ = State::RECOVERY_READY;
            // Never accept an ACK issued before the ready state was observable.
            if (previous == State::RECOVERY_READY && state_ == State::RECOVERY_READY && in.acknowledge) {
                state_ = State::NORMAL;
                latched_ = NO_FAULT;
                safe_tracking_ = false;
                accepted = true;
            }
        }
    } else if (valid && fresh) {
        if (in.sample.temperature_c >= config_.warning_c) state_ = State::WARNING;
        else if (state_ != State::WARNING || in.sample.temperature_c < config_.warning_clear_c)
            state_ = State::NORMAL;
    }
    const bool latched = state_ == State::FAULT_LATCHED || state_ == State::RECOVERY_READY;
    if (latched || state_ == State::WARNING) fan_ = true;
    else if (valid && fresh) {
        if (in.sample.temperature_c >= config_.fan_on_c) fan_ = true;
        else if (in.sample.temperature_c < config_.fan_off_c) fan_ = false;
    }
    Output out{};
    out.state = state_;
    out.active_faults = active;
    out.latched_faults = latched_;
    out.motor = (state_ == State::NORMAL || state_ == State::WARNING) && valid && fresh && active == 0;
    out.fan = fan_;
    out.alarm = latched;
    out.transition = previous != state_;
    out.ack_accepted = accepted;
    out.ack_rejected = in.acknowledge && !accepted;
    return out;
}

bool ButtonDebouncer::update(bool high, uint64_t now) {
    if (high != candidate_high_) { candidate_high_ = high; changed_ms_ = now; }
    if (now < changed_ms_ || now - changed_ms_ < debounce_ms_ || stable_high_ == candidate_high_)
        return false;
    stable_high_ = candidate_high_;
    if (stable_high_) { armed_ = true; return false; }
    const bool press = armed_;
    armed_ = false;
    return press;
}
const char* state_name(State state) {
    switch (state) {
        case State::INIT: return "INIT";
        case State::NORMAL: return "NORMAL";
        case State::WARNING: return "WARNING";
        case State::FAULT_LATCHED: return "FAULT_LATCHED";
        case State::RECOVERY_READY: return "RECOVERY_READY";
    }
    return "UNKNOWN";
}
const char* primary_fault(uint32_t faults) {
    if (faults & TASK_HEALTH) return "TASK_HEALTH";
    if (faults & CONTROL_DEADLINE) return "CONTROL_DEADLINE";
    if (faults & SENSOR_TIMEOUT) return "SENSOR_TIMEOUT";
    if (faults & INVALID_SAMPLE) return "INVALID_SAMPLE";
    if (faults & TEMPERATURE) return "TEMPERATURE";
    if (faults & VIBRATION) return "VIBRATION";
    return "NONE";
}
}
