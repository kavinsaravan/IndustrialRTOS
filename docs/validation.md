# Validation checklist

Record real results here; do not infer a simulator pass from a firmware build.

## Manual Wokwi acceptance

1. Build with ESP-IDF v5.4.2: `idf.py set-target esp32`, `idf.py build`.
2. Open this directory in VS Code with Wokwi installed and start simulation.
   `wokwi.toml` uses `build/flasher_args.json` for the complete flash layout.
3. Confirm startup, valid measurements, and normal motor LED enable.
4. Increase temperature to >=65 °C: WARNING and fan on.
5. Move around 63–65 °C: warning must not chatter; it clears below 62 °C.
6. Increase to >=80 °C: FAULT_LATCHED, motor off, fan and alarm on.
7. Press ACK while hot: motor stays off and rejection is logged.
8. Restore temperature below 60 °C and vibration below 4 units. Hold safe
   readings for at least 3 seconds: RECOVERY_READY, motor still off.
9. Press and release ACK: NORMAL, motor on. Holding ACK before recovery must
   not serve as a later acknowledgment; a new press is required.
10. Raise vibration to >=7 units: motor off; restore and repeat recovery.
11. Toggle INVALID: invalid-sample count increases, motor off. Toggle back;
    safe dwell and a new ACK are still required.
12. Toggle STOP SAMPLES: motor off when sample age exceeds 350 ms; restore,
    dwell, ACK.
13. For deadline injection, build with the demo control-stall option enabled,
    toggle its virtual switch and confirm the diagnostics supervisor independently
    disables the motor. Disable the option for the normal build.
14. If network support is enabled, disconnect the broker and Wi-Fi. Sensor/control
    heartbeats and LED fault response must continue. Record this separately from
    offline acceptance.

## Evidence boundaries

Host tests verify deterministic logic with virtual time. They do not execute the
FreeRTOS scheduler, GPIO ISR, ADC peripheral, Wi-Fi stack or Wokwi analog model.
ESP32 compilation verifies toolchain/API compatibility, not runtime operation.
Stack high-water marks and jitter are runtime observations in the simulator,
not hardware real-time guarantees. No physical hardware or certified functional
safety validation is part of this project.
