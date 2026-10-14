# Actual validation results

Validated locally on macOS arm64 using ESP-IDF **v5.4.2**, its Xtensa GCC
**14.2.0** compiler, AppleClang **15.0.0**, CMake and Python 3.

| Firmware configuration | Compilation | Application binary size |
| --- | --- | --- |
| Offline | Pass | 216,240 bytes |
| MQTT + control stall | Pass | 898,816 bytes |

Both final firmware builds completed without compiler warnings or errors. Sizes
are compiled artifacts, not runtime performance measurements. The two builds
use separate sdkconfig/build directories. The network profile enables MQTT and
the deliberate GPIO33 control-stall demonstration. The default Wokwi configuration
loads the offline build.

Native test results:
- **35 C++ contract cases passed** (including 2000 deterministic mixed-input
  steps that exercise both faults and successful restarts).
- **2/2 CTest entries passed**: the C++ suite and Python JSON/demo acceptance.
- **18 native-demo JSON frames** parsed and checked for normal, warning, fault,
  recovery readiness, restart, timeout and vibration-fault behavior.
- AddressSanitizer and UndefinedBehaviorSanitizer build: **2/2 CTest entries
  passed**, with no reported sanitizer errors.
- Diagram JSON parsed; part IDs unique; wire endpoints reference existing parts.
- Wokwi flash layout checked: all referenced offline binaries and ELF exist.

[Exact case output](native-test-results.txt), [native walkthrough](host-demo.jsonl).

## Coverage limits

- **Wokwi runtime: not executed.** No Wokwi CLI or simulation token was available.
- GPIO/ADC response, ISR debounce under Wokwi, FreeRTOS scheduling and supervisor
  override under an actual task stall require the manual simulator acceptance.
- **Live Wi-Fi/MQTT connection, reconnect behavior and broker disconnect: not
  executed.** Native tests verify the pure disconnect/control boundary and the
  production retry policy, not ESP-IDF socket behavior.
- FreeRTOS queue capacity/overflow execution was not run natively. Tests check
  production overflow counters and the corresponding conservative interlock.
- Runtime stack use, maximum execution time, jitter and response latency:
  **not measured**. Runtime instrumentation exists; no values are fabricated.
- GitHub Actions workflow supplied; remote CI execution not observed.
- No physical equipment or certified safety validation.

## Artifact identities

SHA-256 hashes tie these results to the locally built binaries:

```text
8ec42e4bc0174c514b014ea9d2d2f3f0ba2b9837bb4203be552edc2533eff17c  build/IndustrialRTOS.bin
fd05ed886da9230cb920b09d1b10fb6a83719311d1de2223329e6e4c5927ffe0  build-mqtt/IndustrialRTOS.bin
```
