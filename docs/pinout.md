# Simulation wiring

All equipment is virtual. The ESP32 board is `board-esp32-devkit-c-v4`.
The two `wokwi-potentiometer` parts are directly controllable analog sensor
surrogates, not calibrated temperature or vibration sensors.

| Part | Part pin | ESP32 GPIO | Function |
| --- | --- | --- | --- |
| Temperature knob | SIG | 34 / ADC1 channel 6 | 20–100 °C demonstration range |
| Vibration knob | SIG | 35 / ADC1 channel 7 | 0–10 arbitrary demonstration units |
| Both knobs | VCC / GND | 3V3 / GND | Supply / return |
| Motor LED | A through 220 Ω | 18 | Active-high simulated motor enable |
| Fan LED | A through 220 Ω | 19 | Active-high simulated cooling enable |
| Alarm LED | A through 220 Ω | 23 | Active-high latched alarm |
| All LEDs | C | GND | Return |
| ACK button | 1.l / 2.l | 25 / GND | Falling edge, internal pull-up |
| Invalid switch | 2 / 3 | 26 / GND | Low injects invalid measurements |
| Sampling switch | 2 / 3 | 27 / GND | Low stops publishing measurements |
| Optional control stall switch | 2 / 3 | 33 / GND | Low stalls control when demo option is enabled |

Each switch connects its center pin to either an unconnected contact (normal,
internal pull-up) or GND (fault injection). Start both switches with their center
contact disconnected from GND. Releasing an invalid or sampling fault does not
automatically restart the machine.

The input mapping is `temperature = 20 + 80 * raw / 4095` and
`vibration = 10 * raw / 4095`. Endpoints are demonstration values. No ADC voltage
calibration or physical RMS vibration measurement is implied. Use the reported
engineering value, rather than an assumed knob position, to find thresholds.
ADC1 avoids the ESP32 ADC2/Wi-Fi conflict.

Documentation checked:
- [Wokwi supported parts](https://docs.wokwi.com/getting-started/supported-hardware)
- [Potentiometer SIG/VCC/GND and control range](https://docs.wokwi.com/parts/wokwi-potentiometer)
- [Button contacts and bounce](https://docs.wokwi.com/parts/wokwi-pushbutton)
- [Switch pins](https://docs.wokwi.com/parts/wokwi-slide-switch)
- [ESP-IDF ESP32 ADC oneshot](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32/api-reference/peripherals/adc_oneshot.html)
- GPIO34/channel6 and GPIO35/channel7 checked against ESP-IDF v5.4.2
  `components/soc/esp32/include/soc/adc_channel.h`.
- [Wokwi flasher_args.json workflow](https://docs.wokwi.com/vscode/project-config)

LEDs show logical commands. They are not motor/fan power drivers. Physical
fail-safe circuitry, electrical noise, sensor disconnection voltages and analog
accuracy are outside this simulation.
