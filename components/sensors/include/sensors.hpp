#pragma once
#include "control.hpp"
#include "esp_adc/adc_oneshot.h"

namespace industrial {
// Knobs represent engineering values; this is not a calibrated physical sensor.
class Sensors {
public:
    bool initialize();
    Sample read(uint64_t now_ms);
private:
    adc_oneshot_unit_handle_t adc_ = nullptr;
};
}
