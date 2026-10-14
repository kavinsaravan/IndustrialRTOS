#include "sensors.hpp"
#include "driver/gpio.h"

namespace industrial {
bool Sensors::initialize() {
    adc_oneshot_unit_init_cfg_t unit{};
    unit.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unit, &adc_) != ESP_OK) return false;
    adc_oneshot_chan_cfg_t channel{};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_12;
    if (adc_oneshot_config_channel(adc_, ADC_CHANNEL_6, &channel) != ESP_OK ||
        adc_oneshot_config_channel(adc_, ADC_CHANNEL_7, &channel) != ESP_OK) return false;
    gpio_config_t input{};
    input.pin_bit_mask = (1ULL << GPIO_NUM_26) | (1ULL << GPIO_NUM_27);
    input.mode = GPIO_MODE_INPUT;
    input.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&input) == ESP_OK;
}
Sample Sensors::read(uint64_t now_ms) {
    int temperature = 0, vibration = 0;
    Sample sample{};
    sample.timestamp_ms = now_ms;
    const auto t = adc_oneshot_read(adc_, ADC_CHANNEL_6, &temperature);
    const auto v = adc_oneshot_read(adc_, ADC_CHANNEL_7, &vibration);
    sample.temperature_c = 20.0f + 80.0f * static_cast<float>(temperature) / 4095.0f;
    sample.vibration = 10.0f * static_cast<float>(vibration) / 4095.0f;
    sample.valid = t == ESP_OK && v == ESP_OK && gpio_get_level(GPIO_NUM_26) != 0;
    // GPIO27 low explicitly simulates acquisition loss. The task still beats.
    sample.publish = gpio_get_level(GPIO_NUM_27) != 0;
    return sample;
}
}
